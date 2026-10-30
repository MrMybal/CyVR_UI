// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=7) uniform sampler2D sceneBackdrop;
layout(push_constant) uniform Environment {
    vec4 origin; vec4 forward; vec4 right; vec4 up; vec4 options; vec4 shape;
} e;
layout(location=0) out vec4 outColor;
void main() {
    vec2 radius=vec2(e.options.z*.35);
    vec3 color=texture(sceneBackdrop,uv).rgb*4.;
    color+=(texture(sceneBackdrop,uv+vec2(radius.x,0)).rgb+texture(sceneBackdrop,uv-vec2(radius.x,0)).rgb+
            texture(sceneBackdrop,uv+vec2(0,radius.y)).rgb+texture(sceneBackdrop,uv-vec2(0,radius.y)).rgb)*2.;
    color+=texture(sceneBackdrop,uv+radius).rgb+texture(sceneBackdrop,uv-radius).rgb+
           texture(sceneBackdrop,uv+vec2(radius.x,-radius.y)).rgb+texture(sceneBackdrop,uv+vec2(-radius.x,radius.y)).rgb;
    outColor=vec4(color/16.,1);
}
