#version 450

layout(location = 0) in vec3 inPos;

layout(location = 0) out vec3 vWorldDir;

layout(push_constant) uniform SkyPC {
    mat4 sky_mvp;
    vec4 params;
} pc;

void main() {
    vec4 clip = pc.sky_mvp * vec4(inPos, 1.0);
    gl_Position = clip.xyww;
    // inPos 为世界对齐单位立方体顶点，方向即世界空间采样向量（不可再乘 view 的旋转，
    // 否则会把上下颠倒/扭曲成“两块盒面”）。
    vWorldDir = inPos;
}
