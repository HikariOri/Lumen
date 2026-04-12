#pragma once

namespace renderer::ubo {

// 建议：每帧一个，用 ring buffer 管理
struct FrameUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;

    glm::vec3 cameraPos;
    float time {};

    glm::vec2 sceenSize;

    glm::vec4 exposureIblMips;

    int debugMode;
};

// 每 Object 一个，用 ring buffer 管理
struct ObjectUBO {
    glm::mat4 model;
    glm::mat4 normalMatrix;
};

} // namespace renderer::ubo
