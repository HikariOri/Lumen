#version 450

// 无顶点缓冲全屏三角（位运算得到 (0,0)(2,0)(0,2) 的 NDC 大三角）

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
