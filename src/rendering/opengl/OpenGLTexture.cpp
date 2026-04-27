// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLTexture.h"
#include <stdexcept>

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace NDEVC::Graphics::OpenGL {

OpenGLTexture::OpenGLTexture(const TextureDesc& desc)
    : handle_(0), target_(GL_TEXTURE_2D), desc_(desc) {
    CreateTexture();
}

OpenGLTexture::~OpenGLTexture() {
    if (handle_) {
        glDeleteTextures(1, &handle_);
    }
}

void OpenGLTexture::CreateTexture() {
    const GLenum target = (desc_.type == TextureType::TextureCube) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glCreateTextures(target, 1, &handle_);
    target_ = target;

    GLenum internalFormat, baseFormat, type;
    ConvertFormat(desc_.format, internalFormat, baseFormat, type);

    glTextureStorage2D(handle_, desc_.mipLevels, internalFormat, desc_.width, desc_.height);

    // AAA Strategy: Set sampler state BEFORE bindless handover
    SetupSamplerState();

    // Bindless Residency
    bindlessHandle_ = glGetTextureHandleARB(handle_);
    if (bindlessHandle_) {
        glMakeTextureHandleResidentARB(bindlessHandle_);
    }

    if (!desc_.debugName.empty()) {
        glObjectLabel(GL_TEXTURE, handle_, static_cast<GLsizei>(desc_.debugName.size()), desc_.debugName.c_str());
    }
}

uint64_t OpenGLTexture::GetBindlessHandle() const {
    return bindlessHandle_;
}

void OpenGLTexture::SetupSamplerState() {
    // AAA Strategy: Prevent texture incompleteness by correctly selecting filters for single-level textures.
    // Mipmap filters require valid mipmaps; using them on 1-level textures results in black pixels.
    bool isGBufferLike = desc_.format == Format::RGB32F || desc_.format == Format::RGBA32F ||
                        desc_.format == Format::RGB16F || desc_.format == Format::RGBA16F;
    bool isDepth = desc_.format == Format::D24_UNORM_S8_UINT || desc_.format == Format::D32_FLOAT_S8_UINT;

    GLint minFilter = (isGBufferLike || isDepth) ? GL_NEAREST : GL_LINEAR;
    if (desc_.mipLevels > 1 && !isDepth && !isGBufferLike) {
        minFilter = GL_LINEAR_MIPMAP_LINEAR;
    }
    GLint magFilter = (isGBufferLike || isDepth) ? GL_NEAREST : GL_LINEAR;

    glTextureParameteri(handle_, GL_TEXTURE_MIN_FILTER, minFilter);
    glTextureParameteri(handle_, GL_TEXTURE_MAG_FILTER, magFilter);
    glTextureParameteri(handle_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(handle_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target_ == GL_TEXTURE_CUBE_MAP) {
        glTextureParameteri(handle_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
}

void OpenGLTexture::ConvertFormat(Format format, GLenum& internalFormat, GLenum& baseFormat, GLenum& type) {
    switch (format) {
        case Format::RGBA8_UNORM:
            internalFormat = GL_RGBA8;
            baseFormat = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
            break;
        case Format::RGB32F:
            internalFormat = GL_RGB32F;
            baseFormat = GL_RGB;
            type = GL_FLOAT;
            break;
        case Format::RGBA32F:
            internalFormat = GL_RGBA32F;
            baseFormat = GL_RGBA;
            type = GL_FLOAT;
            break;
        case Format::RGBA16F:
            internalFormat = GL_RGBA16F;
            baseFormat = GL_RGBA;
            type = GL_HALF_FLOAT;
            break;
        case Format::RGB16F:
            internalFormat = GL_RGB16F;
            baseFormat = GL_RGB;
            type = GL_HALF_FLOAT;
            break;
        case Format::D24_UNORM_S8_UINT:
            internalFormat = GL_DEPTH24_STENCIL8;
            baseFormat = GL_DEPTH_STENCIL;
            type = GL_UNSIGNED_INT_24_8;
            break;
        case Format::D32_FLOAT_S8_UINT:
            internalFormat = GL_DEPTH32F_STENCIL8;
            baseFormat = GL_DEPTH_STENCIL;
            type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
            break;
        case Format::BC1:
            internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            baseFormat = GL_RGB;
            type = GL_UNSIGNED_BYTE;
            break;
        case Format::BC3:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            baseFormat = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
            break;
        case Format::BC5:
            internalFormat = GL_COMPRESSED_RG_RGTC2;
            baseFormat = GL_RG;
            type = GL_UNSIGNED_BYTE;
            break;
        default:
            throw std::runtime_error("Unsupported texture format");
    }
}

}
