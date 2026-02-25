// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IPLATFORM_H
#define NDEVC_IPLATFORM_H

#include <memory>
#include <string>

namespace NDEVC::Platform {

class IWindow;
class IInputSystem;

class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual std::shared_ptr<IWindow> CreateApplicationWindow(const std::string& title, int width, int height) = 0;
    virtual std::shared_ptr<IInputSystem> CreateInputSystem() = 0;

    virtual const char* GetPlatformName() const = 0;
};

}
#endif