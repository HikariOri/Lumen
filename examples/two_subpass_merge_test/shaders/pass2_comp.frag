#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput u0;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput u1;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 a = subpassLoad(u0);
    vec4 b = subpassLoad(u1);
    outColor = vec4(0.5 * a.rgb + 0.5 * b.rgb, 1.0);
}
