#version 450

layout(location = 0) in float vKind;

layout(location = 0) out vec4 outColor;

void main() {
    int k = int(vKind + 0.5);
    vec3 minor = vec3(0.20, 0.21, 0.24);
    vec3 major = vec3(0.32, 0.34, 0.38);
    vec3 xAxis = vec3(0.88, 0.28, 0.24);
    vec3 zAxis = vec3(0.24, 0.52, 0.95);
    vec3 c = minor;
    if (k == 1) {
        c = major;
    } else if (k == 2) {
        c = xAxis;
    } else if (k == 3) {
        c = zAxis;
    }
    outColor = vec4(c, 1.0);
}
