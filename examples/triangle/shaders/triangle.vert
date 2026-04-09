#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(set = 0, binding = 0) uniform UboViewProj {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 v_color;

void main() {
    gl_Position = ubo.proj * ubo.view * vec4(in_position, 0.0, 1.0);
    v_color = in_color;
}
