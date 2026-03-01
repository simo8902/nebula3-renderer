// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_BUFFER_H
#define NDEVC_GL_BUFFER_H

#include <glad/glad.h>
#include "Rendering/Interfaces/IBuffer.h"

namespace NDEVC::Graphics::OpenGL {

class OpenGLBuffer : public IBuffer {
public:
    OpenGLBuffer(const BufferDesc& desc);
    ~OpenGLBuffer();

    uint32_t GetSize() const override { return size_; }
    BufferType GetType() const override { return type_; }
    void* GetNativeHandle() const override { return (void*)&handle_; }
    void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override;

private:
    GLuint handle_;
    GLenum target_;
    BufferType type_;
    uint32_t size_;

    void CreateBuffer(const BufferDesc& desc);
    static GLenum BufferType2GL(BufferType type);
};

}
#endif