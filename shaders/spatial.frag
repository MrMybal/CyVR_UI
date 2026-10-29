#version 450
layout(location=0) in vec4 color;
layout(location=1) in vec3 normal;
layout(location=0) out vec4 outColor;
void main(){
    vec3 n=normalize(normal);
    float key=max(dot(n,normalize(vec3(-.4,1,.6))),0.0);
    float fill=max(dot(n,normalize(vec3(.8,.2,-.4))),0.0);
    float rim=pow(1.0-abs(n.z),3.0);
    outColor=vec4(color.rgb*(.43+.49*key+.16*fill)+vec3(.06,.1,.12)*rim,color.a);
}
