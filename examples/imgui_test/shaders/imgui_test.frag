#version 450

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec3 cameraPos;
    float time;

    vec2 sceenSize;

    vec4 exposureIblMips;

    int debugMode;
} frame;

layout(set = 2, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;

    vec4 emissive; // rgb + intensity

    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;

    uint flags;
    uint alphaMode;

    float alphaCutoff;

} materialUBO;

layout(set = 2, binding = 1) uniform sampler2D baseColorTex;
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessTex;
layout(set = 2, binding = 3) uniform sampler2D normalTex;
layout(set = 2, binding = 4) uniform sampler2D occlusionTex;
layout(set = 2, binding = 5) uniform sampler2D emissiveTex;

// ----- IBL（继续往后排！）-----
layout(set = 3, binding = 1) uniform samplerCube irradianceMap;
layout(set = 3, binding = 2) uniform samplerCube prefilteredMap;
layout(set = 3, binding = 3) uniform sampler2D brdfLUT;

void main() {
    outColor = texture(baseColorTex, fragUV);
}
