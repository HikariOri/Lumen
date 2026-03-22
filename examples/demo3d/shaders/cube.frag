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

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 mvp;
    mat3 normalMatrix;
    vec4 cameraWorld;
    GPULight lights[8];
    vec4 sceneParams;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

const int kMaxLights = 8;
const float kShininess = 48.0;
const float kKd = 0.85;
const float kKs = 0.35;

float spotFactor(vec3 lightPos, vec3 worldPos, vec3 spotDir, float cosOuter,
                 float cosInner) {
    vec3 l2f = normalize(worldPos - lightPos);
    float theta = dot(l2f, spotDir);
    float denom = max(cosInner - cosOuter, 1e-5);
    return clamp((theta - cosOuter) / denom, 0.0, 1.0);
}

void accumLight(GPULight L, vec3 N, vec3 V, out vec3 diffAcc, out vec3 specAcc) {
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
            atten *= spotFactor(L.position.xyz, fragWorldPos, spotDir, L.params.y,
                                L.params.z);
        }
    }

    float NdotL = max(dot(N, Ll), 0.0);
    vec3 H = normalize(V + Ll);
    float NdotH = max(dot(N, H), 0.0);
    float specPow = pow(NdotH, kShininess);

    diffAcc += radiance * atten * NdotL * kKd;
    specAcc += radiance * atten * specPow * kKs;
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

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraWorld.xyz - fragWorldPos);

    int nLights = int(ubo.sceneParams.x + 0.5);
    nLights = min(nLights, kMaxLights);

    float ambient = 0.12 + 0.08 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 diff = vec3(0.0);
    vec3 spec = vec3(0.0);
    for (int i = 0; i < nLights; ++i) {
        vec3 dAdd = vec3(0.0);
        vec3 sAdd = vec3(0.0);
        accumLight(ubo.lights[i], N, V, dAdd, sAdd);
        diff += dAdd;
        spec += sAdd;
    }

    vec3 lighting = vec3(ambient) + diff + spec;
    vec4 texColor = texture(texSampler, fragUV);
    outColor = vec4(texColor.rgb * lighting * pc.modelColor.rgb,
                   texColor.a * pc.modelColor.a);
}
