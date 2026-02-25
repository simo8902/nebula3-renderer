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
    virtual void RenderFrame() = 0;
    virtual void Resize(int width, int height) = 0;

    virtual void AppendModel(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) = 0;
    virtual void LoadMap(const MapData* map) = 0;
    virtual void ReloadMapWithCurrentMode() = 0;

    virtual void SetCheckGLErrors(bool enabled) = 0;
    virtual bool GetCheckGLErrors() const = 0;
    virtual void SetRenderLog(bool enabled) = 0;

    virtual DrawCmd* GetSelectedObject() = 0;
    virtual int GetSelectedIndex() const = 0;

    virtual Camera& GetCamera() = 0;
    virtual const Camera& GetCamera() const = 0;
};

}
}
#endif