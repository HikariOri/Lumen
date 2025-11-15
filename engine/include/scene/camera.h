#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

enum class CameraProjectionType : std::uint8_t { Perspective, Orthographic };

struct Camera {
    CameraProjectionType projection = CameraProjectionType::Perspective;

    // 共同属性
    glm::vec3 position = glm::vec3(0.0f); // 相机的位置
    glm::vec3 rotation = glm::vec3(0.0f); // 欧拉角 (度) 或自定义顺序
                                          // 通常 rotation = (pitch, yaw, roll)
                                          // 或 (yaw, pitch, roll)，看你习惯

    // 投影参数 - Perspective
    float fovYDeg = 45.0f;            // 垂直视野角度 (度)
    float aspectRatio = 16.0f / 9.0f; // 宽 / 高
    float nearClip = 0.1f;
    float farClip = 100.0f;

    // 投影参数 - Orthographic
    float orthoWidth = 10.0f;  // 正交投影宽度（视口宽度在世界空间的大小）
    float orthoHeight = 10.0f; // 高度
    // 或者你只设置宽度和 aspect，就能计算高度；这里是同时设置两个以灵活控制

    // Up 向量（决定"上"的方向）
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    // ---------- 方法 ----------

    // 获取 View 矩阵：把世界坐标转换到相机（视点）坐标系
    glm::mat4 getViewMatrix() const {
        // 先构建移动 + 旋转矩阵
        glm::mat4 view = glm::mat4(1.0f);
        // 注意：顺序通常是先旋转再平移（因为 view 是摄像机从世界看出去）
        // 假设 rotation.x = yaw (绕 Y), rotation.y = pitch (绕 X), rotation.z =
        // roll (绕 Z) 先把欧拉角转成弧度
        float yaw = glm::radians(rotation.x);
        float pitch = glm::radians(rotation.y);
        float roll = glm::radians(rotation.z);

        // 构造旋转矩阵
        glm::mat4 rot = glm::eulerAngleYXZ(yaw, pitch, roll);

        // 视图矩阵 = 逆(rotation) * inverse(translation) = transpose(rot) *
        // translate(-position) 或者这么做：先旋转相机（相机的方向），再移动相机
        view = glm::inverse(rot) * glm::translate(glm::mat4(1.0f), -position);
        return view;
    }

    // 获取投影矩阵
    glm::mat4 getProjectionMatrix() const {
        if (projection == CameraProjectionType::Perspective) {
            return glm::perspective(glm::radians(fovYDeg), aspectRatio,
                                    nearClip, farClip);
        } else {
            // 正交投影
            // 这里设定的 orthoWidth / orthoHeight
            // 是视口在世界空间的宽高的一半（或全部？取决于你的设定）
            float halfW = orthoWidth * 0.5f;
            float halfH = orthoHeight * 0.5f;
            // glm::ortho(left, right, bottom, top, near, far)
            return glm::ortho(-halfW, +halfW, -halfH, +halfH, nearClip,
                              farClip);
        }
    }

    // 获取 ViewProjection 矩阵
    glm::mat4 getViewProjectionMatrix() const {
        return getProjectionMatrix() * getViewMatrix();
    }

    // 设置为透视投影
    void setPerspective(float fovYDegrees, float aspect, float nearC,
                        float farC) {
        projection = CameraProjectionType::Perspective;
        fovYDeg = fovYDegrees;
        aspectRatio = aspect;
        nearClip = nearC;
        farClip = farC;
    }

    // 设置为正交投影
    void setOrthographic(float width, float height, float nearC, float farC) {
        projection = CameraProjectionType::Orthographic;
        orthoWidth = width;
        orthoHeight = height;
        nearClip = nearC;
        farClip = farC;
    }

    // 例如 LookAt 方法
    void lookAt(const glm::vec3 &target,
                const glm::vec3 &upVec = glm::vec3(0.0f, 1.0f, 0.0f)) {
        // 使用 glm::lookAt 简化视图矩阵构造，然后从中提取旋转／位置
        glm::mat4 view = glm::lookAt(position, target, upVec);
        // view = R * T (分别旋转和平移)，我们要从 view invert 得到模型朝向
        glm::mat4 invView = glm::inverse(view);
        // 提取旋转成四元数
        glm::quat q = glm::quat_cast(invView);
        glm::vec3 euler = glm::eulerAngles(q);
        euler = glm::degrees(euler);
        rotation = glm::vec3(euler.x, euler.y, euler.z);
    }
};
