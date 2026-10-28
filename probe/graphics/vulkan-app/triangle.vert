#version 450
layout(location=0) out vec3 color;
vec2 positions[3]=vec2[](vec2(0,-0.7),vec2(0.7,0.6),vec2(-0.7,0.6));
vec3 colors[3]=vec3[](vec3(1,0.15,0.1),vec3(0.1,1,0.15),vec3(0.15,0.2,1));
void main(){ gl_Position=vec4(positions[gl_VertexIndex],0,1); color=colors[gl_VertexIndex]; }
