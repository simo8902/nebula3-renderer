// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Platform/IPlatform.h"

namespace NDEVC::Platform::GLFW {

class GLFWPlatform : public IPlatform {
public:
    GLFWPlatform();
    ~GLFWPlatform();

    bool Initialize() override;
    void Shutdown() override;

    std::shared_ptr<IWindow> CreateApplicationWindow(const std::string& title, int width, int height) override;
    std::shared_ptr<IInputSystem> CreateInputSystem() override;

    const char* GetPlatformName() const override;

private:
    bool initialized_;
    std::shared_ptr<IWindow> window_;
};

}
