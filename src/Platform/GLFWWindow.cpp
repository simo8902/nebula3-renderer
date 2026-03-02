// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Platform/GLFWWindow.h"
#include "GLFW/glfw3.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace NDEVC::Platform::GLFW {

std::unordered_map<GLFWwindow*, GLFWWindow*> g_windowMap;

static bool ReadEnvToggleWindow(const char* name) {
    if (!name || !name[0]) return false;
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return false;
    const bool enabled = value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
}

GLFWWindow::GLFWWindow(const std::string& title, int width, int height)
    : handle_(nullptr) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWmonitor* monitor = nullptr;
    if (ReadEnvToggleWindow("NDEVC_FULLSCREEN_EXCLUSIVE")) {
        monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                glfwWindowHint(GLFW_RED_BITS, mode->redBits);
                glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
                glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
                glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
                width = mode->width;
                height = mode->height;
                std::cout << "[WINDOW] Exclusive fullscreen " << width << "x" << height
                          << " @ " << mode->refreshRate << "Hz\n";
            }
        }
    }

    handle_ = glfwCreateWindow(width, height, title.c_str(), monitor, nullptr);
    if (!handle_) {
        throw std::runtime_error("GLFW window creation failed");
    }

    g_windowMap[handle_] = this;

    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(0);
}

GLFWWindow::~GLFWWindow() {
    if (handle_) {
        g_windowMap.erase(handle_);
        glfwDestroyWindow(handle_);
    }
}

void GLFWWindow::MakeCurrent() const {
    glfwMakeContextCurrent(handle_);
}

void GLFWWindow::SwapBuffers() {
    glfwSwapBuffers(handle_);
}

void GLFWWindow::PollEvents() {
    glfwPollEvents();
}

bool GLFWWindow::ShouldClose() const {
    return glfwWindowShouldClose(handle_) != 0;
}

void GLFWWindow::SetShouldClose(bool value) {
    glfwSetWindowShouldClose(handle_, value ? GLFW_TRUE : GLFW_FALSE);
}

void GLFWWindow::GetFramebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(handle_, &width, &height);
}

void GLFWWindow::SetFramebufferSizeCallback(std::function<void(int, int)> callback) {
    framebufferSizeCallback_ = callback;
    glfwSetFramebufferSizeCallback(handle_, FramebufferSizeCallback);
}

void GLFWWindow::SetScrollCallback(std::function<void(double, double)> callback) {
    scrollCallback_ = callback;
    glfwSetScrollCallback(handle_, ScrollCallback);
}

void* GLFWWindow::GetNativeHandle() const {
    return handle_;
}

int GLFWWindow::GetKey(int key) const {
    return glfwGetKey(handle_, key);
}

bool GLFWWindow::IsKeyPressed(int key) const {
    return glfwGetKey(handle_, key) == GLFW_PRESS;
}

void GLFWWindow::GetCursorPos(double& x, double& y) const {
    glfwGetCursorPos(handle_, &x, &y);
}

void GLFWWindow::SetCursorPos(double x, double y) {
    glfwSetCursorPos(handle_, x, y);
}

void GLFWWindow::SetInputMode(int mode, int value) {
    glfwSetInputMode(handle_, mode, value);
}

void GLFWWindow::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto it = g_windowMap.find(window);
    if (it != g_windowMap.end() && it->second->framebufferSizeCallback_) {
        it->second->framebufferSizeCallback_(width, height);
    }
}

void GLFWWindow::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto it = g_windowMap.find(window);
    if (it != g_windowMap.end() && it->second->scrollCallback_) {
        it->second->scrollCallback_(xoffset, yoffset);
    }
}

}
