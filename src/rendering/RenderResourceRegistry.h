// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERRESOURCEREGISTRY_H
#define NDEVC_RENDERRESOURCEREGISTRY_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

#include "Rendering/Interfaces/RenderingTypes.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Rendering/Interfaces/ITexture.h"
#include "Rendering/Interfaces/IFramebuffer.h"

// Forward-declare the GL handle type to avoid pulling in OpenGL headers here.
// Pass classes that need the raw GLuint use GetTextureHandle()/GetFramebufferHandle().
typedef unsigned int GLuint;

class RenderResourceRegistry {
public:
    RenderResourceRegistry(NDEVC::Graphics::IGraphicsDevice* device, int viewportWidth, int viewportHeight);
    ~RenderResourceRegistry() = default;

    // Declare a viewport-relative 2D texture (recreated on Resize).
    void DeclareTexture(const std::string& name,
                        NDEVC::Graphics::Format format,
                        bool isDepth = false);

    // Declare a fixed-dimension 2D texture (shadow maps etc.; not resized).
    void DeclareTextureFixed(const std::string& name,
                              NDEVC::Graphics::Format format,
                              int width, int height,
                              bool isDepth = false);

    // Declare a framebuffer whose attachments are named registered textures.
    // colorAttachments are bound in order to COLOR_ATTACHMENT0..N.
    // depthAttachment (optional) is bound to DEPTH_STENCIL_ATTACHMENT.
    void DeclareFramebuffer(const std::string& name,
                             const std::vector<std::string>& colorAttachments,
                             const std::string& depthAttachment = "");

    // Build all declared resources (call once after all Declare* calls).
    void Build();

    // Recreate viewport-relative textures and all FBOs at the new dimensions.
    void Resize(int newWidth, int newHeight);

    // Resource accessors.
    NDEVC::Graphics::ITexture*      GetTexture(const std::string& name) const;
    NDEVC::Graphics::IFramebuffer*  GetFramebuffer(const std::string& name) const;
    GLuint GetTextureHandle(const std::string& name) const;
    GLuint GetFramebufferHandle(const std::string& name) const;

    int GetViewportWidth()  const { return viewportWidth_; }
    int GetViewportHeight() const { return viewportHeight_; }

private:
    NDEVC::Graphics::IGraphicsDevice* device_;
    int viewportWidth_;
    int viewportHeight_;

    struct TextureDesc {
        NDEVC::Graphics::Format format;
        bool isDepth = false;
        bool fixedDims = false;
        int fixedWidth = 0;
        int fixedHeight = 0;
    };

    struct FBODesc {
        std::vector<std::string> colorAttachments;
        std::string depthAttachment;
    };

    std::unordered_map<std::string, TextureDesc>   textureDescs_;
    std::unordered_map<std::string, FBODesc>       fboDescs_;

    std::unordered_map<std::string, std::shared_ptr<NDEVC::Graphics::ITexture>>      textures_;
    std::unordered_map<std::string, std::shared_ptr<NDEVC::Graphics::IFramebuffer>>  fbos_;

    // Declaration order is preserved for deterministic creation.
    std::vector<std::string> textureOrder_;
    std::vector<std::string> fboOrder_;

    void CreateTexture(const std::string& name);
    void CreateFramebuffer(const std::string& name);
    void DestroyViewportTextures();
    void DestroyAllFBOs();
};

#endif
