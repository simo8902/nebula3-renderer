// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IGRAPHICSDEVICE_H
#define NDEVC_IGRAPHICSDEVICE_H

#include <memory>
#include "glm.hpp"
#include "Rendering/Interfaces/RenderingTypes.h"
#include "ITexture.h"
#include "IFramebuffer.h"
#include "IRenderState.h"
#include "IBuffer.h"
#include "ISampler.h"

namespace NDEVC::Graphics {

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;

    virtual std::shared_ptr<ITexture> CreateTexture(const TextureDesc& desc) = 0;
    virtual std::shared_ptr<IFramebuffer> CreateFramebuffer(const FramebufferDesc& desc) = 0;
    virtual std::shared_ptr<IRenderState> CreateRenderState(const RenderStateDesc& desc) = 0;
    virtual std::shared_ptr<IBuffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual std::shared_ptr<ISampler> CreateSampler(const SamplerDesc& desc) = 0;

    virtual void SetViewport(const Viewport& viewport) = 0;
    virtual void BindFramebuffer(IFramebuffer* fbo) = 0;
    virtual void BindTexture(ITexture* texture, uint32_t slot) = 0;
    virtual void BindSampler(ISampler* sampler, uint32_t slot) = 0;
    virtual void ApplyRenderState(IRenderState* state) = 0;

    virtual void Clear(bool color, bool depth, bool stencil,
        const glm::vec4& clearColor = glm::vec4(0),
        float clearDepth = 1.0f,
        uint8_t clearStencil = 0) = 0;

    virtual const char* GetRendererName() const = 0;
};

}
#endif