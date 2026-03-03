// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLDevice.h"
#include "Rendering/OpenGL/OpenGLTexture.h"
#include "Rendering/OpenGL/OpenGLFramebuffer.h"
#include "Rendering/OpenGL/OpenGLRenderState.h"
#include "Rendering/OpenGL/OpenGLBuffer.h"
#include "Rendering/OpenGL/OpenGLSampler.h"
#include "Core/Logger.h"
#include <cstring>

namespace NDEVC::Graphics::OpenGL {

OpenGLDevice::OpenGLDevice()
    : boundFBO_(0), boundProgram_(0), currentRenderState_(nullptr) {
    for (int i = 0; i < 32; ++i) {
        boundTextures_[i] = 0;
        boundTextureTargets_[i] = GL_TEXTURE_2D;
    }
    if (!GLAD_GL_ARB_shader_storage_buffer_object)
        NC::LOGGING::Warning("[GL] GL_ARB_shader_storage_buffer_object not supported — SSBOs unavailable");
    if (!GLAD_GL_ARB_shader_draw_parameters)
        NC::LOGGING::Warning("[GL] GL_ARB_shader_draw_parameters not supported — gl_DrawID/gl_BaseVertex unavailable");
    if (!GLAD_GL_ARB_gpu_shader_int64)
        NC::LOGGING::Warning("[GL] GL_ARB_gpu_shader_int64 not supported — int64 uniforms unavailable");
}

std::shared_ptr<ITexture> OpenGLDevice::CreateTexture(const TextureDesc& desc) {
    return std::make_shared<OpenGLTexture>(desc);
}

std::shared_ptr<IFramebuffer> OpenGLDevice::CreateFramebuffer(const FramebufferDesc& desc) {
    return std::make_shared<OpenGLFramebuffer>(desc);
}

std::shared_ptr<IRenderState> OpenGLDevice::CreateRenderState(const RenderStateDesc& desc) {
    return std::make_shared<OpenGLRenderState>(desc);
}

std::shared_ptr<IBuffer> OpenGLDevice::CreateBuffer(const BufferDesc& desc) {
    return std::make_shared<OpenGLBuffer>(desc);
}

std::shared_ptr<ISampler> OpenGLDevice::CreateSampler(const SamplerDesc& desc) {
    return std::make_shared<OpenGLSampler>(desc);
}

void OpenGLDevice::BindStorageBuffer(IBuffer* buffer, uint32_t bindingPoint) {
    const GLuint handle = buffer ? *(const GLuint*)buffer->GetNativeHandle() : 0;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, handle);
}

void OpenGLDevice::SetViewport(const Viewport& viewport) {
    if (cachedViewport_.x     == viewport.x        && cachedViewport_.y        == viewport.y &&
        cachedViewport_.width == viewport.width     && cachedViewport_.height   == viewport.height &&
        cachedViewport_.minDepth == viewport.minDepth && cachedViewport_.maxDepth == viewport.maxDepth) {
        return;
    }
    cachedViewport_ = viewport;
    glViewport((GLint)viewport.x, (GLint)viewport.y, (GLsizei)viewport.width, (GLsizei)viewport.height);
    glDepthRange(viewport.minDepth, viewport.maxDepth);
}

void OpenGLDevice::BindFramebuffer(IFramebuffer* fbo) {
    GLuint handle = fbo ? *(GLuint*)fbo->GetNativeHandle() : 0;
    if (boundFBO_ != handle) {
        glBindFramebuffer(GL_FRAMEBUFFER, handle);
        boundFBO_ = handle;
    }
}

void OpenGLDevice::BindTexture(ITexture* texture, uint32_t slot) {
    if (slot >= 32) return;

    GLuint handle = texture ? *(GLuint*)texture->GetNativeHandle() : 0;
    GLenum target = GL_TEXTURE_2D;
    if (texture && texture->GetType() == TextureType::TextureCube) {
        target = GL_TEXTURE_CUBE_MAP;
    }

    if (boundTextures_[slot] != handle || boundTextureTargets_[slot] != target) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(target, handle);
        boundTextures_[slot] = handle;
        boundTextureTargets_[slot] = target;
    }
}

void OpenGLDevice::BindSampler(ISampler* sampler, uint32_t slot) {
    if (slot >= 32) return;
    GLuint handle = sampler ? *(GLuint*)sampler->GetNativeHandle() : 0;
    glBindSampler(slot, handle);
}

void OpenGLDevice::ApplyRenderState(IRenderState* state) {
    if (!state) return;

    const RenderStateDesc& desc = state->GetDesc();

    const bool depthChanged   = !renderStateCacheValid_ || std::memcmp(&desc.depth,      &cachedRenderState_.depth,      sizeof(DepthState))     != 0;
    const bool blendChanged   = !renderStateCacheValid_ || std::memcmp(&desc.blend,      &cachedRenderState_.blend,      sizeof(BlendState))     != 0;
    const bool rastChanged    = !renderStateCacheValid_ || std::memcmp(&desc.rasterizer, &cachedRenderState_.rasterizer, sizeof(RasterizerState))!= 0;
    const bool stencilChanged = !renderStateCacheValid_ || std::memcmp(&desc.stencil,    &cachedRenderState_.stencil,    sizeof(StencilState))   != 0;

    if (depthChanged)   { ApplyDepthState(desc.depth);           cachedRenderState_.depth      = desc.depth; }
    if (blendChanged)   { ApplyBlendState(desc.blend);           cachedRenderState_.blend      = desc.blend; }
    if (rastChanged)    { ApplyRasterizerState(desc.rasterizer); cachedRenderState_.rasterizer = desc.rasterizer; }
    if (stencilChanged) { ApplyStencilState(desc.stencil);       cachedRenderState_.stencil    = desc.stencil; }
    renderStateCacheValid_ = true;
}

void OpenGLDevice::ApplyDepthState(const DepthState& depth) {
    if (depth.depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(CompareFunc2GL(depth.depthFunc));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(depth.depthWrite ? GL_TRUE : GL_FALSE);
}

void OpenGLDevice::ApplyBlendState(const BlendState& blend) {
    if (blend.blendEnable) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(
            BlendFactor2GL(blend.srcColor), BlendFactor2GL(blend.dstColor),
            BlendFactor2GL(blend.srcAlpha), BlendFactor2GL(blend.dstAlpha)
        );
    } else {
        glDisable(GL_BLEND);
    }
}

void OpenGLDevice::ApplyRasterizerState(const RasterizerState& rasterizer) {
    if (rasterizer.cullMode != CullMode::None) {
        glEnable(GL_CULL_FACE);
        glCullFace(CullMode2GL(rasterizer.cullMode));
    } else {
        glDisable(GL_CULL_FACE);
    }

    if (rasterizer.scissorEnable) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

GLenum OpenGLDevice::StencilOp2GL(StencilOp op) {
    switch (op) {
        case StencilOp::Keep: return GL_KEEP;
        case StencilOp::Zero: return GL_ZERO;
        case StencilOp::Replace: return GL_REPLACE;
        case StencilOp::Incr: return GL_INCR;
        case StencilOp::IncrWrap: return GL_INCR_WRAP;
        case StencilOp::Decr: return GL_DECR;
        case StencilOp::DecrWrap: return GL_DECR_WRAP;
        case StencilOp::Invert: return GL_INVERT;
        default: return GL_KEEP;
    }
}

void OpenGLDevice::ApplyStencilState(const StencilState& stencil) {
    if (stencil.stencilEnable) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(stencil.writeMask);
        glStencilFunc(CompareFunc2GL(stencil.stencilFunc), stencil.ref, stencil.readMask);
        glStencilOp(StencilOp2GL(stencil.stencilFailOp), StencilOp2GL(stencil.depthFailOp), StencilOp2GL(stencil.depthPassOp));
    } else {
        glDisable(GL_STENCIL_TEST);
    }
}

void OpenGLDevice::Clear(bool color, bool depth, bool stencil,
    const glm::vec4& clearColor, float clearDepth, uint8_t clearStencil) {
    GLbitfield clearBits = 0;

    if (color) {
        clearBits |= GL_COLOR_BUFFER_BIT;
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    }
    if (depth) {
        clearBits |= GL_DEPTH_BUFFER_BIT;
        glClearDepth(clearDepth);
    }
    if (stencil) {
        clearBits |= GL_STENCIL_BUFFER_BIT;
        glClearStencil(clearStencil);
    }

    if (clearBits) {
        glClear(clearBits);
    }
}

const char* OpenGLDevice::GetRendererName() const {
    return "OpenGL 4.6";
}

GLenum OpenGLDevice::CompareFunc2GL(CompareFunc func) {
    switch (func) {
        case CompareFunc::Never: return GL_NEVER;
        case CompareFunc::Less: return GL_LESS;
        case CompareFunc::Equal: return GL_EQUAL;
        case CompareFunc::LessEqual: return GL_LEQUAL;
        case CompareFunc::Greater: return GL_GREATER;
        case CompareFunc::NotEqual: return GL_NOTEQUAL;
        case CompareFunc::GreaterEqual: return GL_GEQUAL;
        case CompareFunc::Always: return GL_ALWAYS;
        default: return GL_ALWAYS;
    }
}

GLenum OpenGLDevice::BlendFactor2GL(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcColor: return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha: return GL_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
        default: return GL_ONE;
    }
}

GLenum OpenGLDevice::CullMode2GL(CullMode mode) {
    switch (mode) {
        case CullMode::Front: return GL_FRONT;
        case CullMode::Back: return GL_BACK;
        default: return GL_BACK;
    }
}

}
