// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Environment {
    vec4 origin; vec4 forward; vec4 right; vec4 up; vec4 options; vec4 shape;
} e;
vec3 scene(vec3 ray){
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
        // A 3x3 separable Gaussian evaluated over the app-owned procedural environment.
        color=scene(ray)*4.0;
        color+=(scene(normalize(ray+x))+scene(normalize(ray-x))+scene(normalize(ray+y))+scene(normalize(ray-y)))*2.0;
        color+=scene(normalize(ray+x+y))+scene(normalize(ray+x-y))+scene(normalize(ray-x+y))+scene(normalize(ray-x-y));
        color/=16.0;
    }
    outColor=vec4(color,alpha);
}
