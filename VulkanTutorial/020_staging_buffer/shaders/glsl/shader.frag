#version 450

// vertex shader 和 fragment shader 中的变量名不一定要一样
// 只需让 location 对上即可
layout(location = 0) in vec3 fragColor;

// render target index
layout (location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
