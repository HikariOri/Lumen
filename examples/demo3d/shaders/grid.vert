#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inKind;

layout(push_constant) uniform GridPush {
    mat4 viewProj;
} pc;

layout(location = 0) out float vKind;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    vKind = inKind;
}
