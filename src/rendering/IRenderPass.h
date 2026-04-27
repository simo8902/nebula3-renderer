// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IRENDERPASS_H
#define NDEVC_IRENDERPASS_H

struct RenderContext;
namespace NDEVC::Graphics {
    class IGraphicsDevice;
    class IShaderManager;
}
class RenderResourceRegistry;

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual const char* GetName() const = 0;
    virtual void Init(NDEVC::Graphics::IGraphicsDevice* device,
                      NDEVC::Graphics::IShaderManager* shaderManager,
                      RenderResourceRegistry* resources) = 0;
    virtual void Execute(RenderContext& ctx) = 0;
    virtual bool ShouldSkip(const RenderContext& ctx) const = 0;
    virtual void OnResize(int width, int height) = 0;
};

#endif
