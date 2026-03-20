#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

layout(set = 0, binding = 0) uniform UBO {
    vec2 position;
    float rotation;
};

void main() {
    // 旋转后平移到 position
    float c = cos(rotation);
    float s = sin(rotation);
    mat2 rot = mat2(c, s, -s, c);
    vec2 pos = rot * inPosition + position;

    gl_Position = vec4(pos, 0.0, 1.0);
    fragUV = inUV;
}
