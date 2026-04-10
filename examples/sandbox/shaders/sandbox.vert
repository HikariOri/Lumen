#version 450

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec3 inColor;

layout (location = 0) out vec3 fragColor;

layout (set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec3 cameraPos;
    float time;

    vec2 sceenSize;
} sceneUBO;

layout (set = 2, binding = 0) uniform ObjectUBO {
    mat4 model;
} objectUBO;


void main() {
    gl_Position = objectUBO.model * vec4(inPos, 0.0, 1.0);

    fragColor = inColor;
}
