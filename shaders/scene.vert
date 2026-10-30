// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 material;
layout(location=4) in vec2 texcoord;
layout(location=5) in vec3 tangent;
layout(location=6) in vec3 bakedIndirect;
layout(push_constant) uniform Scene { mat4 projection; vec4 eyeExposure; vec4 lighting; } s;
layout(location=0) out vec3 point;
layout(location=1) out vec3 outNormal;
layout(location=2) out vec3 albedo;
layout(location=3) flat out int surface;
layout(location=4) out float ambient;
layout(location=5) out float sunlight;
layout(location=6) out vec2 uv;
layout(location=7) out vec3 outTangent;
layout(location=8) out vec3 indirect;
vec3 linearColor(vec3 c) {
    return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));
}
void main() {
    gl_Position=s.projection*vec4(position,1);
    point=position;outNormal=normal;albedo=linearColor(color.rgb);
    surface=int(material.x+.5);ambient=material.y;sunlight=color.a;
    uv=texcoord;outTangent=tangent;indirect=bakedIndirect;
}
