#version 450

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    float time;
};

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    vec4 texColor = texture(texSampler, fragUV);
    // 可选：随时间轻微呼吸效果
    float wave = 0.95 + 0.05 * sin(time * 2.0);
    outColor = vec4(texColor.rgb * wave, texColor.a);
}
