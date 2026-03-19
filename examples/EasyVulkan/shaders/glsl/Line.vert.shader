#version 450
#pragma shader_stage(vertex)

layout(push_constant) uniform pushConstants {
    vec2 viewportSize;
    vec2 offset[2];
};

void main() {
<<<<<<< HEAD
    gl_Position = vec4(2 * offset[gl_VertexIndex] / viewportSize - 1, 0, 1);
=======
    gl_Position = vec4(2 * offsets[gl_VertexIndex] / viewportSize - 1, 0, 1);
>>>>>>> 828fd4f0d2f439a7f0baad72cfb4d0bd293cc720
}
