// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/RenderResourceRegistry.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Rendering/Interfaces/ITexture.h"
#include "Rendering/Interfaces/IFramebuffer.h"

#include "glad/glad.h"
#include <iostream>
#include <cassert>

RenderResourceRegistry::RenderResourceRegistry(NDEVC::Graphics::IGraphicsDevice* device,
                                               int viewportWidth, int viewportHeight)
    : device_(device)
    , viewportWidth_(viewportWidth)
    , viewportHeight_(viewportHeight)
{
}

void RenderResourceRegistry::DeclareTexture(const std::string& name,
                                             NDEVC::Graphics::Format format,
                                             bool isDepth)
{
    if (textureDescs_.count(name)) return;
    TextureDesc desc;
    desc.format = format;
    desc.isDepth = isDepth;
    desc.fixedDims = false;
    textureDescs_[name] = desc;
    textureOrder_.push_back(name);
}

void RenderResourceRegistry::DeclareTextureFixed(const std::string& name,
                                                  NDEVC::Graphics::Format format,
                                                  int width, int height,
                                                  bool isDepth)
{
    if (textureDescs_.count(name)) return;
    TextureDesc desc;
    desc.format = format;
    desc.isDepth = isDepth;
    desc.fixedDims = true;
    desc.fixedWidth = width;
    desc.fixedHeight = height;
    textureDescs_[name] = desc;
    textureOrder_.push_back(name);
}

void RenderResourceRegistry::DeclareFramebuffer(const std::string& name,
                                                 const std::vector<std::string>& colorAttachments,
                                                 const std::string& depthAttachment)
{
    if (fboDescs_.count(name)) return;
    FBODesc desc;
    desc.colorAttachments = colorAttachments;
    desc.depthAttachment = depthAttachment;
    fboDescs_[name] = desc;
    fboOrder_.push_back(name);
}

void RenderResourceRegistry::Build()
{
    for (const auto& name : textureOrder_)
        CreateTexture(name);
    for (const auto& name : fboOrder_)
        CreateFramebuffer(name);
}

void RenderResourceRegistry::Resize(int newWidth, int newHeight)
{
    if (newWidth <= 0 || newHeight <= 0) return;
    viewportWidth_  = newWidth;
    viewportHeight_ = newHeight;

    // Destroy all FBOs first (they hold references to the textures).
    DestroyAllFBOs();

    // Recreate viewport-relative textures.
    DestroyViewportTextures();
    for (const auto& name : textureOrder_) {
        if (!textureDescs_[name].fixedDims)
            CreateTexture(name);
    }

    // Rebuild all FBOs.
    for (const auto& name : fboOrder_)
        CreateFramebuffer(name);
}

NDEVC::Graphics::ITexture* RenderResourceRegistry::GetTexture(const std::string& name) const
{
    auto it = textures_.find(name);
    return it != textures_.end() ? it->second.get() : nullptr;
}

NDEVC::Graphics::IFramebuffer* RenderResourceRegistry::GetFramebuffer(const std::string& name) const
{
    auto it = fbos_.find(name);
    return it != fbos_.end() ? it->second.get() : nullptr;
}

GLuint RenderResourceRegistry::GetTextureHandle(const std::string& name) const
{
    auto* tex = GetTexture(name);
    if (!tex) return 0;
    void* handle = tex->GetNativeHandle();
    return handle ? *reinterpret_cast<GLuint*>(handle) : 0;
}

GLuint RenderResourceRegistry::GetFramebufferHandle(const std::string& name) const
{
    auto* fbo = GetFramebuffer(name);
    if (!fbo) return 0;
    void* handle = fbo->GetNativeHandle();
    return handle ? *reinterpret_cast<GLuint*>(handle) : 0;
}

void RenderResourceRegistry::CreateTexture(const std::string& name)
{
    const auto& desc = textureDescs_.at(name);
    const int w = desc.fixedDims ? desc.fixedWidth  : viewportWidth_;
    const int h = desc.fixedDims ? desc.fixedHeight : viewportHeight_;

    NDEVC::Graphics::TextureDesc td;
    td.debugName = name;
    td.type   = NDEVC::Graphics::TextureType::Texture2D;
    td.format = desc.format;
    td.width  = w;
    td.height = h;

    auto tex = device_->CreateTexture(td);
    if (!tex) {
        std::cerr << "[RRR] Failed to create texture '" << name << "'\n";
        return;
    }
    textures_[name] = std::move(tex);
}

void RenderResourceRegistry::CreateFramebuffer(const std::string& name)
{
    const auto& desc = fboDescs_.at(name);

    NDEVC::Graphics::FramebufferDesc fd;
    fd.debugName = name;
    for (const auto& colorName : desc.colorAttachments) {
        auto it = textures_.find(colorName);
        if (it == textures_.end()) {
            std::cerr << "[RRR] FBO '" << name << "': color attachment '" << colorName << "' not found\n";
            return;
        }
        NDEVC::Graphics::FramebufferAttachment att;
        att.texture = it->second;
        fd.colorAttachments.push_back(std::move(att));
    }
    if (!desc.depthAttachment.empty()) {
        auto it = textures_.find(desc.depthAttachment);
        if (it == textures_.end()) {
            std::cerr << "[RRR] FBO '" << name << "': depth attachment '" << desc.depthAttachment << "' not found\n";
            return;
        }
        fd.depthStencilAttachment.texture = it->second;
    }

    auto fbo = device_->CreateFramebuffer(fd);
    if (!fbo) {
        std::cerr << "[RRR] Failed to create framebuffer '" << name << "'\n";
        return;
    }
    fbos_[name] = std::move(fbo);
}

void RenderResourceRegistry::DestroyViewportTextures()
{
    for (const auto& name : textureOrder_) {
        if (!textureDescs_[name].fixedDims)
            textures_.erase(name);
    }
}

void RenderResourceRegistry::DestroyAllFBOs()
{
    for (const auto& name : fboOrder_)
        fbos_.erase(name);
}
