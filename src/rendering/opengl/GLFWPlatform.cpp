// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "GLFWPlatform.h"
#include "GLFWWindow.h"
#include "GLFWInput.h"
#include "GLFW/glfw3.h"
#include <iostream>

namespace NDEVC::Platform::GLFW {

GLFWPlatform::GLFWPlatform()
    : initialized_(false) {
}

GLFWPlatform::~GLFWPlatform() {
    Shutdown();
}

bool GLFWPlatform::Initialize() {
    if (initialized_) return true;

    if (!glfwInit()) {
        std::cerr << "[GLFW] Initialization failed\n";
        return false;
    }

    initialized_ = true;
    return true;
}

void GLFWPlatform::Shutdown() {
    if (!initialized_) return;

    window_.reset();
    glfwTerminate();
    initialized_ = false;
}

std::shared_ptr<IWindow> GLFWPlatform::CreateApplicationWindow(const std::string& title, int width, int height) {
    if (!initialized_) {
        std::cerr << "[GLFW] Platform not initialized\n";
        return nullptr;
    }

    window_ = std::make_shared<GLFWWindow>(title, width, height);
    return window_;
}

std::shared_ptr<IInputSystem> GLFWPlatform::CreateInputSystem() {
    if (!window_) {
        std::cerr << "[GLFW] Window not created\n";
        return nullptr;
    }

    return std::make_shared<GLFWInput>(window_);
}

const char* GLFWPlatform::GetPlatformName() const {
    return "GLFW";
}

}
