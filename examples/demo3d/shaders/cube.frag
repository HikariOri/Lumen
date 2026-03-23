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
    vec4 envParams;
} scene;

layout(set = 0, binding = 1) uniform samplerCube envMap;
layout(set = 0, binding = 2) uniform sampler2D brdfLUT;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 mrAoFactors;
    vec4 emissiveFactor;
} matUbo;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

const int kMaxLights = 8;
const float PI = 3.14159265359;

float schlick_fresnel_u(float u) {
    float m = clamp(1.0 - u, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

vec3 fresnel_schlick_roughness(float cos_theta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
                    pow(1.0 - cos_theta, 5.0);
}

float distribution_ggx(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 1e-7);
}

float v_smith_ggx_height_correlated(float NdotL, float NdotV, float alpha) {
    float lambdaV = NdotL * (NdotV * (1.0 - alpha) + alpha);
    float lambdaL = NdotV * (NdotL * (1.0 - alpha) + alpha);
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

vec3 disney_diffuse_term(vec3 baseColor, float NdotL, float NdotV, float VoH,
                         float roughness) {
    float FL = schlick_fresnel_u(NdotL);
    float FV = schlick_fresnel_u(NdotV);
    float Fd90 = 0.5 + 2.0 * roughness * VoH * VoH;
    float fd = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);
    return baseColor * fd * (1.0 / PI);
}

float spotFactor(vec3 lightPos, vec3 worldPos, vec3 spotDir, float cosOuter,
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

vec3 perturb_normal(vec3 N, vec3 worldPos, vec3 V, vec2 uv, vec3 map) {
    map = map * 2.0 - 1.0;
    map.y = -map.y;
    mat3 TBN = cotangent_frame(N, worldPos, uv);
    return normalize(TBN * map);
}

void accum_light_pbr(GPULight L, vec3 N, vec3 V, vec3 albedo, float metallic,
                     float roughness, out vec3 Lo) {
    float kind = L.position.w;
    vec3 radiance = L.color.rgb * L.color.w;
    vec3 Ll;
    float atten = 1.0;

    if (kind < 0.5) {
        Ll = normalize(L.direction.xyz);
    } else {
        vec3 toLight = L.position.xyz - fragWorldPos;
        float dist = length(toLight);
        if (dist < 1e-5)
            return;
        Ll = toLight / dist;
        float maxR = L.params.x;
        if (dist > maxR)
            return;
        atten = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        float fade = 1.0 - smoothstep(maxR * 0.85, maxR, dist);
        atten *= fade;
        if (kind > 1.5) {
            vec3 spotDir = normalize(L.direction.xyz);
            atten *= spotFactor(L.position.xyz, fragWorldPos, spotDir,
                                L.params.y, L.params.z);
        }
    }

    float NdotL = max(dot(N, Ll), 0.0);
    if (NdotL <= 0.0)
        return;

    vec3 H = normalize(V + Ll);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnel_schlick_roughness(VdotH, F0, roughness);
    float alpha = roughness * roughness;
    float D = distribution_ggx(NdotH, roughness);
    float Vis = v_smith_ggx_height_correlated(NdotL, NdotV, alpha);
    vec3 specular = D * Vis * F;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse =
        kD * disney_diffuse_term(albedo, NdotL, NdotV, VdotH, roughness);

    Lo += (diffuse + specular) * radiance * atten * NdotL;
}

void main() {
    if (pc.mode == 1u) {
        outColor = vec4(pc.modelColor.rgb, 1.0);
        return;
    }
    if (pc.mode == 2u) {
        vec3 n = normalize(fragNormal);
        outColor = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }
    if (pc.mode == 3u) {
        float d = gl_FragCoord.z;
        outColor = vec4(vec3(d), 1.0);
        return;
    }

    vec3 V = normalize(scene.cameraWorld.xyz - fragWorldPos);
    vec3 N0 = normalize(fragNormal);
    vec3 tn = texture(normalMap, fragUV).rgb;
    vec3 N = perturb_normal(N0, fragWorldPos, V, fragUV, tn);

    float exposure = max(scene.envParams.x, 0.0);
    float maxMip = max(scene.envParams.y, 0.0);
    float diffMip = max(scene.envParams.z, 0.0);
    float iblStrength = max(scene.envParams.w, 0.0);

    vec4 texColor = texture(albedoMap, fragUV);
    vec3 albedo = texColor.rgb * matUbo.baseColorFactor.rgb * pc.modelColor.rgb;

    vec4 mrSample = texture(metallicRoughnessMap, fragUV);
    float metallic = clamp(mrSample.b * matUbo.mrAoFactors.x, 0.0, 1.0);
    float roughness =
        clamp(mrSample.g * matUbo.mrAoFactors.y, 0.04, 1.0);
    float ao = clamp(texture(aoMap, fragUV).r * matUbo.mrAoFactors.z, 0.0, 1.0);

    int nLights = int(scene.sceneParams.x + 0.5);
    nLights = min(nLights, kMaxLights);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < nLights; ++i) {
        accum_light_pbr(scene.lights[i], N, V, albedo, metallic, roughness, Lo);
    }

    float NdotV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F_ibl = fresnel_schlick_roughness(NdotV, F0, roughness);
    vec3 kS = F_ibl;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 irradiance = textureLod(envMap, N, diffMip).rgb * exposure;
    vec3 diffuse_ibl = irradiance * albedo * kD * (1.0 / PI) * ao;

    vec3 R = reflect(-V, N);
    float lod = roughness * maxMip;
    vec3 prefiltered = textureLod(envMap, R, lod).rgb * exposure;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 spec_ibl = prefiltered * (F0 * brdf.x + brdf.y) * ao;

    vec3 ambient = (diffuse_ibl + spec_ibl) * iblStrength;
    vec3 emissive =
        texture(emissiveMap, fragUV).rgb * matUbo.emissiveFactor.rgb;
    vec3 color = ambient + Lo + emissive;
    outColor = vec4(color, texColor.a * pc.modelColor.a * matUbo.baseColorFactor.a);
}
