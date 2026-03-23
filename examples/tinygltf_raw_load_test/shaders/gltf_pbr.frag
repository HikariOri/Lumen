#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormalWS;
layout(location = 2) in vec3 vPosWS;

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
} uScene;

/// GGX 预过滤辐射度（CPU 生成 mip 链，与 split-sum BRDF LUT 配套）
layout(set = 0, binding = 1) uniform samplerCube uRadianceEnv;
layout(set = 0, binding = 2) uniform sampler2D uBrdfLut;
/// 余弦加权积分得到的辐照度 E(N)=∫ L(ω)(N·ω)+ dω
layout(set = 0, binding = 3) uniform samplerCube uIrradianceEnv;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 mrAoFactors;
    vec4 emissiveFactor;
    vec4 shaderParams;
} uMat;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

const int kMaxLights = 8;
const float PI = 3.14159265359;

float distribution_ggx(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 1e-7);
}

/// Heitz 高度相关 Smith GGX 可见性项（与 `ibl_cpu::integrate_brdf` 所用形式一致）
float visibility_smith_ggx_height_correlated(float NdotL, float NdotV,
                                             float alpha) {
    float lambdaV = NdotL * (NdotV * (1.0 - alpha) + alpha);
    float lambdaL = NdotV * (NdotL * (1.0 - alpha) + alpha);
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

/// 直射光：经典 Schlick Fresnel（F0 + (1-F0)(1 - cosθ)^5）
vec3 fresnel_schlick_direct(float cosTheta, vec3 F0) {
    float x = clamp(1.0 - cosTheta, 0.0, 1.0);
    float x2 = x * x;
    float x5 = x2 * x2 * x;
    return F0 + (1.0 - F0) * x5;
}

/// IBL：粗糙度调制 Fresnel（Karis / glTF 参考实现常用形式）
vec3 fresnel_schlick_ibl(float NdotV, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
                    pow(1.0 - NdotV, 5.0);
}

float spot_attenuation(vec3 lightPos, vec3 worldPos, vec3 spotDir, float cosOuter,
                       float cosInner) {
    vec3 l2f = normalize(worldPos - lightPos);
    float theta = dot(l2f, spotDir);
    float denom = max(cosInner - cosOuter, 1e-5);
    return clamp((theta - cosOuter) / denom, 0.0, 1.0);
}

mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) < 1e-8)
        return mat3(vec3(1, 0, 0), vec3(0, 1, 0), N);
    float invdet = 1.0 / det;
    vec3 T = normalize((dp1 * duv2.y - dp2 * duv1.y) * invdet);
    vec3 B = normalize((-dp1 * duv2.x + dp2 * duv1.x) * invdet);
    return mat3(T, B, N);
}

vec3 perturb_normal(vec3 Ngeom, vec3 worldPos, vec3 V, vec2 uv, vec3 mapSample) {
    vec3 m = mapSample * 2.0 - 1.0;
    m.y = -m.y;
    mat3 TBN = cotangent_frame(Ngeom, worldPos, uv);
    return normalize(TBN * m);
}

vec3 srgb_to_linear_rgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.04045));
    vec3 a = c / 12.92;
    vec3 b = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(b, a, vec3(lo));
}

uint material_tex_mask() { return floatBitsToUint(uMat.shaderParams.z); }

/// Cook-Torrance 镜面项写作 D * V * F（V 已含分母中与 G 的组合，与引擎 BRDF LUT 一致）
void accumulate_direct_pbr(GPULight L, vec3 N, vec3 V, vec3 baseColor,
                           float metallic, float roughness, inout vec3 Lo) {
    float kind = L.position.w;
    vec3 radiance = L.color.rgb * L.color.w;
    vec3 Lvec;
    float atten = 1.0;

    if (kind < 0.5) {
        Lvec = normalize(L.direction.xyz);
    } else {
        vec3 toLight = L.position.xyz - vPosWS;
        float dist = length(toLight);
        if (dist < 1e-5)
            return;
        Lvec = toLight / dist;
        float maxR = L.params.x;
        if (dist > maxR)
            return;
        atten = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        float fade = 1.0 - smoothstep(maxR * 0.85, maxR, dist);
        atten *= fade;
        if (kind > 1.5) {
            vec3 spotDir = normalize(L.direction.xyz);
            atten *= spot_attenuation(L.position.xyz, vPosWS, spotDir,
                                      L.params.y, L.params.z);
        }
    }

    float NdotL = max(dot(N, Lvec), 0.0);
    if (NdotL <= 0.0)
        return;

    vec3 H = normalize(V + Lvec);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F = fresnel_schlick_direct(VdotH, F0);

    float alpha = roughness * roughness;
    float D = distribution_ggx(NdotH, roughness);
    float Vis = visibility_smith_ggx_height_correlated(NdotL, NdotV, alpha);
    vec3 specular = D * Vis * F;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * baseColor * (1.0 / PI);

    Lo += (diffuse + specular) * radiance * atten * NdotL;
}

void main() {
    vec3 V = normalize(uScene.cameraWorld.xyz - vPosWS);
    vec3 Ngeom = normalize(vNormalWS);
    uint tmask = material_tex_mask();

    vec3 baseColor;
    float baseAlpha = 1.0;
    if ((tmask & 1u) != 0u) {
        vec4 tc = texture(albedoMap, vUV);
        baseColor = srgb_to_linear_rgb(tc.rgb) * uMat.baseColorFactor.rgb;
        baseAlpha = tc.a;
    } else {
        baseColor = uMat.baseColorFactor.rgb;
        baseAlpha = uMat.baseColorFactor.a;
    }

    vec3 N;
    if ((tmask & 2u) != 0u) {
        vec3 tn = texture(normalMap, vUV).rgb;
        N = perturb_normal(Ngeom, vPosWS, V, vUV, tn);
    } else {
        N = Ngeom;
    }

    // glTF 2.0 metallic-roughness：G=roughness，B=metallic；贴图须 **UNORM** 线性采样
    float metallic;
    float roughness;
    if ((tmask & 4u) != 0u) {
        vec4 mrSample = texture(metallicRoughnessMap, vUV);
        if ((tmask & 32u) != 0u) {
            // KHR_materials_pbrSpecularGlossiness：粗糙度由光泽度推导（与引擎 mask 一致）
            metallic = clamp(uMat.mrAoFactors.x, 0.0, 1.0);
            float gloss = clamp(mrSample.a * uMat.mrAoFactors.y, 0.0, 1.0);
            roughness = clamp(1.0 - gloss, 0.04, 1.0);
        } else {
            metallic =
                clamp(mrSample.b * uMat.mrAoFactors.x, 0.0, 1.0);
            roughness =
                clamp(mrSample.g * uMat.mrAoFactors.y, 0.04, 1.0);
        }
    } else {
        metallic = clamp(uMat.mrAoFactors.x, 0.0, 1.0);
        roughness = clamp(uMat.mrAoFactors.y, 0.04, 1.0);
    }

    float ao;
    if ((tmask & 8u) != 0u) {
        ao = clamp(texture(aoMap, vUV).r * uMat.mrAoFactors.z, 0.0, 1.0);
    } else {
        ao = clamp(uMat.mrAoFactors.z, 0.0, 1.0);
    }

    vec3 emissive;
    if ((tmask & 16u) != 0u) {
        emissive = srgb_to_linear_rgb(texture(emissiveMap, vUV).rgb) *
                   uMat.emissiveFactor.rgb;
    } else {
        emissive = uMat.emissiveFactor.rgb;
    }

    float exposure = max(uScene.envParams.x, 0.0);
    float radianceMaxLod = max(uScene.envParams.y, 0.0);
    float iblStrength = max(uScene.envParams.w, 0.0);

    float NdotV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    vec3 Lo = vec3(0.0);
    int nLights = int(uScene.sceneParams.x + 0.5);
    nLights = min(nLights, kMaxLights);
    for (int i = 0; i < nLights; ++i) {
        accumulate_direct_pbr(uScene.lights[i], N, V, baseColor, metallic,
                            roughness, Lo);
    }

    vec3 F_ibl = fresnel_schlick_ibl(NdotV, F0, roughness);
    vec3 kS = F_ibl;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    // AO 对间接光用 sqrt：比线性 ao 抬亮深缝（Sponza 等重 AO 场景常见折中）
    float ao_indirect = sqrt(clamp(ao, 0.0, 1.0));

    vec3 irradiance = texture(uIrradianceEnv, N).rgb * exposure;
    vec3 diffuse_ibl = irradiance * kD * baseColor * (1.0 / PI) * ao_indirect;

    vec3 R = reflect(-V, N);
    float specLod = roughness * radianceMaxLod;
    vec3 prefiltered = textureLod(uRadianceEnv, R, specLod).rgb * exposure;
    vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 spec_ibl = prefiltered * (F0 * brdf.x + brdf.y) * ao_indirect;

    vec3 indirect = (diffuse_ibl + spec_ibl) * iblStrength;
    vec3 color = Lo + indirect + emissive;

    float alpha = baseAlpha * uMat.baseColorFactor.a;
    float amode = uMat.shaderParams.x;
    if (amode > 0.5 && amode < 1.5) {
        if (alpha < uMat.shaderParams.y)
            discard;
    }

    float outA = 1.0;
    if (amode > 1.5)
        outA = alpha;
    outColor = vec4(color, outA);
}
