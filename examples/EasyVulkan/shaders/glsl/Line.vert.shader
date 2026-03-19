#version 450
#pragma shader_stage(vertex)

layout(push_constant) uniform pushConstants {
    vec2 viewportSize;
    vec2 offset[2];
};

void main() {
    gl_Position = vec4(2 * offset[gl_VertexIndex] / viewportSize - 1, 0, 1);
}
