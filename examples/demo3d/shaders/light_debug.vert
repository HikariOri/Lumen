#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform Push {
    mat4 vp;
} pc;

void main() {
    fragColor = inColor;
    gl_Position = pc.vp * vec4(inPos, 1.0);
}
