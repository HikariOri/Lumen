#version 450

layout(location = 0) flat in uint v_tri;

layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;

void main() {
    if (v_tri == 0u) {
        o0 = vec4(1.0, 0.2, 0.1, 1.0);
        o1 = vec4(0.1, 0.25, 1.0, 1.0);
    } else {
        o0 = vec4(0.1, 1.0, 0.25, 1.0);
        o1 = vec4(1.0, 0.95, 0.1, 1.0);
    }
}
