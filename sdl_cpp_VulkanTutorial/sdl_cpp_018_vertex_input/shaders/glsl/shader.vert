#version 450

// 这里的 location 是顶点属性的 index
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

// vertex shader 和 fragment shader 中的变量名不一定要一样
// 只需让 location 对上即可
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
