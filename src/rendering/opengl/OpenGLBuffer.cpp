// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLBuffer.h"

namespace NDEVC::Graphics::OpenGL {

OpenGLBuffer::OpenGLBuffer(const BufferDesc& desc)
    : handle_(0), type_(desc.type), size_(desc.size) {
    CreateBuffer(desc);
}

OpenGLBuffer::~OpenGLBuffer() {
    if (handle_) {
        glDeleteBuffers(1, &handle_);
    }
}

void OpenGLBuffer::CreateBuffer(const BufferDesc& desc) {
    target_ = BufferType2GL(desc.type);
    glGenBuffers(1, &handle_);
    glBindBuffer(target_, handle_);
    glBufferData(target_, desc.size, desc.initialData, GL_DYNAMIC_DRAW);
    glBindBuffer(target_, 0);
}

void OpenGLBuffer::UpdateData(const void* data, uint32_t size, uint32_t offset) {
    if (size + offset > size_) return;
    glBindBuffer(target_, handle_);
    glBufferSubData(target_, offset, size, data);
    glBindBuffer(target_, 0);
}

GLenum OpenGLBuffer::BufferType2GL(BufferType type) {
    switch (type) {
        case BufferType::Vertex: return GL_ARRAY_BUFFER;
        case BufferType::Index: return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::Uniform: return GL_UNIFORM_BUFFER;
        case BufferType::Indirect: return GL_DRAW_INDIRECT_BUFFER;
        default: return GL_ARRAY_BUFFER;
    }
}

}
