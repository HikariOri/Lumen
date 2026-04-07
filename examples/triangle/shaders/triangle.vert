#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 v_color;

layout(set = 0, binding = 0) uniform TimeUbo { float time; } ubo;

void main() {
    const float c = cos(ubo.time);
    const float s = sin(ubo.time);
    const mat2 rot = mat2(c, -s, s, c);
    const vec2 p = rot * in_position;
    gl_Position = vec4(p, 0.0, 1.0);
    v_color = in_color;
}
