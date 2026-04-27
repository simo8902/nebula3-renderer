// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERER_H
#define NDEVC_RENDERER_H

#include <string>
#include <memory>
#include "glm.hpp"
#include "gtc/quaternion.hpp"

class Camera;
struct DrawCmd;
struct MapData;
class SceneManager;

namespace NDEVC {
namespace Graphics {

class IGraphicsDevice;
class ITexture;

namespace Platform {
class IPlatform;
class IWindow;
class IInputSystem;
}

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual void UpdateFrameTime() = 0;
    virtual void SetFrameDeltaTime(double dt) = 0;
    virtual void PollEvents() = 0;    virtual void RenderFrame() = 0;
    virtual void RenderSingleFrame() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void Resize(int width, int height) = 0;

    virtual void SetCheckGLErrors(bool enabled) = 0;
    virtual bool GetCheckGLErrors() const = 0;
    virtual void SetRenderLog(bool enabled) = 0;

    virtual void AttachScene(SceneManager& scene) = 0;
    virtual SceneManager& GetScene() = 0;
    virtual Camera& GetCamera() = 0;
    virtual const Camera& GetCamera() const = 0;
};

}
}
#endif
