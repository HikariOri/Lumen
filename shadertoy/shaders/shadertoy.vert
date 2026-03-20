#version 450

// 全屏三角形，无需顶点缓冲（用 gl_VertexIndex 生成）
// 用于 Shadertoy 风格全屏片段着色

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
