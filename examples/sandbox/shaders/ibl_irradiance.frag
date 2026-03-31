#version 450

layout(location = 0) in vec3 vLocalDir;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

// Van der Corput 基-2 逆序 → Hammersley 低差异序列（与 ibl_prefilter 一致）
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

// 余弦加权半球：pdf = cos(θ)/π，估计 ∫ L(ω) cosθ dω 时只用 (π/N)·Σ L(ω_i)
vec3 cosineSampleHemisphere(vec2 xi, vec3 N) {
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    vec3 hl =
        vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * hl.x + bitangent * hl.y + N * hl.z);
}

void main() {
    vec3 N = normalize(vLocalDir);
    vec3 irradiance = vec3(0.0);
    const uint SAMPLE_COUNT = 256u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 L = cosineSampleHemisphere(xi, N);
        irradiance += textureLod(envMap, L, 0.0).rgb;
    }
    irradiance = PI * irradiance / float(SAMPLE_COUNT);
    outColor = vec4(irradiance, 1.0);
}
