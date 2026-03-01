// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.


#include "Input/GLFWInput.h"
#include "Platform/GLFWWindow.h"
#include "GLFW/glfw3.h"

namespace NDEVC::Platform::GLFW {

GLFWInput::GLFWInput(std::shared_ptr<IWindow> window)
    : window_(window), scrollDelta_(0.0f) {
    window_->SetScrollCallback([this](double x, double y) { OnScroll(x, y); });
}

GLFWInput::~GLFWInput() = default;

bool GLFWInput::IsKeyPressed(int key) const {
    return window_->IsKeyPressed(key);
}

void GLFWInput::GetCursorPos(double& x, double& y) const {
    window_->GetCursorPos(x, y);
}

void GLFWInput::SetCursorPos(double x, double y) {
    window_->SetCursorPos(x, y);
}

float GLFWInput::GetScrollDelta() {
    return scrollDelta_;
}

void GLFWInput::ResetScrollDelta() {
    scrollDelta_ = 0.0f;
}

void GLFWInput::Update() {
}

void GLFWInput::OnScroll(double xoffset, double yoffset) {
    scrollDelta_ = (float)yoffset;
}

}
