// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec3 point;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 albedo;
layout(location=3) flat in int surface;
layout(location=4) in float ambient;
layout(location=5) in float sunlight;
layout(location=6) in vec2 uv;
layout(location=7) in vec3 tangent;
layout(location=8) in vec3 indirect;
layout(set=0,binding=8) uniform sampler2DArray baseColors;
// Tangent normal XY (positive Z reconstructed), perceptual roughness, metallic.
layout(set=0,binding=9) uniform sampler2DArray normalRoughMetal;
layout(set=0,binding=10) uniform samplerCube environmentProbe;
layout(set=0,binding=11) uniform sampler2D integratedBrdf;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Scene { mat4 projection; vec4 eyeExposure; vec4 lighting; } s;
const float PI=3.141592654;
vec3 tonemap(vec3 color) {
    // Luminance-preserving extended Reinhard, white point 4; the target performs sRGB encoding.
    color=max(color,vec3(0))*s.eyeExposure.w;
    float y=dot(color,vec3(.2126,.7152,.0722));
    return clamp(color*(1.+y/16.)/(1.+y),0.,1.);
}
void main() {
    if(surface==4) { outColor=vec4(tonemap(albedo*4.),1);return; }
    vec4 textureColor=texture(baseColors,vec3(uv,surface));
    vec4 material=texture(normalRoughMetal,vec3(uv,surface));
    vec3 base=albedo*textureColor.rgb;
    vec3 ng=normalize(normal),t=normalize(tangent-ng*dot(ng,tangent));
    vec2 xy=material.xy*2.-1.;
    vec3 mapped=vec3(xy,sqrt(max(1.-dot(xy,xy),.001)));
    vec3 n=normalize(mat3(t,cross(ng,t),ng)*mapped);
    vec3 v=normalize(s.eyeExposure.xyz-point),l=normalize(s.lighting.xyz),h=normalize(v+l);
    if(surface==6 && dot(n,v)<0.)n=-n;
    float rough=clamp(material.z,.055,1.),metal=material.w;
    // Specular anti-aliasing supplements the prefiltered material mip chains.
    vec3 dx=dFdx(n),dy=dFdy(n);
    rough=sqrt(clamp(rough*rough+.20*max(dot(dx,dx),dot(dy,dy)),.003,1.));
    float nv=max(dot(n,v),.001),nl=max(dot(n,l),0.),nh=max(dot(n,h),0.),vh=max(dot(v,h),0.);
    float a2=rough*rough*rough*rough;
    float d=nh*nh*(a2-1.)+1.;
    float distribution=a2/max(PI*d*d,1e-7);
    float visibility=.5/max(nl*sqrt(nv*nv*(1.-a2)+a2)+nv*sqrt(nl*nl*(1.-a2)+a2),1e-5);
    vec3 f0=mix(vec3(.04),base,metal);
    vec3 fresnel=f0+(1.-f0)*pow(1.-vh,5.);
    vec3 diffuse=(1.-fresnel)*(1.-metal)*base/PI;
    int kind=int(s.lighting.w);
    vec3 sun=kind==4?vec3(.65,1.15,2.1):kind==5?vec3(4.5,2.55,1.15):vec3(3.4,3.12,2.6);
    vec3 direct=(diffuse+fresnel*distribution*visibility)*sun*nl*sunlight;
    vec3 fView=f0+(max(vec3(1.-rough),f0)-f0)*pow(1.-nv,5.);
    // Diffuse IBL and nearby color bounce are baked per vertex; no double AO multiplication.
    vec3 diffuseIndirect=base*(1.-metal)*(1.-fView)*max(indirect,vec3(0))*textureColor.a;
    float maxLod=float(textureQueryLevels(environmentProbe)-1);
    vec3 reflected=textureLod(environmentProbe,reflect(-v,n),rough*maxLod).rgb;
    vec2 dfg=texture(integratedBrdf,vec2(nv,rough)).rg;
    float specularAO=mix(1.,clamp(ambient*textureColor.a,0.,1.),rough);
    vec3 color=direct+diffuseIndirect+reflected*(f0*dfg.x+dfg.y)*specularAO;
    if(surface==6)color+=base*sun*.06*max(dot(-n,l),0.)*sunlight;
    if(surface==8) {
        float bands=sin(point.y*5.+.7*sin(point.x*.8)+.4*sin(point.z*1.1));
        color*=.78+.22*smoothstep(-.2,.8,bands);
        color+=vec3(.025,.08,.12)*pow(1.-nv,4.);
    }
    vec3 fog=kind==4?vec3(.025,.045,.075):kind==5?vec3(.28,.28,.32):vec3(.35,.42,.40);
    color=mix(color,fog,smoothstep(12.,32.,length(point-s.eyeExposure.xyz))*.35);
    outColor=vec4(tonemap(color),1);
}
