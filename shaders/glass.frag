// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=1) uniform sampler2D backdrop;
layout(push_constant) uniform Environment {
    vec4 origin; vec4 forward; vec4 right; vec4 up; vec4 options; vec4 shape;
} e;
layout(location=0) out vec4 outColor;
void main(){
    vec2 halfSize=vec2(e.shape.x,1.0)*.5;
    vec2 q=abs(uv*vec2(e.shape.x,1.0)-halfSize)-(halfSize-e.shape.y);
    float sd=length(max(q,0.0))+min(max(q.x,q.y),0.0)-e.shape.y;
    float alpha=(1.0-smoothstep(-.001,.001,sd))*e.options.w;
    outColor=vec4(texture(backdrop,uv).rgb,alpha);
}
