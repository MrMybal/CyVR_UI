// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=5) uniform sampler2D lounge;
layout(set=0,binding=6) uniform sampler2D venice;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Environment {
    vec4 origin; vec4 forward; vec4 right; vec4 up; vec4 options; vec4 shape;
} e;
vec3 scene(vec3 ray){
    int kind=int(e.options.x+.5);
    if(kind==6)return vec3(0);
    if(kind==7)return mix(vec3(.004,.006,.010),vec3(.001,.002,.004),smoothstep(-.5,.7,ray.y));
    if(kind>=3) {
        float height=smoothstep(-.15,.85,ray.y);
        vec3 color;
        if(kind==4) {
            color=mix(vec3(.024,.035,.063),vec3(.004,.009,.026),height);
            vec2 star=vec2(atan(ray.x,-ray.z),asin(ray.y))*280.;
            vec2 cell=floor(star);float hash=fract(sin(dot(cell,vec2(127.1,311.7)))*43758.5453);
            float dotStar=1.-smoothstep(.01,.10,length(fract(star)-.5));
            color+=vec3(.50,.62,.80)*dotStar*step(.985,hash)*smoothstep(0.,.20,ray.y);
        } else {
            color=kind==5?mix(vec3(.67,.43,.31),vec3(.10,.25,.40),height):
                                mix(vec3(.60,.66,.64),vec3(.20,.40,.56),height);
            vec3 sun=normalize(kind==5?vec3(-.6,.30,-.8):vec3(-.5,.65,-.7));
            float disc=smoothstep(.9992,.9998,dot(ray,sun));
            color+=vec3(1.,.72,.40)*disc;
            color+=vec3(.20,.12,.055)*pow(max(dot(ray,sun),0.),32.);
        }
        return color*exp2((e.options.y-.65)*2.5);
    }
    int index=e.options.x>.5?1:0;
    if((int(e.shape.w)&(1<<index))!=0) {
        // Start facing the lounge window / sunset rather than a wall or pedestrians.
        float heading=index==0?.38:.35;
        vec2 coordinate=vec2(atan(ray.z,ray.x)*.159154943+.5+heading,acos(clamp(ray.y,-1.,1.))*.318309886);
        coordinate.y=clamp(coordinate.y,.00025,.99975);
        vec3 color=index==0?texture(lounge,coordinate).rgb:texture(venice,coordinate).rgb;
        return color*exp2((e.options.y-.65)*2.5);
    }
    bool night=e.options.x>0.5;
    vec3 floorColor=night?vec3(.009,.017,.035):vec3(.032,.061,.070);
    vec3 sky=night?vec3(.003,.007,.018):vec3(.023,.043,.057);
    vec3 color=mix(floorColor,sky,smoothstep(-.5,.7,ray.y));
    float glow=pow(max(dot(ray,normalize(vec3(-.7,.35,-1))),0.0),18.0);
    color+=glow*(night?vec3(.035,.04,.13):vec3(.065,.15,.16));
    // World-fixed studio floor. Derivatives keep distant grid lines stable in stereo.
    if(ray.y<-.015 && e.origin.y>0.0){
        float t=-e.origin.y/ray.y;
        vec2 p=(e.origin.xyz+ray*t).xz;
        vec2 fw=max(fwidth(p),vec2(.003));
        vec2 edge=abs(fract(p+.5)-.5)/fw;
        float grid=1.0-min(min(edge.x,edge.y),1.0);
        color+=vec3(.045,.075,.08)*grid*exp(-t*.12);
    }
    float azimuth=atan(ray.x,-ray.z);
    float strips=exp(-pow((abs(azimuth)-1.25)/.028,2.0))*smoothstep(-.2,.0,ray.y)*(1.0-smoothstep(.6,.8,ray.y));
    color+=strips*(night?vec3(.06,.09,.26):vec3(.15,.23,.25));
    return color*e.options.y;
}
void main(){
    vec2 xy=vec2(uv.x*2.0-1.0,1.0-uv.y*2.0);
    vec3 ray=normalize(e.forward.xyz+xy.x*e.right.xyz+xy.y*e.up.xyz);
    float alpha=e.options.w;
    if(e.shape.z>0.5){
        // Same rounded contour as the UI base. Text is rendered after the backdrop.
        vec2 p=uv*vec2(e.shape.x,1.0), halfSize=vec2(e.shape.x,1.0)*.5;
        vec2 q=abs(p-halfSize)-(halfSize-e.shape.y);
        float sd=length(max(q,0.0))+min(max(q.x,q.y),0.0)-e.shape.y;
        alpha*=1.0-smoothstep(-.001,.001,sd);
    }
    vec3 color=scene(ray);
    if(e.options.z>0.0){
        vec3 x=normalize(e.right.xyz)*e.options.z, y=normalize(e.up.xyz)*e.options.z;
        // A 3x3 separable Gaussian evaluated over the app-owned environment.
        color=scene(ray)*4.0;
        color+=(scene(normalize(ray+x))+scene(normalize(ray-x))+scene(normalize(ray+y))+scene(normalize(ray-y)))*2.0;
        color+=scene(normalize(ray+x+y))+scene(normalize(ray+x-y))+scene(normalize(ray-x+y))+scene(normalize(ray-x-y));
        color/=16.0;
    }
    outColor=vec4(color*e.origin.w,alpha);
}
