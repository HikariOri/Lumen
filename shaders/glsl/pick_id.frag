#version 450

layout(location = 0) out uint outId;

layout(push_constant) uniform PC {
    uint id;
} pc;

void main() {
    outId = pc.id;
}
