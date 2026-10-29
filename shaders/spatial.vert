#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
layout(location=2) in vec3 normal;
layout(push_constant) uniform Camera { mat4 viewProjection; } camera;
layout(location=0) out vec4 outColor;
layout(location=1) out vec3 outNormal;
void main(){gl_Position=camera.viewProjection*vec4(position,1.0);outColor=color;outNormal=normal;}
