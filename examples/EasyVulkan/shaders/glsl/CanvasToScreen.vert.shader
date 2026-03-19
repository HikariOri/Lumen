<<<<<<< HEAD
#version 460
#pragma shader_stage(vertex)

vec2 positions[4] = {
    { 0, 0 },
    { 0, 1 },
    { 1, 0 },
    { 1, 1 }
};

layout(location = 0) out vec2 o_TexCoord;

layout(push_constant) uniform pushConstants {
    vec2 viewportSize;
    vec2 canvasSize;
};

void main() {
    o_TexCoord = positions[gl_VertexIndex];
    gl_Position = vec4(2 * positions[gl_VertexIndex] * canvasSize / viewportSize - 1, 0, 1);
=======
#version 450
#pragma shader_stage(vertex)

vec2 positions[3] = {
    {    0, -.5f },
    { -.5f,  .5f },
    {  .5f,  .5f }
};

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0, 1);
>>>>>>> 828fd4f0d2f439a7f0baad72cfb4d0bd293cc720
}
