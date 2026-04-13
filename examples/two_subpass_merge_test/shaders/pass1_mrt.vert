#version 450

layout(push_constant) uniform PC {
    uint tri;
} pc;

layout(location = 0) flat out uint v_tri;

const vec2 L[3] =
    vec2[3](vec2(-0.95, -0.9), vec2(-0.95, 0.9), vec2(-0.05, 0.9));
const vec2 R[3] =
    vec2[3](vec2(0.05, -0.9), vec2(0.95, -0.9), vec2(0.95, 0.9));

void main() {
    vec2 p = (pc.tri == 0u) ? L[gl_VertexIndex] : R[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);
    v_tri = pc.tri;
}
