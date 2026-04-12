/*
set0 → frame（camera / time）
set1 → object（dynamic UBO）
set2 → material
set3 → lighting（light + shadow）
set4 → pass input（GBuffer / shadow map） ⭐
set5 → IBL（可选）
*/

#version 450

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec2 fragUV;

layout (set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec3 cameraPos;
    float time;

    vec2 sceenSize;

    vec4 exposureIblMips;

    int debugMode;
    
} frameUBO;

layout (set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    mat4 normalMatrix;
} objectUBO;

void main() {
    // gl_Position = objectUBO.model * vec4(inPos, 0.0, 1.0);
    gl_Position = vec4(inPos, 0.0, 1.0);

    fragUV = inUV;
}
