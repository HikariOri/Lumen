#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragUV;

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

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    mat3 normalMat = mat3(transpose(inverse(ubo.model)));
    fragWorldNormal = normalMat * inNormal;
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragUV = inUV;
}
