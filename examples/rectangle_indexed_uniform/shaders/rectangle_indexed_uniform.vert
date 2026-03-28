#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

layout(set = 0, binding = 1) uniform TimeUBO {
    vec4 data; // x = 运行时间（秒），y = 角速度（弧度/秒），由 CPU 写入
} u_time;

void main() {
    float angle = u_time.data.x * u_time.data.y;
    float c = cos(angle);
    float s = sin(angle);
    mat2 rot = mat2(c, -s, s, c);
    vec2 pos = rot * inPosition;
    gl_Position = vec4(pos, 0.0, 1.0);
    fragUV = inUV;
}
