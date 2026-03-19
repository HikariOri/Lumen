#version 450

// 无顶点缓冲，使用 gl_VertexIndex 生成三角形
void main() {
    // 顶点顺序为 CCW（与 frontFace 匹配），避免被背面剔除
    vec2 positions[3] = vec2[](
        vec2(0.0, -0.5),
        vec2(-0.5, 0.5),
        vec2(0.5, 0.5)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
