#version 450

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    float time;
};

void main() {
    // 柔和呼吸感：在 0.7～1.0 之间微微波动，保持粉嫩
    float t = time * 1.5;
    float wave = 0.85 + 0.15 * sin(t);
    float r = fragColor.r * wave;
    float g = fragColor.g * (0.9 + 0.1 * sin(t + 2.094));
    float b = fragColor.b * (0.9 + 0.1 * sin(t + 4.188));
    outColor = vec4(r, g, b, fragColor.a);
}
