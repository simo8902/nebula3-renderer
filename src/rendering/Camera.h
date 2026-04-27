// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_CAMERA_H
#define NDEVC_CAMERA_H

#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "Core/Logger.h"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "Platform/NDEVcHeaders.h"

class Camera {
public:
    enum class CameraMovement {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };
    Camera(std::string  name, glm::vec3 position, glm::vec3 forward, glm::vec3 up,
        float yaw, float pitch, float moveSpeed,
        float mouseSensitivity, float fov, float nearPlane, float farPlane)
        : m_position(position),
        m_forwardVec(forward),
        m_upVec(up),
        name(std::move(name)),
        m_yaw(yaw),
        m_pitch(pitch),
        m_movementSpeed(moveSpeed),
        m_mouseSensitivity(mouseSensitivity),
        m_fov(fov),
        m_nearPlane(nearPlane),
        m_farPlane(farPlane)
    {
        // If forward is provided but doesn't match yaw/pitch, we should decide which to trust.
        // Usually constructors like this trust yaw/pitch and derive forward, OR trust forward and derive yaw/pitch.
        // Given existing code derived yaw/pitch from forward:
        m_yaw = glm::degrees(atan2(m_forwardVec.z, m_forwardVec.x));
        m_pitch = glm::degrees(asin(std::clamp((double)m_forwardVec.y, -1.0, 1.0)));

        updateProjectionMatrix();
        updateVectors();
        updateViewMatrix();
    }

    glm::mat4 getViewMatrix() const {
        return m_viewMatrix;
    }

    const glm::mat4& getProjectionMatrix() const {
        return m_ProjectionMatrix;
    }

    glm::dvec3 getForward() const {
        return m_forwardVec;
    }


    void processKeyboard(CameraMovement direction, float deltaTime, float speedMultiplier = 1.0f) {
        double velocity = (double)m_movementSpeed * (double)deltaTime * (double)speedMultiplier;
        bool moved = false;
        if (direction == CameraMovement::FORWARD)  { m_position += m_forwardVec * velocity; moved = true; }
        if (direction == CameraMovement::BACKWARD) { m_position -= m_forwardVec * velocity; moved = true; }
        if (direction == CameraMovement::LEFT)     { m_position -= right * velocity; moved = true; }
        if (direction == CameraMovement::RIGHT)    { m_position += right * velocity; moved = true; }
        if (direction == CameraMovement::UP)       { m_position.y += velocity; moved = true; }
        if (direction == CameraMovement::DOWN)     { m_position.y -= velocity; moved = true; }

        if (moved) {
            updateViewMatrix();
        }
    }

    void processMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true) {
        double dx = (double)xoffset * m_mouseSensitivity;
        double dy = (double)yoffset * m_mouseSensitivity;

        m_yaw += dx;
        m_pitch += dy;

        // Wrap yaw to maintain precision in trig functions
        if (m_yaw > 360.0) m_yaw -= 360.0;
        if (m_yaw < 0.0) m_yaw += 360.0;

        if (constrainPitch) {
            if (m_pitch > 89.0)  m_pitch = 89.0;
            if (m_pitch < -89.0) m_pitch = -89.0;
        }

        updateVectors();
        updateViewMatrix();
    }

    void processMouseScroll(float yoffset) {
        if (std::abs(yoffset) < 0.01f) return;
        m_fov -= yoffset;
        if (m_fov < 1.0f)  m_fov = 1.0f;
        if (m_fov > 45.0f) m_fov = 45.0f;
        updateProjectionMatrix();
    }

    double getYaw() const { return m_yaw; }
    double getPitch() const { return m_pitch; }

    glm::dvec3 getPosition() const {
        return m_position;
    }

    void updateAspectRatio(int width, int height) {
        if (height > 0) {
            m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            updateProjectionMatrix();
        }
    }

    float getAspectRatio() const {
        return m_aspectRatio;
    }

    void setPosition(const glm::dvec3& position) {
        m_position = position;
        updateViewMatrix();
    }

    void lookAt(const glm::vec3& target) {
        glm::dvec3 targetD(target);
        glm::dvec3 direction = glm::normalize(targetD - m_position);

        m_pitch = glm::degrees(asin(direction.y));
        m_yaw = glm::degrees(atan2(direction.z, direction.x));
        updateVectors();
        updateViewMatrix();
    }

    void setPerspective(float fovDeg, float aspect, float nearPlane, float farPlane) {
        m_fov = fovDeg;
        m_aspectRatio = aspect;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        updateProjectionMatrix();
    }

    float getFov() const { return m_fov; }
    void setFov(float fovDeg) { m_fov = fovDeg; updateProjectionMatrix(); }

    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane()  const { return m_farPlane; }
    void setNearPlane(float v) { m_nearPlane = v; updateProjectionMatrix(); }
    void setFarPlane(float v) { m_farPlane = v; updateProjectionMatrix(); }

    void setAspectRatio(float aspect) { m_aspectRatio = aspect; updateProjectionMatrix(); }
    glm::vec4 getPerspective() const { return glm::vec4(m_fov, m_aspectRatio, m_nearPlane, m_farPlane); }
    const glm::mat4& viewMatrix() const {
        return m_viewMatrix;
    }
private:
    static glm::mat4 BuildReversedZPerspective(float fovRadians, float aspect, float nearPlane, float farPlane) {
        const float safeAspect = aspect > 0.0f ? aspect : 1.0f;
        const float safeNear = std::max(nearPlane, 0.0001f);
        const float f = 1.0f / tan(fovRadians * 0.5f);

        glm::mat4 proj(0.0f);
        proj[0][0] = f / safeAspect;
        proj[1][1] = f;
        
        if (farPlane <= 0.0f) { // Infinite Reversed Z
            proj[2][2] = 0.0f;
            proj[2][3] = -1.0f;
            proj[3][2] = safeNear;
        } else { // Finite Reversed Z
            const float safeFar = std::max(farPlane, safeNear + 0.0001f);
            proj[2][2] = safeNear / (safeFar - safeNear);
            proj[2][3] = -1.0f;
            proj[3][2] = (safeFar * safeNear) / (safeFar - safeNear);
        }
        return proj;
    }

    void updateVectors() {
        const double radYaw = glm::radians(m_yaw);
        const double radPitch = glm::radians(m_pitch);

        glm::dvec3 forward;
        forward.x = cos(radYaw) * cos(radPitch);
        forward.y = sin(radPitch);
        forward.z = sin(radYaw) * cos(radPitch);
        m_forwardVec = glm::normalize(forward);

        // Calculate orthonormal basis in double precision to prevent drift
        const glm::dvec3 upWorld(0.0, 1.0, 0.0);
        right = glm::normalize(glm::cross(m_forwardVec, upWorld));
        m_upVec = glm::normalize(glm::cross(right, m_forwardVec));
    }
    void updateViewMatrix() {
        // Build view matrix using pure double precision dot products
        m_viewMatrix = glm::mat4(
            glm::vec4(static_cast<float>(right.x), static_cast<float>(m_upVec.x), static_cast<float>(-m_forwardVec.x), 0.0f),
            glm::vec4(static_cast<float>(right.y), static_cast<float>(m_upVec.y), static_cast<float>(-m_forwardVec.y), 0.0f),
            glm::vec4(static_cast<float>(right.z), static_cast<float>(m_upVec.z), static_cast<float>(-m_forwardVec.z), 0.0f),
            glm::vec4(static_cast<float>(-glm::dot(right, m_position)),
                      static_cast<float>(-glm::dot(m_upVec, m_position)),
                      static_cast<float>(glm::dot(m_forwardVec, m_position)), 1.0f)
        );
    }

    void updateProjectionMatrix() {
        m_ProjectionMatrix = BuildReversedZPerspective(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
    }

    std::string name;
    glm::mat4 m_ProjectionMatrix{};
    glm::mat4 m_viewMatrix{};
    glm::dvec3 right{};
    glm::dvec3 m_position;
    glm::dvec3 m_forwardVec;
    glm::dvec3 m_upVec;

    double m_yaw;
    double m_pitch;
    float m_movementSpeed;
    double m_mouseSensitivity;
    float m_fov;
    float m_nearPlane;
    float m_farPlane;
    float m_aspectRatio = 16.0f / 9.0f;
};

#endif //NDEVC_CAMERA_H
