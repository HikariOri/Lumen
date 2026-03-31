#version 450

// `note/forward_shader.md`：Frame / Material / Object / Lighting + IBL + exp 色调 + gamma

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
} frame;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 emissive;
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

layout(push_constant) uniform Push {
    int debugView;
    int _pad[3];
} pc;

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2

const float k_pi = 3.14159265358979323846;

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
                   out vec3 specOut) {
    diffuseOut = vec3(0.0);
    specOut = vec3(0.0);

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

void main() {
    if (pc.debugView == 1) {
        vec3 g = normalize(vGeomNormal);
        outColor = vec4(g * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
    vec3 mapN = texture(normalTex, vUv).rgb * 2.0 - 1.0;
    vec3 N = normalize(vTbn * mapN);

    if (pc.debugView == 2) {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

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

    if (pc.debugView == 3) {
        outColor = vec4(max(albedo, vec3(0.0)), 1.0);
        return;
    }
    if (pc.debugView == 15) {
        outColor = vec4(max(baseSam.rgb, vec3(0.0)), baseSam.a);
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

    vec3 ibl = (kD * diffuseIbl + specIbl) * ao * iblStrength;

    if (pc.debugView == 9) {
        vec3 c = vec3(1.0) - exp(-irradiance * exposure);
        c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
        outColor = vec4(c, 1.0);
        return;
    }
    if (pc.debugView == 10) {
        vec3 c = vec3(1.0) - exp(-prefiltered * exposure);
        c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
        outColor = vec4(c, 1.0);
        return;
    }
    if (pc.debugView == 11) {
        outColor = vec4(vec3(NdotV), 1.0);
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
        vec3 c = vec3(1.0) - exp(-emissive * exposure);
        c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
        outColor = vec4(c, 1.0);
        return;
    }

    if (pc.debugView == 7) {
        vec3 c =
            vec3(1.0) - exp(-kD * diffuseIbl * ao * iblStrength * exposure);
        c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
        outColor = vec4(c, 1.0);
        return;
    }
    if (pc.debugView == 8) {
        vec3 c = vec3(1.0) - exp(-specIbl * ao * iblStrength * exposure);
        c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
        outColor = vec4(c, 1.0);
        return;
    }

    vec3 directDiff = vec3(0.0);
    vec3 directSpec = vec3(0.0);
    if (pc.debugView == 0) {
        int n = clamp(lighting.lightCount, 0, MAX_LIGHTS);
        for (int i = 0; i < n; ++i) {
            vec3 dd;
            vec3 ds;
            evalLightBrdf(lighting.lights[i], N, V, vWorldPos, roughness,
                          albedo, metallic, F0, dd, ds);
            directDiff += dd;
            directSpec += ds;
        }
    }

    vec3 colorHdr = directDiff + directSpec + ibl + emissive;
    vec3 mapped = vec3(1.0) - exp(-colorHdr * exposure);
    vec3 finalRgb = pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(finalRgb, baseSam.a);
}
