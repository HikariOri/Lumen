#version 450

// 无 vertex buffer：用 gl_VertexIndex 画 clip 空间内的小三角形（NDC，非全屏）
const vec2 k_positions[3] = vec2[](vec2(0.0, -0.55), vec2(0.55, 0.45),
                                   vec2(-0.55, 0.45));

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = k_positions[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);
    v_uv = p * 0.5 + 0.5;
}
