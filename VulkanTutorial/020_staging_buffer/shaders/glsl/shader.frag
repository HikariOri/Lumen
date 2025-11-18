#version 450

// Vertex shader 里输出变量名不必和 Fragment shader 里的一致
// 只需保证他们的 location（索引）对上即可
layout(location = 0) in vec3 fragColor;

//  指定帧缓冲区的所索引
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
