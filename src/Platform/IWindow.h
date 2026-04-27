// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IWINDOW_H
#define NDEVC_IWINDOW_H

#include <string>
#include <functional>

namespace NDEVC::Platform {

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual void MakeCurrent() const = 0;
    virtual void ReleaseContext() const = 0;
    virtual void SwapBuffers() = 0;
    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void SetShouldClose(bool value) = 0;

    virtual void GetFramebufferSize(int& width, int& height) const = 0;
    virtual void SetFramebufferSizeCallback(std::function<void(int, int)> callback) = 0;

    virtual void SetScrollCallback(std::function<void(double, double)> callback) = 0;

    virtual void* GetNativeHandle() const = 0;
    virtual unsigned int GetDefaultFramebuffer() const { return 0; }

    virtual int GetKey(int key) const = 0;
    virtual bool IsKeyPressed(int key) const = 0;
    virtual bool IsMouseButtonPressed(int button) const { (void)button; return false; }

    virtual void GetCursorPos(double& x, double& y) const = 0;
    virtual void SetCursorPos(double x, double y) = 0;
    virtual void SetInputMode(int mode, int value) = 0;
    virtual void SetSwapInterval(int interval) = 0;
};

}
#endif
