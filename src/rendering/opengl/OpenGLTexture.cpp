// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "OpenGLTexture.h"
#include <stdexcept>

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
    glGenTextures(1, &handle_);

    if (desc_.type == TextureType::TextureCube) {
        target_ = GL_TEXTURE_CUBE_MAP;
    } else {
        target_ = GL_TEXTURE_2D;
    }

    glBindTexture(target_, handle_);

    GLenum internalFormat, baseFormat, type;
    ConvertFormat(desc_.format, internalFormat, baseFormat, type);

    if (target_ == GL_TEXTURE_2D) {
        glTexImage2D(target_, 0, internalFormat, desc_.width, desc_.height, 0, baseFormat, type, nullptr);
    } else if (target_ == GL_TEXTURE_CUBE_MAP) {
        for (int i = 0; i < 6; ++i) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, desc_.width, desc_.height, 0, baseFormat, type, nullptr);
        }
    }

    SetupSamplerState();
}

void OpenGLTexture::SetupSamplerState() {
    bool isGBufferLike = desc_.format == Format::RGB32F || desc_.format == Format::RGBA32F ||
                        desc_.format == Format::RGB16F || desc_.format == Format::RGBA16F;
    bool isDepth = desc_.format == Format::D24_UNORM_S8_UINT || desc_.format == Format::D32_FLOAT_S8_UINT;

    GLint filter = (isGBufferLike || isDepth) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(target_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target_ == GL_TEXTURE_CUBE_MAP) {
        glTexParameteri(target_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
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
          //  internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            baseFormat = GL_RGB;
            type = GL_UNSIGNED_BYTE;
            break;
        case Format::BC3:
           // internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
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
