#version 450

// ID Map：Set0 Frame + Set1 Object（与 PBR 的 Set2 布局相同，便于复用 Frame/Object 描述集）

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
    int debugMode;
    int _framePad0;
    int _framePad1;
    int _framePad2;
} frame;

layout(set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    mat4 normalMatrix;
} object;

void main() {
    vec4 wp = object.model * vec4(inPos, 1.0);
    gl_Position = frame.viewProj * wp;
}
