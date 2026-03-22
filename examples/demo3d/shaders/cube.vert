#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;

struct GPULight {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 mvp;
    mat4 normalMatrix;
    vec4 cameraWorld;
    GPULight lights[8];
    vec4 sceneParams;
} ubo;

void main() {
    vec4 world = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = world.xyz;
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    fragUV = inUV;
    fragNormal = mat3(ubo.normalMatrix) * inNormal;
}
