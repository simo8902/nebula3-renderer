// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_CAMERA_H
#define NDEVC_CAMERA_H

#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "NDEVcHeaders.h"

class Camera {
    enum class CameraMovement {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };
public:
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
        m_yaw = glm::degrees(atan2(m_forwardVec.z, m_forwardVec.x));
        m_pitch = glm::degrees(asin(m_forwardVec.y));

        updateProjectionMatrix();
        updateVectors();
        updateViewMatrix();
    }

    glm::mat4 getViewMatrix() {
        return m_viewMatrix;
    }

    const glm::mat4& getProjectionMatrix() const {
        return m_ProjectionMatrix;
    }

    glm::vec3 getForward() const {
        glm::vec3 dir;
        dir.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        dir.y = sin(glm::radians(m_pitch));
        dir.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        return glm::normalize(dir);
    }


    void processKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = m_movementSpeed * deltaTime;
        if (direction == CameraMovement::FORWARD)  m_position += m_forwardVec * velocity;
        if (direction == CameraMovement::BACKWARD) m_position -= m_forwardVec * velocity;
        if (direction == CameraMovement::LEFT)     m_position -= glm::normalize(glm::cross(m_forwardVec, m_upVec)) * velocity;
        if (direction == CameraMovement::RIGHT)    m_position += glm::normalize(glm::cross(m_forwardVec, m_upVec)) * velocity;
        if (direction == CameraMovement::UP)       m_position.y += velocity;
        if (direction == CameraMovement::DOWN)     m_position.y -= velocity;
        updateViewMatrix();
    }

    void processMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true) {
        xoffset *= m_mouseSensitivity;
        yoffset *= m_mouseSensitivity;

        m_yaw += xoffset;
        m_pitch += yoffset;

        if (constrainPitch) {
            if (m_pitch > 89.0f)  m_pitch = 89.0f;
            if (m_pitch < -89.0f) m_pitch = -89.0f;
        }
        updateVectors();
        updateViewMatrix();
    }

    void processMouseScroll(float yoffset) {
        m_fov -= yoffset;
        if (m_fov < 1.0f)  m_fov = 1.0f;
        if (m_fov > 45.0f) m_fov = 45.0f;
        updateProjectionMatrix();
    }

    void handleInput(GLFWwindow* window, float deltaTime) {
       // if (ImGui::GetIO().WantCaptureKeyboard) return;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) processKeyboard(CameraMovement::FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) processKeyboard(CameraMovement::BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) processKeyboard(CameraMovement::LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) processKeyboard(CameraMovement::RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) processKeyboard(CameraMovement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) processKeyboard(CameraMovement::DOWN, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) processKeyboard(CameraMovement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) processKeyboard(CameraMovement::DOWN, deltaTime);
    }

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
       // ImGuiIO& io = ImGui::GetIO();
       // if (io.WantCaptureMouse) return;

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS)
            return;

        Camera* cam = (Camera*)glfwGetWindowUserPointer(window);
        if (!cam) return;

        static float lastX = (float)xpos;
        static float lastY = (float)ypos;
        static bool first = true;

        if (first) {
            lastX = (float)xpos;
            lastY = (float)ypos;
            first = false;
            return;
        }

        float dx = (float)xpos - lastX;
        float dy = lastY - (float)ypos;

        lastX = (float)xpos;
        lastY = (float)ypos;

        cam->processMouseMovement(dx, dy);
    }

    struct Plane {
        glm::vec3 normal;
        float d;
    };

    struct Frustum {
        Plane planes[6]; // near, far, left, right, top, bottom
    };

    Frustum extractFrustum(const glm::mat4& viewProj) {
        Frustum f;
        const glm::mat4& m = viewProj;

        auto normPlane = [](glm::vec4 p) {
            float len = glm::length(glm::vec3(p));
            if (len <= 0.0f) return std::make_pair(glm::vec3(0.0f), 0.0f);
            return std::make_pair(glm::vec3(p) / len, p.w / len);
        };

        std::pair<glm::vec3, float> plane;

        // Extract rows from column-major matrix.
        glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

        plane = normPlane(row3 + row2); // near
        f.planes[0].normal = plane.first;
        f.planes[0].d = plane.second;

        plane = normPlane(row3 - row2); // far
        f.planes[1].normal = plane.first;
        f.planes[1].d = plane.second;

        plane = normPlane(row3 + row0); // left
        f.planes[2].normal = plane.first;
        f.planes[2].d = plane.second;

        plane = normPlane(row3 - row0); // right
        f.planes[3].normal = plane.first;
        f.planes[3].d = plane.second;

        plane = normPlane(row3 + row1); // bottom
        f.planes[4].normal = plane.first;
        f.planes[4].d = plane.second;

        plane = normPlane(row3 - row1); // top
        f.planes[5].normal = plane.first;
        f.planes[5].d = plane.second;

        return f;
    }
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
       // if (ImGui::GetIO().WantCaptureMouse) return;
        Camera* cam = reinterpret_cast<Camera*>(glfwGetWindowUserPointer(window));
        if (!cam) return;
        cam->processMouseScroll((float)yoffset);
    }

    float getYaw() const { return m_yaw; }
    float getPitch() const { return m_pitch; }

    glm::vec3 getPosition() const {
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

    void setPosition(const glm::vec3& position) {
        m_position = position;
        updateViewMatrix();
    }

    void lookAt(const glm::vec3& target) {
        glm::vec3 direction = glm::normalize(target - m_position);
        m_forwardVec = direction;

        m_pitch = glm::degrees(asin(m_forwardVec.y));
        m_yaw = glm::degrees(atan2(m_forwardVec.z, m_forwardVec.x));

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
    const glm::mat4& viewMatrix() const { return m_viewMatrix; }
private:
    void updateVectors() {
        glm::vec3 forward;
        forward.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        forward.y = sin(glm::radians(m_pitch));
        forward.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        m_forwardVec = glm::normalize(forward);

        right = glm::normalize(glm::cross(m_forwardVec, glm::vec3(0.0f, 1.0f, 0.0f)));
        m_upVec = glm::normalize(glm::cross(right, m_forwardVec));
    }
    void updateViewMatrix() {
        glm::vec3 target = m_position + m_forwardVec;
        m_viewMatrix = glm::lookAt(m_position, target, m_upVec);
    }

    void updateProjectionMatrix() {
        m_ProjectionMatrix = glm::perspective(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
    }

    std::string name;
    glm::mat4 m_ProjectionMatrix{};
    glm::mat4 m_viewMatrix{};
    glm::vec3 right{};
    glm::vec3 m_position;
    glm::vec3 m_forwardVec;
    glm::vec3 m_upVec;

    float m_yaw;
    float m_pitch;
    float m_movementSpeed;
    float m_mouseSensitivity;
    float m_fov;
    float m_nearPlane;
    float m_farPlane;
    float m_aspectRatio = 16.0f / 9.0f;
};

#endif //NDEVC_CAMERA_H
