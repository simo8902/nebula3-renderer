// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/FrameGraph.h"
#include "glad/glad.h"
#include <iostream>
#include <algorithm>

namespace {
static GLenum toGL(NDEVC::Graphics::Format f) {
    switch (f) {
        case NDEVC::Graphics::Format::RGB32F: return GL_RGB32F;
        case NDEVC::Graphics::Format::RGBA32F: return GL_RGBA32F;
        case NDEVC::Graphics::Format::RGBA16F: return GL_RGBA16F;
        case NDEVC::Graphics::Format::RGB16F: return GL_RGB16F;
        case NDEVC::Graphics::Format::D24_UNORM_S8_UINT: return GL_DEPTH24_STENCIL8;
        case NDEVC::Graphics::Format::D32_FLOAT_S8_UINT: return GL_DEPTH32F_STENCIL8;
        case NDEVC::Graphics::Format::RGBA8_UNORM: return GL_RGBA8;
        case NDEVC::Graphics::Format::BC1: return GL_RGBA8;
        case NDEVC::Graphics::Format::BC3: return GL_RGBA8;
        case NDEVC::Graphics::Format::BC5: return GL_RG8;
        default: return GL_RGBA8;
    }
}

static GLenum toGL(NDEVC::Graphics::BlendFactor b) {
    switch (b) {
        case NDEVC::Graphics::BlendFactor::Zero: return GL_ZERO;
        case NDEVC::Graphics::BlendFactor::One: return GL_ONE;
        case NDEVC::Graphics::BlendFactor::SrcColor: return GL_SRC_COLOR;
        case NDEVC::Graphics::BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case NDEVC::Graphics::BlendFactor::DstColor: return GL_DST_COLOR;
        case NDEVC::Graphics::BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case NDEVC::Graphics::BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case NDEVC::Graphics::BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case NDEVC::Graphics::BlendFactor::DstAlpha: return GL_DST_ALPHA;
        case NDEVC::Graphics::BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case NDEVC::Graphics::BlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
        case NDEVC::Graphics::BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case NDEVC::Graphics::BlendFactor::ConstantAlpha: return GL_CONSTANT_ALPHA;
        case NDEVC::Graphics::BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
        default: return GL_ONE;
    }
}

static GLenum toGL(NDEVC::Graphics::CullMode c) {
    switch (c) {
        case NDEVC::Graphics::CullMode::Front: return GL_FRONT;
        case NDEVC::Graphics::CullMode::Back: return GL_BACK;
        case NDEVC::Graphics::CullMode::None: return 0;
        default: return GL_BACK;
    }
}
}

namespace NDEVC::Graphics {

FrameGraph::FrameGraph(IGraphicsDevice* device, int width, int height)
    : device_(device), width_(width), height_(height) {
}

FrameGraph::~FrameGraph() {
}

void FrameGraph::setDimensions(int width, int height) {
    if (width_ == width && height_ == height) return;
    width_ = width;
    height_ = height;
    for (auto& [name, rt] : resources_) {
        rt.texture.reset();
    }
    fbos_.clear();
}

void FrameGraph::declareResource(const std::string& name, NDEVC::Graphics::Format format, bool isDepth, bool isTransient) {
    resourceDescs_[name] = {name, format, width_, height_, isDepth, isTransient};
}

RenderPass& FrameGraph::addPass(const std::string& name) {
    auto pass = std::make_unique<RenderPass>();
    pass->name = name;
    passes_.push_back(std::move(pass));
    return *passes_.back();
}

void FrameGraph::compile() {
    createResources();
    buildFBOs();
}

void FrameGraph::execute() {
    for (auto& pass : passes_) {
        if (pass->externalFBO) {
            if (pass->bindExternalFBO) {
                pass->bindExternalFBO();
            }
            device_->SetViewport(Viewport{0.0f, 0.0f, (float)width_, (float)height_, 0.0f, 1.0f});
            applyState(*pass);
            if (pass->clearColorBuffer || pass->clearDepth || pass->clearStencil) {
                if (pass->bindExternalFBO) {
                    device_->Clear(pass->clearColorBuffer, pass->clearDepth, pass->clearStencil,
                                   pass->clearColor, 1.0f, 0);
                } else {
                    std::cerr << "[FrameGraph] Pass '" << pass->name
                              << "' requested clear on external FBO without bindExternalFBO callback\n";
                }
            }
            if (pass->execute) pass->execute();
            device_->BindFramebuffer(nullptr);
            continue;
        }

        std::shared_ptr<IFramebuffer> fbo;
        if (!pass->writes.empty()) {
            std::string key;
            for (auto& w : pass->writes) key += w + ",";
            if (fbos_.count(key)) {
                fbo = fbos_[key];
            } else {
                FramebufferDesc fbDesc;
                fbDesc.width = width_;
                fbDesc.height = height_;

                for (auto& write : pass->writes) {
                    auto& rt = resources_[write];
                    if (rt.isDepth) {
                        fbDesc.depthStencilAttachment.texture = rt.texture;
                        fbDesc.depthStencilAttachment.mipLevel = 0;
                    } else {
                        FramebufferAttachment attach;
                        attach.texture = rt.texture;
                        attach.mipLevel = 0;
                        fbDesc.colorAttachments.push_back(attach);
                    }
                }

                fbo = device_->CreateFramebuffer(fbDesc);
                fbos_[key] = fbo;
            }
        }

        device_->BindFramebuffer(fbo.get());
        device_->SetViewport(Viewport{0.0f, 0.0f, (float)width_, (float)height_, 0.0f, 1.0f});
        applyState(*pass);

        if (pass->clearColorBuffer || pass->clearDepth || pass->clearStencil) {
            device_->Clear(pass->clearColorBuffer, pass->clearDepth, pass->clearStencil,
                          pass->clearColor, 1.0f, 0);
        }

        if (pass->execute) pass->execute();

        device_->BindFramebuffer(nullptr);
    }
}

std::shared_ptr<ITexture> FrameGraph::getTextureInterface(const std::string& name) const {
    auto it = resources_.find(name);
    return (it != resources_.end()) ? it->second.texture : nullptr;
}

void FrameGraph::createResources() {
    for (auto& [name, desc] : resourceDescs_) {
        if (resources_.count(name) && resources_[name].texture) continue;

        TextureDesc texDesc;
        texDesc.format = desc.format;
        texDesc.width = width_;
        texDesc.height = height_;
        texDesc.type = TextureType::Texture2D;

        RenderTarget rt;
        rt.format = desc.format;
        rt.width = width_;
        rt.height = height_;
        rt.isDepth = desc.isDepth;
        rt.isTransient = desc.isTransient;
        rt.texture = device_->CreateTexture(texDesc);

        resources_[name] = rt;
    }
}

void FrameGraph::buildFBOs() {
}

void FrameGraph::applyState(const RenderPass& pass) {
    RenderStateDesc stateDesc;

    stateDesc.depth.depthTest = pass.depthTest;
    stateDesc.depth.depthWrite = pass.depthWrite;
    stateDesc.depth.depthFunc = CompareFunc::Less;

    stateDesc.blend.blendEnable = pass.blend;
    stateDesc.blend.srcColor = pass.blendSrc;
    stateDesc.blend.dstColor = pass.blendDst;
    stateDesc.blend.srcAlpha = pass.blendSrc;
    stateDesc.blend.dstAlpha = pass.blendDst;
    stateDesc.rasterizer.cullMode = pass.cullFace ? pass.cullMode : CullMode::None;

    const GLenum glFormat = toGL(Format::RGBA8_UNORM);
    const GLenum glBlend = toGL(pass.blendSrc);
    const GLenum glCull = toGL(pass.cullMode);
    (void)glFormat;
    (void)glBlend;
    (void)glCull;

    stateDesc.stencil.stencilEnable = pass.stencilTest;
    stateDesc.stencil.writeMask = 0xFF;

    auto renderState = device_->CreateRenderState(stateDesc);
    device_->ApplyRenderState(renderState.get());
}

}
