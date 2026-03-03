// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_DEVICE_H
#define NDEVC_GL_DEVICE_H

#include <glad/glad.h>
#include <memory>
#include "Rendering/Interfaces/IGraphicsDevice.h"

namespace NDEVC::Graphics::OpenGL {

class OpenGLDevice : public IGraphicsDevice {
public:
    OpenGLDevice();
    ~OpenGLDevice() = default;

    std::shared_ptr<ITexture> CreateTexture(const TextureDesc& desc) override;
    std::shared_ptr<IFramebuffer> CreateFramebuffer(const FramebufferDesc& desc) override;
    std::shared_ptr<IRenderState> CreateRenderState(const RenderStateDesc& desc) override;
    std::shared_ptr<IBuffer> CreateBuffer(const BufferDesc& desc) override;
    std::shared_ptr<ISampler> CreateSampler(const SamplerDesc& desc) override;

    void BindStorageBuffer(IBuffer* buffer, uint32_t bindingPoint) override;
    void SetViewport(const Viewport& viewport) override;
    void BindFramebuffer(IFramebuffer* fbo) override;
    void BindTexture(ITexture* texture, uint32_t slot) override;
    void BindSampler(ISampler* sampler, uint32_t slot) override;
    void ApplyRenderState(IRenderState* state) override;
    void InvalidateRenderStateCache() override { renderStateCacheValid_ = false; }

    void Clear(bool color, bool depth, bool stencil,
        const glm::vec4& clearColor = glm::vec4(0),
        float clearDepth = 1.0f,
        uint8_t clearStencil = 0) override;

    const char* GetRendererName() const override;

private:
    GLuint boundFBO_;
    GLuint boundProgram_;
    GLuint boundTextures_[32];
    GLenum boundTextureTargets_[32];
    IRenderState* currentRenderState_;

    // Per-instance render state cache (invalidated each frame after baseline reset)
    RenderStateDesc cachedRenderState_{};
    bool renderStateCacheValid_ = false;

    // Per-instance viewport cache
    Viewport cachedViewport_{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};

    void ApplyDepthState(const DepthState& depth);
    void ApplyBlendState(const BlendState& blend);
    void ApplyRasterizerState(const RasterizerState& rasterizer);
    void ApplyStencilState(const StencilState& stencil);

    static GLenum CompareFunc2GL(CompareFunc func);
    static GLenum BlendFactor2GL(BlendFactor factor);
    static GLenum CullMode2GL(CullMode mode);
    static GLenum StencilOp2GL(StencilOp op);
};

}
#endif