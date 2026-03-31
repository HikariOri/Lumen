#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraWorld;
    vec4 envParams;
    vec4 pointLightParams;
    PointLight pointLights[4];
} scene;

layout(push_constant) uniform Push {
    mat4 model;
    /// rgb：glTF emissiveFactor；w：全局自发光倍率（ImGui）
    vec4 emissiveMul;
    vec4 baseColorFactor;
    /// xyz：金属度/粗糙度/AO 与贴图采样值的乘子（glTF factor）
    vec4 mrAoFactors;
    uint materialFlags;
    int debugView;
    int _pad[2];
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vUv;
layout(location = 2) out mat3 vTbn;
layout(location = 5) out vec3 vGeomNormal;

void main() {
    vec4 wp = pc.model * vec4(inPos, 1.0);
    vWorldPos = wp.xyz;
    vUv = inUv;
    mat3 nm = mat3(transpose(inverse(pc.model)));
    vec3 T = normalize(nm * inTangent.xyz);
    vec3 N = normalize(nm * inNormal);
    vGeomNormal = N;
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * inTangent.w;
    vTbn = mat3(T, B, N);
    gl_Position = scene.proj * scene.view * wp;
}
