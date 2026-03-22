#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 vWorldRay;

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
    mat4 skyMvp;
    mat4 skyOrientInv;
    vec4 pbrParams;
    vec4 envParams;
} ubo;

void main() {
    vec4 clip = ubo.skyMvp * vec4(inPosition, 1.0);
    gl_Position = clip.xyww;
    vWorldRay = (ubo.skyOrientInv * vec4(inPosition, 0.0)).xyz;
}
