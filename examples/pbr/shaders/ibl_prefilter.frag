#version 450

layout(location = 0) in vec3 vLocalDir;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(push_constant) uniform Push {
    mat4 projView;
    vec4 rough_pad;
} pc;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

float distributionGGX(vec3 N, vec3 H, float r) {
    float a = r * r;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 xi, vec3 N, float r) {
    float a = r * r;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 Ht = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    vec3 sx = tangent * Ht.x + bitangent * Ht.y + N * Ht.z;
    return normalize(sx);
}

void main() {
    vec3 N = normalize(vLocalDir);
    vec3 R = N;
    vec3 V = R;
    float r = clamp(pc.rough_pad.x, 0.04, 1.0);
    vec3 prefiltered = vec3(0.0);
    float weight = 0.0;
    const uint SAMPLE_COUNT = 128u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(xi, N, r);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 1e-5);
            float D = distributionGGX(N, H, r);
            float pdf =
                (D * NdotH / (4.0 * HdotV)) + 1e-5;
            float saTexel = 4.0 * PI / (6.0 * 512.0 * 512.0);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 1e-5);
            float mip =
                0.5 * log2(saSample / saTexel + 1.0);
            prefiltered += textureLod(envMap, L, mip).rgb * NdotL;
            weight += NdotL;
        }
    }
    prefiltered /= max(weight, 1e-5);
    outColor = vec4(prefiltered, 1.0);
}
