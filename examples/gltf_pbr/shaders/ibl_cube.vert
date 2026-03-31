#version 450

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Push {
    mat4 projView;
} pc;

layout(location = 0) out vec3 vLocalDir;

void main() {
    vLocalDir = inPos;
    gl_Position = pc.projView * vec4(inPos, 1.0);
}
