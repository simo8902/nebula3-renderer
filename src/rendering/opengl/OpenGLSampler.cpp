// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLSampler.h"

namespace NDEVC::Graphics::OpenGL {

OpenGLSampler::OpenGLSampler(const SamplerDesc& desc)
    : handle_(0), desc_(desc) {
    CreateSampler(desc);
}

OpenGLSampler::~OpenGLSampler() {
    if (handle_) {
        glDeleteSamplers(1, &handle_);
    }
}

void OpenGLSampler::CreateSampler(const SamplerDesc& desc) {
    glGenSamplers(1, &handle_);

    glSamplerParameteri(handle_, GL_TEXTURE_MIN_FILTER, SamplerFilter2GL(desc.minFilter));
    glSamplerParameteri(handle_, GL_TEXTURE_MAG_FILTER, SamplerFilter2GL(desc.magFilter));
    glSamplerParameteri(handle_, GL_TEXTURE_WRAP_S, SamplerWrap2GL(desc.wrapS));
    glSamplerParameteri(handle_, GL_TEXTURE_WRAP_T, SamplerWrap2GL(desc.wrapT));
    glSamplerParameteri(handle_, GL_TEXTURE_WRAP_R, SamplerWrap2GL(desc.wrapR));

    if (desc.useCompare) {
        glSamplerParameteri(handle_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(handle_, GL_TEXTURE_COMPARE_FUNC, CompareFunc2GL(desc.compareFunc));
    }

    glSamplerParameterf(handle_, GL_TEXTURE_MIN_LOD, desc.minLod);
    glSamplerParameterf(handle_, GL_TEXTURE_MAX_LOD, desc.maxLod);
    glSamplerParameterf(handle_, GL_TEXTURE_LOD_BIAS, desc.lodBias);
}

GLenum OpenGLSampler::SamplerFilter2GL(SamplerFilter filter) {
    switch (filter) {
        case SamplerFilter::Nearest: return GL_NEAREST;
        case SamplerFilter::Linear: return GL_LINEAR;
        case SamplerFilter::NearestMipmapNearest: return GL_NEAREST_MIPMAP_NEAREST;
        case SamplerFilter::LinearMipmapNearest: return GL_LINEAR_MIPMAP_NEAREST;
        case SamplerFilter::NearestMipmapLinear: return GL_NEAREST_MIPMAP_LINEAR;
        case SamplerFilter::LinearMipmapLinear: return GL_LINEAR_MIPMAP_LINEAR;
        default: return GL_LINEAR;
    }
}

GLenum OpenGLSampler::SamplerWrap2GL(SamplerWrap wrap) {
    switch (wrap) {
        case SamplerWrap::Repeat: return GL_REPEAT;
        case SamplerWrap::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case SamplerWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
        case SamplerWrap::ClampToBorder: return GL_CLAMP_TO_BORDER;
        default: return GL_REPEAT;
    }
}

GLenum OpenGLSampler::CompareFunc2GL(CompareFunc func) {
    switch (func) {
        case CompareFunc::Never: return GL_NEVER;
        case CompareFunc::Less: return GL_LESS;
        case CompareFunc::Equal: return GL_EQUAL;
        case CompareFunc::LessEqual: return GL_LEQUAL;
        case CompareFunc::Greater: return GL_GREATER;
        case CompareFunc::NotEqual: return GL_NOTEQUAL;
        case CompareFunc::GreaterEqual: return GL_GEQUAL;
        case CompareFunc::Always: return GL_ALWAYS;
        default: return GL_ALWAYS;
    }
}

}
