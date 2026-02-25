// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_SAMPLER_H
#define NDEVC_GL_SAMPLER_H

#include <glad/glad.h>
#include "../abstract/ISampler.h"

namespace NDEVC::Graphics::OpenGL {

class OpenGLSampler : public ISampler {
public:
    OpenGLSampler(const SamplerDesc& desc);
    ~OpenGLSampler();

    const SamplerDesc& GetDesc() const override { return desc_; }
    void* GetNativeHandle() const override { return (void*)&handle_; }

private:
    GLuint handle_;
    SamplerDesc desc_;

    void CreateSampler(const SamplerDesc& desc);
    static GLenum SamplerFilter2GL(SamplerFilter filter);
    static GLenum SamplerWrap2GL(SamplerWrap wrap);
    static GLenum CompareFunc2GL(CompareFunc func);
};

}
#endif