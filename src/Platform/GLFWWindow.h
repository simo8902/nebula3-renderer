// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_WINDOW_H
#define NDEVC_GL_WINDOW_H
#include <memory>
#include <functional>
#include "Platform/IWindow.h"

struct GLFWwindow;

namespace NDEVC::Platform::GLFW {

class GLFWWindow : public IWindow {
public:
    GLFWWindow(const std::string& title, int width, int height);
    ~GLFWWindow();

    void MakeCurrent() const override;
    void ReleaseContext() const override;
    void SwapBuffers() override;
    void PollEvents() override;
    bool ShouldClose() const override;
    void SetShouldClose(bool value) override;

    void GetFramebufferSize(int& width, int& height) const override;
    void SetFramebufferSizeCallback(std::function<void(int, int)> callback) override;

    void SetScrollCallback(std::function<void(double, double)> callback) override;

    void* GetNativeHandle() const override;

    int GetKey(int key) const override;
    bool IsKeyPressed(int key) const override;
    bool IsMouseButtonPressed(int button) const override;

    void GetCursorPos(double& x, double& y) const override;
    void SetCursorPos(double x, double y) override;
    void SetInputMode(int mode, int value) override;
    void SetSwapInterval(int interval) override;

private:
    GLFWwindow* handle_;
    std::function<void(int, int)> framebufferSizeCallback_;
    std::function<void(double, double)> scrollCallback_;

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

}
#endif
