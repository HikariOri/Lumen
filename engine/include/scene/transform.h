#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

namespace lumen {
    struct Transform {

        glm::vec3 position = glm::vec3(0.0F);
        // Euler angles in degrees: (yaw, pitch, roll) 或别的约定
        glm::vec3 rotation = glm::vec3(0.0F);
        glm::vec3 scale = glm::vec3(1.0F);

        // 如果要支持父 Transform，可以加一个指针 parent
        Transform *parent = nullptr;

        // 如果有 parent，就返回 local position = 相对于 parent 的 position
        glm::vec3 getLocalPosition() const {
            // 如果没有 parent，就直接返回
            if (!parent) {
                return position;
            }
            // 如果有 parent，则可以做逆变换：世界坐标 → 父空间
            glm::mat4 invParent = glm::inverse(parent->getLocalToWorldMatrix());
            glm::vec4 localPos = invParent * glm::vec4(position, 1.0f);
            return glm::vec3(localPos);
        }

        glm::vec3 getLocalRotation() const {
            if (!parent)
                return rotation;
            // 这里简单版：相减，如果父 rotation 没有缩放／shear／非欧拉复杂性
            return rotation - parent->rotation;
        }

        // 获取旋转矩阵（从本地欧拉角构建）
        glm::mat4 getRotationMatrix() const {
            // 假设 rotation.x = yaw (绕 Y 轴)，rotation.y = pitch (绕 X
            // 轴)，rotation.z = roll (绕 Z 轴)
            float yaw = glm::radians(rotation.x);
            float pitch = glm::radians(rotation.y);
            float roll = glm::radians(rotation.z);
            // 你之前用的是 eulerAngleYXZ，所以保持这个顺序：先 Yaw(Y),再
            // Pitch(X),再 Roll(Z)
            return glm::eulerAngleYXZ(yaw, pitch, roll);
        }

        glm::vec3 getForward() const {
            // 通常 forward 是 "物体面向的方向"，假设 +Z
            // 是前方，或你可以修改为别的 我们先定义 local forward 向量：
            const glm::vec4 localForward = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            glm::mat4 rot = getRotationMatrix();
            glm::vec3 f = glm::normalize(glm::vec3(rot * localForward));
            return f;
        }

        glm::vec3 getRight() const {
            // local right 通常是 +X 方向
            const glm::vec4 localRight = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            glm::mat4 rot = getRotationMatrix();
            glm::vec3 r = glm::normalize(glm::vec3(rot * localRight));
            return r;
        }

        glm::vec3 getUp() const {
            // local up 通常是 +Y
            const glm::vec4 localUp = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            glm::mat4 rot = getRotationMatrix();
            glm::vec3 u = glm::normalize(glm::vec3(rot * localUp));
            return u;
        }

        glm::mat4 getLocalToWorldMatrix() const {
            // 构建模型矩阵：translate * rotate * scale
            glm::mat4 m = glm::mat4(1.0f);
            // 如果有父对象，要先父 local → world
            if (parent) {
                m = parent->getLocalToWorldMatrix();
            }

            m = glm::translate(m, position);
            m *= getRotationMatrix(); // 应用旋转
            m = glm::scale(m, scale);
            return m;
        }

        glm::mat4 getWorldToLocalMatrix() const {
            // 逆变换：逆 scale, 逆 rotation, 逆 translate, 以及父的逆
            glm::mat4 m = glm::mat4(1.0f);
            if (parent) {
                // 如果有父，也要把世界→父空间
                m = glm::inverse(parent->getLocalToWorldMatrix());
            }
            // 再把自己逆变换
            // 注意 scale 分量可能包含零或负，要小心
            m = glm::scale(m, glm::vec3(1.0f) / scale);
            glm::mat4 rot = getRotationMatrix();
            m *= glm::inverse(rot);
            m = glm::translate(m, -position);
            return m;
        }

        void lookAt(const glm::vec3 &target,
                    const glm::vec3 &up = glm::vec3(0.0f, 1.0f, 0.0f)) {
            // 让 transform 面向 target 点
            glm::mat4 m = glm::lookAt(position, target, up);
            // glm::lookAt 创建的是视图矩阵（camera
            // view），我们需要其逆来获取模型的朝向
            glm::mat4 inv = glm::inverse(m);
            // 从模型矩阵中提取 rotation 部分转成欧拉角
            glm::quat q = glm::quat_cast(inv);
            glm::vec3 euler = glm::eulerAngles(q); // 返回弧度
            // 转成度
            euler = glm::degrees(euler);
            rotation = euler;
        }

        void translate(const glm::vec3 &delta) { position += delta; }

        // deltaAngles: 欧拉角增量（度数）
        void rotate(const glm::vec3 &deltaAngles) { rotation += deltaAngles; }

        void setScale(const glm::vec3 &newScale) { scale = newScale; }
    };
} // namespace lumen
