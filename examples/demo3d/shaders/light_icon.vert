#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

void main() {
    fragUV = inUV;
    gl_Position = pc.mvp * vec4(inPos, 0.0, 1.0);
}
