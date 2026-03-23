#version 450

layout(location = 0) in vec3 vWorldRay;

layout(location = 0) out vec4 outColor;

struct GPULight {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 model;
    mat4 mvp;
    mat4 normalMatrix;
    vec4 cameraWorld;
    GPULight lights[8];
    vec4 sceneParams;
    mat4 skyMvp;
    mat4 skyOrientInv;
    vec4 envParams;
} ubo;

layout(set = 0, binding = 1) uniform samplerCube envMap;

void main() {
    vec3 dir = normalize(vWorldRay);
    float exposure = ubo.envParams.x;
    vec3 c = texture(envMap, dir).rgb * exposure;
    outColor = vec4(c, 1.0);
}
