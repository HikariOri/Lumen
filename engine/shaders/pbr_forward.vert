#version 450

// `note/forward_shader.md`：Set0 Frame，Set2 Object

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 exposureIblMips;
} frame;

layout(set = 2, binding = 0) uniform ObjectUBO {
    mat4 model;
    mat4 normalMatrix;
} object;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vUv;
layout(location = 2) out mat3 vTbn;
layout(location = 5) out vec3 vGeomNormal;

void main() {
    vec4 wp = object.model * vec4(inPos, 1.0);
    vWorldPos = wp.xyz;
    vUv = inUv;
    mat3 nm = mat3(object.normalMatrix);
    vec3 T = normalize(nm * inTangent.xyz);
    vec3 N = normalize(nm * inNormal);
    vGeomNormal = N;
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * inTangent.w;
    vTbn = mat3(T, B, N);
    gl_Position = frame.viewProj * wp;
}
