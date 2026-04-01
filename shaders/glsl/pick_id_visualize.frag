#version 450

layout(set = 0, binding = 0) uniform usampler2D idMap;

layout(location = 0) out vec4 outColor;

vec3 id_to_color(uint id) {
    if (id == 0u) {
        // sRGB 附件上过暗会看起来像纯黑，略提亮背景
        return vec3(0.22, 0.22, 0.28);
    }
    uint h = id * 747796405u + 2891336453u;
    h = (h ^ (h >> 16)) * 2246822519u;
    h ^= h >> 13;
    vec3 c = vec3(float(h & 255u), float((h >> 8) & 255u),
                  float((h >> 16) & 255u)) /
             255.0;
    return clamp(c * 0.85 + 0.12, vec3(0.0), vec3(1.0));
}

void main() {
    // 与 Pick 使用相同 viewport 尺寸时，用像素坐标 1:1 取 texel，避免插值 vUv
    // 无法覆盖整张 UINT 纹理导致的错误采样
    ivec2 sz = textureSize(idMap, 0);
    ivec2 fc = ivec2(gl_FragCoord.xy);
    ivec2 tc = clamp(fc, ivec2(0), max(sz - ivec2(1), ivec2(0)));
    uint id = texelFetch(idMap, tc, 0).r;
    outColor = vec4(id_to_color(id), 1.0);
}
