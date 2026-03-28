#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vNormalWS;
layout(location = 2) out vec3 vPosWS;

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
} uScene;

void main() {
    vec4 posWS = uScene.model * vec4(inPosition, 1.0);
    vPosWS = posWS.xyz;
    gl_Position = uScene.mvp * vec4(inPosition, 1.0);
    vUV = inUV;
    vNormalWS = mat3(uScene.normalMatrix) * inNormal;
}
