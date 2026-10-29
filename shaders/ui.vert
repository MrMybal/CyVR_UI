#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(push_constant) uniform Screen { vec2 size; } screen;
layout(location=0) out vec2 outUV;
layout(location=1) out vec4 outColor;
void main() { gl_Position=vec4(position/screen.size*2.0-1.0,0.0,1.0);outUV=uv;outColor=color; }
