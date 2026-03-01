// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IINPUTSYSTEM_H
#define NDEVC_IINPUTSYSTEM_H

namespace NDEVC::Platform {

class IInputSystem {
public:
    virtual ~IInputSystem() = default;

    virtual bool IsKeyPressed(int key) const = 0;
    virtual void GetCursorPos(double& x, double& y) const = 0;
    virtual void SetCursorPos(double x, double y) = 0;
    virtual float GetScrollDelta() = 0;
    virtual void ResetScrollDelta() = 0;
    virtual void Update() = 0;
};

}
#endif