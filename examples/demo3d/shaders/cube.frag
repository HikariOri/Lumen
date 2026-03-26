#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) uint mode;
    layout(offset = 16) vec4 modelColor;
} pc;

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
    vec4 envParams; // x exposure, y maxMip, z diffMip, w iblStrength
} scene;

layout(set = 0, binding = 1) uniform samplerCube envMap;
layout(set = 0, binding = 2) uniform sampler2D brdfLUT;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 mrAoFactors;
    vec4 emissiveFactor;
    vec4 shaderParams;
} matUbo;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

const int kMaxLights = 8;
const float PI = 3.14159265359;

// ================= 工具函数 =================

vec3 srgb_to_linear_rgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.04045));
    vec3 a = c / 12.92;
    vec3 b = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(b, a, vec3(lo));
}

float distribution_ggx(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 1e-7);
}

float geometry_smith(float NdotL, float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float g1 = NdotL / (NdotL * (1.0 - k) + k);
    float g2 = NdotV / (NdotV * (1.0 - k) + k);

    return g1 * g2;
}

vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
                 * pow(1.0 - cosTheta, 5.0);
}

uint material_tex_mask() {
    return floatBitsToUint(matUbo.shaderParams.z);
}

// ================= 直射光 =================

void accum_light(
    GPULight L,
    vec3 N,
    vec3 V,
    vec3 albedo,
    float metallic,
    float roughness,
    inout vec3 Lo)
{
    vec3 Ldir;
    float atten = 1.0;

    if (L.position.w < 0.5) {
        Ldir = normalize(L.direction.xyz);
    } else {
        vec3 toLight = L.position.xyz - fragWorldPos;
        float dist = length(toLight);
        Ldir = toLight / dist;
        atten = 1.0 / (dist * dist);
    }

    float NdotL = max(dot(N, Ldir), 0.0);
    if (NdotL <= 0.0) return;

    vec3 H = normalize(V + Ldir);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 F = fresnel_schlick(VdotH, F0);
    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotL, NdotV, roughness);

    vec3 specular = (D * G * F) / (4.0 * NdotL * NdotV + 1e-4);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 diffuse = albedo / PI;

    vec3 radiance = L.color.rgb * L.color.w;

    Lo += (kD * diffuse + specular) * radiance * NdotL * atten;
}

// ================= 主函数 =================

void main() {

    vec3 V = normalize(scene.cameraWorld.xyz - fragWorldPos);
    vec3 N = normalize(fragNormal);

    uint tmask = material_tex_mask();

    // -------- albedo --------
    vec3 albedo;
    float alpha = 1.0;

    if ((tmask & 1u) != 0u) {
        vec4 tc = texture(albedoMap, fragUV);
        albedo = srgb_to_linear_rgb(tc.rgb) * matUbo.baseColorFactor.rgb;
        alpha = tc.a;
    } else {
        albedo = matUbo.baseColorFactor.rgb;
        alpha = matUbo.baseColorFactor.a;
    }

    albedo *= pc.modelColor.rgb;

    // -------- MR --------
    float metallic;
    float roughness;

    if ((tmask & 4u) != 0u) {
        vec4 mr = texture(metallicRoughnessMap, fragUV);
        metallic = mr.b * matUbo.mrAoFactors.x;
        roughness = mr.g * matUbo.mrAoFactors.y;
    } else {
        metallic = matUbo.mrAoFactors.x;
        roughness = matUbo.mrAoFactors.y;
    }

    roughness = clamp(roughness, 0.04, 1.0);

    // -------- AO --------
    float ao = ((tmask & 8u) != 0u)
        ? texture(aoMap, fragUV).r
        : 1.0;

    // ================= 直射光 =================
    vec3 Lo = vec3(0.0);

    int nLights = int(scene.sceneParams.x);
    nLights = min(nLights, kMaxLights);

    for (int i = 0; i < nLights; ++i) {
        accum_light(scene.lights[i], N, V, albedo, metallic, roughness, Lo);
    }

    // ================= IBL =================

    float exposure = scene.envParams.x;
    float maxMip = scene.envParams.y;
    float diffMip = scene.envParams.z;
    float iblStrength = scene.envParams.w;

    float NdotV = max(dot(N, V), 1e-4);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnel_schlick_roughness(NdotV, F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    // diffuse IBL
    vec3 irradiance = textureLod(envMap, N, diffMip).rgb * exposure;
    vec3 diffuseIBL = irradiance * albedo * kD * ao;

    // specular IBL
    vec3 R = reflect(-V, N);
    float lod = roughness * maxMip;

    vec3 prefiltered = textureLod(envMap, R, lod).rgb * exposure;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

    vec3 specIBL = prefiltered * (F * brdf.x + brdf.y) * ao;

    vec3 ambient = (diffuseIBL + specIBL) * iblStrength;

    // ================= emissive =================
    vec3 emissive = ((tmask & 16u) != 0u)
        ? texture(emissiveMap, fragUV).rgb * matUbo.emissiveFactor.rgb
        : matUbo.emissiveFactor.rgb;

    vec3 color = Lo + ambient + emissive;

    outColor = vec4(color, alpha);
}
