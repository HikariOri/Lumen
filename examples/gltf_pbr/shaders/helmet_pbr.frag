#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec2 vUv;
layout(location = 2) in mat3 vTbn;
layout(location = 5) in vec3 vGeomNormal;

layout(location = 0) out vec4 outColor;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraWorld;
    vec4 envParams;
    vec4 pointLightParams;
    PointLight pointLights[4];
} scene;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;
layout(set = 0, binding = 4) uniform sampler2D albedoMap;
layout(set = 0, binding = 5) uniform sampler2D normalMap;
layout(set = 0, binding = 6) uniform sampler2D metallicMap;
layout(set = 0, binding = 7) uniform sampler2D roughnessMap;
layout(set = 0, binding = 8) uniform sampler2D aoMap;
layout(set = 0, binding = 9) uniform sampler2D emissiveMap;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 emissiveMul;
    vec4 baseColorFactor;
    vec4 mrAoFactors;
    uint materialFlags;
    int debugView;
    int _pad[2];
} pc;

const uint kMatFlagGltfCombinedMr = 1u;

// albedoMap / emissiveMap 为 VK_FORMAT_*_SRGB：texture() 已返回线性 RGB，勿再手动 sRGB→linear。

const float k_pi = 3.14159265358979323846;

float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (k_pi * denom * denom + 1e-6);
}

float geometry_schlick_ggx(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float ggx1 = geometry_schlick_ggx(max(dot(N, V), 0.0), roughness);
    float ggx2 = geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
    return ggx1 * ggx2;
}

vec3 fresnel_schlick_direct(float cosTheta, vec3 F0) {
    float ct = clamp(cosTheta, 0.0, 1.0);
    return F0 + (1.0 - F0) * pow(1.0 - ct, 5.0);
}

vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float roughness) {
    float ct = clamp(cosTheta, 0.0, 1.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
                 * pow(1.0 - ct, 5.0);
}

vec3 reinhard_ldr(vec3 hdr) { return hdr / (hdr + vec3(1.0)); }

void main() {
    // 0 完整 PBR；1 几何法线；2 扰动法线（见下方）
    if (pc.debugView == 1) {
        vec3 g = normalize(vGeomNormal);
        outColor = vec4(g * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 V = normalize(scene.cameraWorld.xyz - vWorldPos);
    vec3 mapN = texture(normalMap, vUv).rgb * 2.0 - 1.0;
    vec3 N = normalize(vTbn * mapN);

    if (pc.debugView == 2) {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 albedo =
        texture(albedoMap, vUv).rgb * pc.baseColorFactor.rgb;
    float metallic;
    float roughness;
    if ((pc.materialFlags & kMatFlagGltfCombinedMr) != 0u) {
        vec4 mr = texture(metallicMap, vUv);
        metallic = clamp(mr.b * pc.mrAoFactors.x, 0.0, 1.0);
        roughness =
            clamp(max(mr.g * pc.mrAoFactors.y, 0.001), 0.04, 1.0);
    } else {
        metallic =
            clamp(texture(metallicMap, vUv).r * pc.mrAoFactors.x, 0.0, 1.0);
        roughness = clamp(
            max(texture(roughnessMap, vUv).r * pc.mrAoFactors.y, 0.001), 0.04,
            1.0);
    }
    float ao = clamp(texture(aoMap, vUv).r * pc.mrAoFactors.z, 0.0, 1.0);
    vec3 emissive = texture(emissiveMap, vUv).rgb * pc.emissiveMul.rgb
                    * pc.emissiveMul.w;

    float exposure = scene.envParams.x;
    float maxMip = scene.envParams.y;
    float diffMip = scene.envParams.z;
    float iblStrength = scene.envParams.w;

    // 3 反照率（线性写入；sRGB 颜色附件会做一次编码）
    if (pc.debugView == 3) {
        outColor = vec4(max(albedo, vec3(0.0)), 1.0);
        return;
    }
    // 15 Base Color（glTF：贴图 × baseColorFactor）
    if (pc.debugView == 15) {
        vec4 bc = texture(albedoMap, vUv) * pc.baseColorFactor;
        outColor = vec4(max(bc.rgb, vec3(0.0)), bc.a);
        return;
    }
    if (pc.debugView == 4) {
        outColor = vec4(vec3(metallic), 1.0);
        return;
    }
    if (pc.debugView == 5) {
        outColor = vec4(vec3(roughness), 1.0);
        return;
    }
    if (pc.debugView == 6) {
        outColor = vec4(vec3(ao), 1.0);
        return;
    }

    float NdotV = max(dot(N, V), 1e-4);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnel_schlick_roughness(NdotV, F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = textureLod(irradianceMap, N, diffMip).rgb * exposure;
    vec3 diffuse = irradiance * albedo;

    vec3 R = reflect(-V, N);
    float lod = roughness * maxMip;
    vec3 prefiltered = textureLod(prefilterMap, R, lod).rgb * exposure;
    vec2 brdf = texture(brdfLUT, vec2(clamp(dot(N, V), 0.0, 1.0), roughness)).rg;
    vec3 specIBL = prefiltered * (F0 * brdf.x + brdf.y);

    if (pc.debugView == 9) {
        vec3 c = reinhard_ldr(irradiance);
        outColor = vec4(max(c, vec3(0.0)), 1.0);
        return;
    }
    if (pc.debugView == 10) {
        vec3 c = reinhard_ldr(prefiltered);
        outColor = vec4(max(c, vec3(0.0)), 1.0);
        return;
    }
    if (pc.debugView == 11) {
        float g = clamp(dot(N, V), 0.0, 1.0);
        outColor = vec4(vec3(g), 1.0);
        return;
    }
    if (pc.debugView == 12) {
        outColor = vec4(F0, 1.0);
        return;
    }
    if (pc.debugView == 13) {
        outColor = vec4(brdf.x, brdf.y, 0.0, 1.0);
        return;
    }
    if (pc.debugView == 14) {
        vec3 c = reinhard_ldr(emissive);
        outColor = vec4(max(c, vec3(0.0)), 1.0);
        return;
    }

    vec3 ambient = (kD * diffuse + specIBL) * ao;

    if (pc.debugView == 7) {
        vec3 c = reinhard_ldr(kD * diffuse * ao * iblStrength);
        outColor = vec4(max(c, vec3(0.0)), 1.0);
        return;
    }
    if (pc.debugView == 8) {
        vec3 c = reinhard_ldr(specIBL * ao * iblStrength);
        outColor = vec4(max(c, vec3(0.0)), 1.0);
        return;
    }

    // 0：完整 PBR（IBL + 可选点光源）
    vec3 LoPoint = vec3(0.0);
    if (pc.debugView == 0) {
        int numLights = min(int(scene.pointLightParams.x + 0.5), 4);
        float gPoint = scene.pointLightParams.y;
        for (int li = 0; li < 4; ++li) {
            if (li >= numLights) {
                break;
            }
            vec3 Lp = scene.pointLights[li].positionRadius.xyz;
            float range = max(scene.pointLights[li].positionRadius.w, 0.05);
            vec3 Lvec = Lp - vWorldPos;
            float dist = length(Lvec);
            vec3 L = Lvec / max(dist, 1e-4);
            float NdotL = max(dot(N, L), 0.0);
            if (NdotL <= 0.0) {
                continue;
            }
            vec3 H = normalize(V + L);
            float attenuation =
                1.0 / max(dist * dist, 1e-4);
            float edge = clamp(1.0 - dist / range, 0.0, 1.0);
            attenuation *= edge * edge;
            vec3 radiance = scene.pointLights[li].colorIntensity.rgb
                            * scene.pointLights[li].colorIntensity.w
                            * attenuation * gPoint;
            float NDF = distribution_ggx(N, H, roughness);
            float G = geometry_smith(N, V, L, roughness);
            vec3 Fp = fresnel_schlick_direct(max(dot(H, V), 0.0), F0);
            vec3 specNum = NDF * G * Fp;
            float specDen = 4.0 * NdotV * NdotL + 1e-4;
            vec3 specPt = specNum / specDen;
            vec3 kDpt = (vec3(1.0) - Fp) * (1.0 - metallic);
            LoPoint += (kDpt * albedo / k_pi + specPt) * radiance * NdotL;
        }
    }

    vec3 color = ambient * iblStrength + emissive + LoPoint;
    vec3 mapped = reinhard_ldr(color);
    outColor = vec4(max(mapped, vec3(0.0)), 1.0);
}
