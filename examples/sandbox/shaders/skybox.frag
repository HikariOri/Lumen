#version 450

layout(location = 0) in vec3 vWorldDir;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform SkyPC {
    mat4 sky_mvp;
    vec4 params;
} pc;

layout(set = 0, binding = 0) uniform samplerCube envMap;

void main() {
    vec3 dir = normalize(vWorldDir);
    float exposure = pc.params.x;
    vec3 c = textureLod(envMap, dir, 0.0).rgb * exposure;
    // Reinhard；*_SRGB Swapchain 下勿手写 gamma 2.2，避免双重编码
    c = c / (c + vec3(1.0));
    outColor = vec4(max(c, vec3(0.0)), 1.0);
}
