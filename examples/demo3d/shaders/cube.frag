#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) uint mode;       // 0=lit, 1=wireframe, 2=normal, 3=depth
    layout(offset = 16) vec4 modelColor;
} pc;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
    mat3 normalMatrix;
    vec4 light0;  // xyz=方向 w=强度
    vec4 light1;
    vec4 light2;
    vec4 light3;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    if (pc.mode == 1u) {
        // 线框模式：线为亮色
        outColor = vec4(pc.modelColor.rgb, 1.0);
        return;
    }
    if (pc.mode == 2u) {
        // 法线可视化：映射 [-1,1] -> [0,1] RGB
        vec3 n = normalize(fragNormal);
        outColor = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }
    if (pc.mode == 3u) {
        // 深度可视化：gl_FragCoord.z 为 [0,1]，近黑远白
        float d = gl_FragCoord.z;
        outColor = vec4(vec3(d), 1.0);
        return;
    }
    // mode 0: 多光源 + 纹理 + modelColor
    vec3 n = normalize(fragNormal);
    // 基础环境光 + 微弱半球项（朝上略亮），整体比纯定向光更均匀
    float ambient = 0.68 + 0.18 * clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    float diff = 0.0;
    diff += max(0.0, dot(n, normalize(ubo.light0.xyz))) * ubo.light0.w;
    diff += max(0.0, dot(n, normalize(ubo.light1.xyz))) * ubo.light1.w;
    diff += max(0.0, dot(n, normalize(ubo.light2.xyz))) * ubo.light2.w;
    diff += max(0.0, dot(n, normalize(ubo.light3.xyz))) * ubo.light3.w;
    float lighting = ambient + diff;
    vec4 texColor = texture(texSampler, fragUV);
    outColor = vec4(texColor.rgb * lighting * pc.modelColor.rgb,
                   texColor.a * pc.modelColor.a);
}
