// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "OpenGLFramebuffer.h"
#include <stdexcept>
#include <iostream>

namespace NDEVC::Graphics::OpenGL {

OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferDesc& desc)
    : handle_(0), desc_(desc) {
    CreateFramebuffer();
}

OpenGLFramebuffer::~OpenGLFramebuffer() {
    if (handle_) {
        glDeleteFramebuffers(1, &handle_);
    }
}

void OpenGLFramebuffer::CreateFramebuffer() {
    glGenFramebuffers(1, &handle_);
    glBindFramebuffer(GL_FRAMEBUFFER, handle_);

    int colorIdx = 0;
    for (const auto& attachment : desc_.colorAttachments) {
        if (attachment.texture) {
            GLuint texHandle = *(GLuint*)attachment.texture->GetNativeHandle();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorIdx, GL_TEXTURE_2D, texHandle, attachment.mipLevel);
            colorIdx++;
        }
    }

    if (desc_.depthStencilAttachment.texture) {
        GLuint depthTexHandle = *(GLuint*)desc_.depthStencilAttachment.texture->GetNativeHandle();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexHandle, desc_.depthStencilAttachment.mipLevel);
    }

    std::vector<GLenum> drawBuffers;
    for (int i = 0; i < colorIdx; ++i) {
        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
    }

    if (!drawBuffers.empty()) {
        glDrawBuffers(drawBuffers.size(), drawBuffers.data());
    } else if (desc_.depthStencilAttachment.texture) {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[OpenGLFramebuffer] FBO incomplete, status=0x" << std::hex << status << std::dec << "\n";
        throw std::runtime_error("Framebuffer not complete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}
