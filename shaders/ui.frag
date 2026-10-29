#version 450
layout(set=0,binding=0) uniform sampler2D atlas;
layout(set=0,binding=2) uniform sampler2D images;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=0) out vec4 outColor;
void main() {
    if(uv.x < -.5) outColor=color*texture(images,vec2(uv.x+2.,uv.y));
    else outColor=vec4(color.rgb,color.a*texture(atlas,uv).r);
}
