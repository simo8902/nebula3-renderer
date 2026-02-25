// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_FRAMEBUFFER_H
#define NDEVC_GL_FRAMEBUFFER_H

#include <glad/glad.h>
#include "../abstract/IFramebuffer.h"

namespace NDEVC::Graphics::OpenGL {

class OpenGLFramebuffer : public IFramebuffer {
public:
    OpenGLFramebuffer(const FramebufferDesc& desc);
    ~OpenGLFramebuffer();

    uint32_t GetWidth() const override { return desc_.width; }
    uint32_t GetHeight() const override { return desc_.height; }
    void* GetNativeHandle() const override { return (void*)&handle_; }

private:
    GLuint handle_;
    FramebufferDesc desc_;

    void CreateFramebuffer();
};

}
#endif