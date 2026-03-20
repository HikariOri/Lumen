#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
    mat3 normalMatrix;
};

void main() {
    gl_Position = mvp * vec4(inPosition, 1.0);
    fragUV = inUV;
    fragNormal = normalMatrix * inNormal;
}
