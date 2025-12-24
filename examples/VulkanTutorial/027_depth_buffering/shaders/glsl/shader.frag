#version 450

layout(binding = 1) uniform sampler2D texSampler;

// Vertex shader 里输出变量名不必和 Fragment shader 里的一致
// 只需保证他们的 location（索引）对上即可
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// 渲染目标
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, fragTexCoord);
}
