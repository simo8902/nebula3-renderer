// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_INPUT_H
#define NDEVC_GL_INPUT_H

#include "Input/IInputSystem.h"
#include "Platform/IWindow.h"
#include <memory>

namespace NDEVC::Platform::GLFW {

class GLFWInput : public IInputSystem {
public:
    GLFWInput(std::shared_ptr<IWindow> window);
    ~GLFWInput();

    bool IsKeyPressed(int key) const override;
    void GetCursorPos(double& x, double& y) const override;
    void SetCursorPos(double x, double y) override;
    float GetScrollDelta() override;
    void ResetScrollDelta() override;
    void Update() override;

private:
    std::shared_ptr<IWindow> window_;
    float scrollDelta_;

    void OnScroll(double xoffset, double yoffset);
};

}
#endif