// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_TEXTURE_H
#define NDEVC_GL_TEXTURE_H

#include <glad/glad.h>
#include "Rendering/Interfaces/ITexture.h"
namespace NDEVC::Graphics::OpenGL {

class OpenGLTexture : public ITexture {
public:
    OpenGLTexture(const TextureDesc& desc);
    ~OpenGLTexture();

    uint32_t GetWidth() const override { return desc_.width; }
    uint32_t GetHeight() const override { return desc_.height; }
    Format GetFormat() const override { return desc_.format; }
    TextureType GetType() const override { return desc_.type; }
    void* GetNativeHandle() const override { return (void*)&handle_; }
    GLenum GetTarget() const { return target_; }

private:
    GLuint handle_;
    GLenum target_;
    TextureDesc desc_;

    void CreateTexture();
    void SetupSamplerState();
    static void ConvertFormat(Format format, GLenum& internalFormat, GLenum& baseFormat, GLenum& type);
};

}
#endif