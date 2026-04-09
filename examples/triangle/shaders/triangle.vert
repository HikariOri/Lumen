#version 450

/// 三角形完全在着色器内定义：`Draw` 仅提交 3 个顶点索引（0、1、2），无顶点缓冲与 UBO。

const vec2 k_positions[3] = vec2[](vec2(0.0, 0.55), vec2(-0.5, -0.45),
                                   vec2(0.5, -0.45));

const vec3 k_colors[3] =
    vec3[](vec3(1.0, 0.2, 0.3), vec3(0.2, 1.0, 0.3), vec3(0.3, 0.5, 1.0));

layout(location = 0) out vec3 v_color;

void main() {
    gl_Position = vec4(k_positions[gl_VertexIndex], 0.0, 1.0);
    v_color = k_colors[gl_VertexIndex];
}
