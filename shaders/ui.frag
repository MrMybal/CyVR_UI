#version 450
layout(set=0,binding=0) uniform sampler2D atlas;
layout(set=0,binding=2) uniform sampler2D images;
layout(set=0,binding=3) uniform sampler2D video;
layout(set=0,binding=4) uniform sampler2D overlay;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=0) out vec4 outColor;
void main() {
    if(uv.x > 7.) {
        // Distance shapes from DrawList: the band selects a ring's inner edge, 0 being solid.
        float band=floor((uv.x-8.)/4.);
        float d=length(vec2(uv.x-10.-band*4.,uv.y));
        float aa=max(fwidth(d),1e-4);
        float coverage=clamp((1.-d)/aa+.5,0.,1.);
        if(band>0.) coverage*=clamp((d-band/255.)/aa+.5,0.,1.);
        outColor=vec4(color.rgb,color.a*coverage);
    }
    else if(uv.x < -4.5) outColor=color*texture(overlay,vec2(uv.x+6.,uv.y));
    else if(uv.x < -2.5) outColor=color*texture(video,vec2(uv.x+4.,uv.y));
    else if(uv.x < -.5) outColor=color*texture(images,vec2(uv.x+2.,uv.y));
    else outColor=vec4(color.rgb,color.a*texture(atlas,uv).r);
}
