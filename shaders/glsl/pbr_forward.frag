#version 450

// `note/forward_shader.md`：Frame / Material / Object / Lighting + IBL + exp 色调 + gamma
// Debug 模式与输出组织见 `note/shader debug.md`

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec2 vUv;
layout(location = 2) in mat3 vTbn;
layout(location = 5) in vec3 vGeomNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec4 cameraPos;

    vec4 exposureIblMips;

    int debugMode;
    int _framePad0;
    int _framePad1;
    int _framePad2;
} frame;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;

    vec4 emissive; // rgb + intensity

    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;

    uint flags;
    uint alphaMode;

    float alphaCutoff;

    float _matPad0;
    float _matPad1;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTex;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 3) uniform sampler2D normalTex;
layout(set = 1, binding = 4) uniform sampler2D occlusionTex;
layout(set = 1, binding = 5) uniform sampler2D emissiveTex;

#define MAX_LIGHTS 64

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(set = 3, binding = 0) uniform LightUBO {
    int lightCount;
    int _lPad0;
    int _lPad1;
    int _lPad2;
    Light lights[MAX_LIGHTS];
} lighting;

#define DEBUG_NONE                 0
#define DEBUG_NORMAL_WS            1
#define DEBUG_NORMAL_TS            2
#define DEBUG_DEPTH                3
#define DEBUG_ALBEDO               10
#define DEBUG_METALLIC             11
#define DEBUG_ROUGHNESS            12
#define DEBUG_AO                   13
#define DEBUG_DIRECT_DIFFUSE       20
#define DEBUG_DIRECT_SPECULAR      21
#define DEBUG_IBL_DIFFUSE          22
#define DEBUG_IBL_SPECULAR         23
#define DEBUG_EMISSIVE             24
#define DEBUG_FINAL_NO_IBL         30
#define DEBUG_FINAL_NO_DIRECT      31
#define DEBUG_HEAT_LIGHT_INTENSITY 50
#define DEBUG_HEAT_NDOTL           51
#define DEBUG_HEAT_LIGHT_COUNT     52

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2

const float k_pi = 3.14159265358979323846;

struct PbrResult {
    vec3 directDiffuse;
    vec3 directSpecular;
    vec3 iblDiffuse;
    vec3 iblSpecular;
    vec3 emissive;
};

float distributionGgx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndh = max(dot(N, H), 0.0);
    float ndh2 = ndh * ndh;
    float denom = ndh2 * (a2 - 1.0) + 1.0;
    return a2 / (k_pi * denom * denom + 1e-6);
}

float geometrySchlickGgx(float ndotx, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotx / (ndotx * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float g1 = geometrySchlickGgx(max(dot(N, V), 0.0), roughness);
    float g2 = geometrySchlickGgx(max(dot(N, L), 0.0), roughness);
    return g1 * g2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    float ct = clamp(cosTheta, 0.0, 1.0);
    return F0 + (1.0 - F0) * pow(1.0 - ct, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    float ct = clamp(cosTheta, 0.0, 1.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - ct, 5.0);
}

void evalLightBrdf(Light light, vec3 N, vec3 V, vec3 fragPos, float roughness,
                   vec3 albedo, float metallic, vec3 F0, out vec3 diffuseOut,
                   out vec3 specOut, out float ndotlOut) {
    diffuseOut = vec3(0.0);
    specOut = vec3(0.0);
    ndotlOut = 0.0;

    int type = int(light.position.w + 0.5);
    vec3 L;
    float attenuation = 1.0;
    float range = max(light.params.x, 0.05);

    if (type == LIGHT_DIRECTIONAL) {
        L = normalize(-light.direction.xyz);
    } else {
        vec3 toL = light.position.xyz - fragPos;
        float dist = length(toL);
        L = toL / max(dist, 1e-4);
        attenuation = 1.0 / max(dist * dist, 1e-4);
        attenuation *= clamp(1.0 - dist / range, 0.0, 1.0);

        if (type == LIGHT_SPOT) {
            float innerCos = light.params.y;
            float outerCos = light.params.z;
            float theta = dot(L, normalize(-light.direction.xyz));
            float epsilon = max(innerCos - outerCos, 1e-4);
            float spotI = clamp((theta - outerCos) / epsilon, 0.0, 1.0);
            attenuation *= spotI;
        }
    }

    vec3 radiance = light.color.rgb * light.color.a * attenuation;
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return;
    }

    ndotlOut = NdotL;

    vec3 H = normalize(V + L);
    float NDF = distributionGgx(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specNumer = NDF * G * F;
    float specDenom = 4.0 * max(dot(N, V), 1e-4) * NdotL + 1e-4;
    vec3 specular = specNumer / specDenom;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    diffuseOut = kD * albedo / k_pi * radiance * NdotL;
    specOut = specular * radiance * NdotL;
}

vec3 heatmap(float v) {
    v = clamp(v, 0.0, 1.0);
    return vec3(
        smoothstep(0.5, 1.0, v),
        smoothstep(0.0, 1.0, v),
        1.0 - smoothstep(0.0, 0.5, v));
}

vec3 getDebugColor(int mode, PbrResult r, vec3 N, vec3 mapN_ts, vec3 albedo,
                   float metallic, float roughness, float ao, float depthNorm,
                   float ndotlMax, int lc) {
    switch (mode) {
    case DEBUG_NORMAL_WS:
        return N * 0.5 + 0.5;
    case DEBUG_NORMAL_TS:
        return mapN_ts * 0.5 + 0.5;
    case DEBUG_DEPTH:
        return vec3(depthNorm);
    case DEBUG_ALBEDO:
        return albedo;
    case DEBUG_METALLIC:
        return vec3(metallic);
    case DEBUG_ROUGHNESS:
        return vec3(roughness);
    case DEBUG_AO:
        return vec3(ao);
    case DEBUG_DIRECT_DIFFUSE:
        return r.directDiffuse;
    case DEBUG_DIRECT_SPECULAR:
        return r.directSpecular;
    case DEBUG_IBL_DIFFUSE:
        return r.iblDiffuse;
    case DEBUG_IBL_SPECULAR:
        return r.iblSpecular;
    case DEBUG_EMISSIVE:
        return r.emissive;
    case DEBUG_FINAL_NO_IBL:
        return r.directDiffuse + r.directSpecular;
    case DEBUG_FINAL_NO_DIRECT:
        return r.iblDiffuse + r.iblSpecular + r.emissive;
    case DEBUG_HEAT_LIGHT_INTENSITY: {
        float v = length(r.directDiffuse + r.directSpecular);
        return heatmap(v / 10.0);
    }
    case DEBUG_HEAT_NDOTL:
        return heatmap(ndotlMax);
    case DEBUG_HEAT_LIGHT_COUNT:
        return heatmap(float(lc) / 64.0);
    default:
        return r.directDiffuse + r.directSpecular + r.iblDiffuse +
               r.iblSpecular + r.emissive;
    }
}

void main() {
    vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
    vec3 mapN = texture(normalTex, vUv).rgb * 2.0 - 1.0;
    vec3 N = normalize(vTbn * mapN);

    vec4 baseSam = texture(baseColorTex, vUv) * material.baseColorFactor;
    vec3 albedo = baseSam.rgb;
    float metallic =
        texture(metallicRoughnessTex, vUv).b * material.metallicFactor;
    float roughness =
        max(texture(metallicRoughnessTex, vUv).g * material.roughnessFactor,
            0.04);
    roughness = min(roughness, 1.0);
    float aoSample = texture(occlusionTex, vUv).r;
    float ao = mix(1.0, aoSample, clamp(material.occlusionStrength, 0.0, 1.0));
    vec3 emissive = texture(emissiveTex, vUv).rgb * material.emissive.rgb
                      * material.emissive.a;

    float exposure = frame.exposureIblMips.x;
    float iblStrength = frame.exposureIblMips.y;
    float maxMip = frame.exposureIblMips.z;
    float diffMip = frame.exposureIblMips.w;

    float NdotV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS = F_ibl;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = textureLod(irradianceMap, N, diffMip).rgb;
    vec3 diffuseIbl = irradiance * albedo;

    vec3 R = reflect(-V, N);
    float lod = roughness * maxMip;
    vec3 prefiltered = textureLod(prefilteredMap, R, lod).rgb;
    vec2 brdf =
        texture(brdfLUT, vec2(clamp(dot(N, V), 0.0, 1.0), roughness)).rg;
    vec3 specIbl = prefiltered * (F0 * brdf.x + brdf.y);

    vec3 iblDiffuse = kD * diffuseIbl * ao * iblStrength;
    vec3 iblSpecular = specIbl * ao * iblStrength;

    vec3 directDiff = vec3(0.0);
    vec3 directSpec = vec3(0.0);
    float ndotlMax = 0.0;
    int n = clamp(lighting.lightCount, 0, MAX_LIGHTS);
    for (int i = 0; i < n; ++i) {
        vec3 dd;
        vec3 ds;
        float nl;
        evalLightBrdf(lighting.lights[i], N, V, vWorldPos, roughness, albedo,
                      metallic, F0, dd, ds, nl);
        directDiff += dd;
        directSpec += ds;
        ndotlMax = max(ndotlMax, nl);
    }

    PbrResult r;
    r.directDiffuse = directDiff;
    r.directSpecular = directSpec;
    r.iblDiffuse = iblDiffuse;
    r.iblSpecular = iblSpecular;
    r.emissive = emissive;

    float depthNorm =
        clamp(length(frame.cameraPos.xyz - vWorldPos) / 100.0, 0.0, 1.0);

    vec3 color = getDebugColor(frame.debugMode, r, N, mapN, albedo, metallic,
                               roughness, ao, depthNorm, ndotlMax,
                               lighting.lightCount);

    if (frame.debugMode == DEBUG_NONE) {
        color = vec3(1.0) - exp(-color * exposure);
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    }

    outColor = vec4(color, frame.debugMode == DEBUG_NONE ? baseSam.a : 1.0);
}
