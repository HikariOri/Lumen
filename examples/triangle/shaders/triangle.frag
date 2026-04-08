#version 450

layout(set = 0, binding = 1) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 o_color;

void main() {
    o_color = texture(tex_sampler, v_uv) * vec4(v_color, 1.0);
}
