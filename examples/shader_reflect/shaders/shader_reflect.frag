#version 450

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec3 cameraPos;

    vec4 exposureIblMips;

    int debugMode;
    int _framePad0;
    int _framePad1;
    int _framePad2;
} frameUBO;

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
    const vec2 uv = vec2(0.0);
    const vec3 cubeDir = vec3(0.0, 0.0, 1.0);

    // 仅占位：让编译器保留对 UBO / 纹理的引用，不参与最终颜色
    // vec4 pretend = vec4(0.0);
    // pretend += vec4(vec3(texture(baseColorTex, uv).rgb), 0.0);
    // pretend += vec4(texture(metallicRoughnessTex, uv).b * materialUBO.metallicFactor);
    // pretend += vec4(texture(normalTex, uv).rgb, 0.0);
    // pretend += vec4(texture(occlusionTex, uv).r);
    // pretend +=
    //     vec4(texture(emissiveTex, uv).rgb * materialUBO.emissive.rgb * materialUBO.emissive.a,
    //          0.0);
    // pretend += vec4(texture(irradianceMap, cubeDir).rgb, 0.0);
    // pretend += vec4(texture(prefilteredMap, cubeDir).rgb, 0.0);
    // pretend += vec4(texture(brdfLUT, uv).rg, 0.0, 0.0);

    // pretend.x += float(materialUBO.flags) * 0.0;
    // pretend.x += float(frameUBO.debugMode) * 0.0;

    // outColor = vec4(fragColor, 1.0) + pretend * 0.0;
    outColor = vec4(fragColor, 1.0);
}
