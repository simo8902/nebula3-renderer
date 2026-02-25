// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "FrameGraph.h"
#include <iostream>
#include <algorithm>

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

void FrameGraph::declareResource(const std::string& name, GLenum format, bool isDepth, bool isTransient) {
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

GLuint FrameGraph::getTexture(const std::string& name) const {
    auto it = resources_.find(name);
    if (it != resources_.end() && it->second.texture) {
        return *(GLuint*)it->second.texture->GetNativeHandle();
    }
    return 0;
}

std::shared_ptr<ITexture> FrameGraph::getTextureInterface(const std::string& name) const {
    auto it = resources_.find(name);
    return (it != resources_.end()) ? it->second.texture : nullptr;
}

void FrameGraph::createResources() {
    for (auto& [name, desc] : resourceDescs_) {
        if (resources_.count(name) && resources_[name].texture) continue;

        Format abstractFormat = Format::RGBA8_UNORM;
        if (desc.format == GL_RGB32F) abstractFormat = Format::RGB32F;
        else if (desc.format == GL_RGBA32F) abstractFormat = Format::RGBA32F;
        else if (desc.format == GL_RGBA16F) abstractFormat = Format::RGBA16F;
        else if (desc.format == GL_RGB16F) abstractFormat = Format::RGB16F;
        else if (desc.format == GL_DEPTH24_STENCIL8) abstractFormat = Format::D24_UNORM_S8_UINT;
        else if (desc.format == GL_DEPTH32F_STENCIL8) abstractFormat = Format::D32_FLOAT_S8_UINT;
        else if (desc.format == GL_RGBA8) abstractFormat = Format::RGBA8_UNORM;

        TextureDesc texDesc;
        texDesc.format = abstractFormat;
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

BlendFactor FrameGraph::glBlendToAbstract(GLenum glBlend) {
    switch (glBlend) {
        case GL_ZERO: return BlendFactor::Zero;
        case GL_ONE: return BlendFactor::One;
        case GL_SRC_COLOR: return BlendFactor::SrcColor;
        case GL_ONE_MINUS_SRC_COLOR: return BlendFactor::OneMinusSrcColor;
        case GL_DST_COLOR: return BlendFactor::DstColor;
        case GL_ONE_MINUS_DST_COLOR: return BlendFactor::OneMinusDstColor;
        case GL_SRC_ALPHA: return BlendFactor::SrcAlpha;
        case GL_ONE_MINUS_SRC_ALPHA: return BlendFactor::OneMinusSrcAlpha;
        case GL_DST_ALPHA: return BlendFactor::DstAlpha;
        case GL_ONE_MINUS_DST_ALPHA: return BlendFactor::OneMinusDstAlpha;
        default: return BlendFactor::One;
    }
}

void FrameGraph::applyState(const RenderPass& pass) {
    RenderStateDesc stateDesc;

    stateDesc.depth.depthTest = pass.depthTest;
    stateDesc.depth.depthWrite = pass.depthWrite;
    stateDesc.depth.depthFunc = CompareFunc::Less;

    stateDesc.blend.blendEnable = pass.blend;
    stateDesc.blend.srcColor = glBlendToAbstract(pass.blendSrc);
    stateDesc.blend.dstColor = glBlendToAbstract(pass.blendDst);
    stateDesc.blend.srcAlpha = glBlendToAbstract(pass.blendSrc);
    stateDesc.blend.dstAlpha = glBlendToAbstract(pass.blendDst);

    switch (pass.cullMode) {
        case GL_FRONT: stateDesc.rasterizer.cullMode = CullMode::Front; break;
        case GL_BACK: stateDesc.rasterizer.cullMode = CullMode::Back; break;
        default: stateDesc.rasterizer.cullMode = pass.cullFace ? CullMode::Back : CullMode::None;
    }

    stateDesc.stencil.stencilEnable = pass.stencilTest;
    stateDesc.stencil.writeMask = 0xFF;

    auto renderState = device_->CreateRenderState(stateDesc);
    device_->ApplyRenderState(renderState.get());
}

}
