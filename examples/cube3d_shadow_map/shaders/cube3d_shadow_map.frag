#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

const int MAX_LIGHTS = 8;

struct GpuLight {
    vec4 param0;
    vec4 color_intensity;
    vec4 spot_axis_inv_range;
    vec4 spot_cos_inner_outer;
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraWorld;
    vec4 materialParams;
    ivec4 lightCount_;
    GpuLight lights[MAX_LIGHTS];
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    vec3 N = normalize(fragWorldNormal);
    vec3 albedo = texture(texSampler, fragUV).rgb;

    float ka = ubo.materialParams.x;
    float kd = ubo.materialParams.y;
    float ks = ubo.materialParams.z;
    float shininess = ubo.materialParams.w;

    vec3 V = normalize(ubo.cameraWorld.xyz - fragWorldPos);

    vec3 ambient = ka * albedo * vec3(0.12, 0.13, 0.15);
    vec3 sumDiffuse = vec3(0.0);
    vec3 sumSpecular = vec3(0.0);

    int n = ubo.lightCount_.x;
    if (n > MAX_LIGHTS) {
        n = MAX_LIGHTS;
    }

    for (int i = 0; i < n; ++i) {
        GpuLight Lg = ubo.lights[i];
        float kind = Lg.param0.w;
        vec3 lc = Lg.color_intensity.rgb;

        vec3 Lvec;
        float dist = 0.0;
        vec3 L;

        if (kind < 0.5) {
            L = normalize(Lg.param0.xyz);
            dist = 0.0;
        } else if (kind < 1.5) {
            Lvec = Lg.param0.xyz - fragWorldPos;
            dist = length(Lvec);
            if (dist < 1e-4) {
                continue;
            }
            L = Lvec / dist;
        } else {
            Lvec = Lg.param0.xyz - fragWorldPos;
            dist = length(Lvec);
            if (dist < 1e-4) {
                continue;
            }
            L = Lvec / dist;
        }

        float atten = 1.0;
        float invR = Lg.spot_axis_inv_range.w;
        if (kind > 0.5) {
            atten = 1.0 / (1.0 + 0.18 * dist + 0.12 * dist * dist);
            if (invR > 1e-5) {
                atten *= clamp(1.0 - dist * invR, 0.0, 1.0);
            }
        }

        if (kind > 1.5) {
            vec3 spotAxis = normalize(Lg.spot_axis_inv_range.xyz);
            vec3 toFrag = normalize(fragWorldPos - Lg.param0.xyz);
            float c = dot(toFrag, spotAxis);
            float cOut = Lg.spot_cos_inner_outer.x;
            float cIn = Lg.spot_cos_inner_outer.y;
            float spotF = smoothstep(cOut, cIn, c);
            atten *= spotF;
        }

        vec3 H = normalize(L + V);
        float ndl = max(dot(N, L), 0.0);
        float nh = max(dot(N, H), 0.0);

        sumDiffuse += kd * ndl * albedo * lc * atten;
        sumSpecular += ks * pow(nh, shininess) * lc * atten;
    }

    outColor = vec4(ambient + sumDiffuse + sumSpecular, 1.0);
}
