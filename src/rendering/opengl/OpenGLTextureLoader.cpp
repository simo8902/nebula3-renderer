// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "OpenGLTextureLoader.h"
#include "OpenGLTexture.h"
#if defined(NDEVC_HAS_DIRECTXTEX) && NDEVC_HAS_DIRECTXTEX
#include "DirectXTex/DirectXTex.h"
#endif
#include <fstream>
#include <iostream>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

namespace NDEVC::Graphics::OpenGL {

OpenGLTextureLoader::OpenGLTextureLoader() {
}

OpenGLTextureLoader::~OpenGLTextureLoader() {
}

std::shared_ptr<ITexture> OpenGLTextureLoader::LoadDDS(const std::string& path) {
#if !(defined(NDEVC_HAS_DIRECTXTEX) && NDEVC_HAS_DIRECTXTEX)
    std::cerr << "[TEXTURE][ERROR] DirectXTex support is disabled; cannot load DDS '" << path
              << "'. Enable NDEVC_HAS_DIRECTXTEX.\n";
    return nullptr;
#else
    std::string norm = path;
    if (norm.rfind("tex:", 0) == 0) norm = norm.substr(4);
    for (auto& ch : norm) if (ch == '/') ch = '\\';

    DirectX::ScratchImage image;
    DirectX::TexMetadata meta;
    HRESULT hr = DirectX::LoadFromDDSFile(
        std::wstring(norm.begin(), norm.end()).c_str(),
        DirectX::DDS_FLAGS_NONE,
        &meta, image
    );
    if (FAILED(hr)) {
        hr = DirectX::LoadFromDDSFile(
            std::wstring(norm.begin(), norm.end()).c_str(),
            DirectX::DDS_FLAGS_LEGACY_DWORD | DirectX::DDS_FLAGS_NO_LEGACY_EXPANSION,
            &meta, image
        );
    }
    if (FAILED(hr)) {
        std::ifstream f(norm, std::ios::binary | std::ios::ate);
        if (!f) {
            std::cerr << "[TEX ERR] Failed to open for memory load: " << norm << "\n";
            return nullptr;
        }
        std::streamsize sz = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(sz);
        if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
            std::cerr << "[TEX ERR] Failed to read bytes for memory load: " << norm << "\n";
            return nullptr;
        }
        hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(),
            DirectX::DDS_FLAGS_NONE, &meta, image);
        if (FAILED(hr)) {
            hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(),
                DirectX::DDS_FLAGS_LEGACY_DWORD | DirectX::DDS_FLAGS_NO_LEGACY_EXPANSION,
                &meta, image);
        }
    }
    if (FAILED(hr)) {
        std::cerr << "[TEX ERR] Failed to load DDS: " << norm << "\n";
        return nullptr;
    }

    if (meta.IsCubemap()) {
        GLenum glFormat = 0;
        switch (meta.format) {
            case DXGI_FORMAT_BC1_UNORM: glFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; break;
            case DXGI_FORMAT_BC2_UNORM: glFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
            case DXGI_FORMAT_BC3_UNORM: glFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
            case DXGI_FORMAT_BC1_UNORM_SRGB: glFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT; break;
            case DXGI_FORMAT_BC2_UNORM_SRGB: glFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT; break;
            case DXGI_FORMAT_BC3_UNORM_SRGB: glFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT; break;
            default: glFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; break;
        }

        TextureDesc texDesc;
        texDesc.width = meta.width;
        texDesc.height = meta.height;
        texDesc.type = TextureType::TextureCube;
        texDesc.isCubemap = true;
        texDesc.mipLevels = static_cast<uint32_t>(meta.mipLevels);
        auto texture = std::make_shared<OpenGLTexture>(texDesc);
        GLuint tex = *(GLuint*)texture->GetNativeHandle();
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

        const size_t mipLevels = (meta.mipLevels > 0) ? meta.mipLevels : 1;
        for (size_t face = 0; face < 6; ++face) {
            for (size_t level = 0; level < mipLevels; ++level) {
                const DirectX::Image* mip = image.GetImage(level, face, 0);
                if (!mip) {
                    if (level == 0) {
                        std::cerr << "[TEX ERR] Cubemap face has no base mip: " << norm << "\n";
                        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                        return nullptr;
                    }
                    break;
                }
                glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face,
                    (GLint)level, glFormat,
                    (GLsizei)mip->width, (GLsizei)mip->height, 0,
                    (GLsizei)mip->slicePitch, mip->pixels);
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                        mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        return texture;
    }

    const DirectX::Image* img = image.GetImage(0, 0, 0);
    if (!img) {
        std::cerr << "[TEX ERR] No image data: " << norm << "\n";
        return nullptr;
    }

    GLenum glFormat = 0;
    bool isSRGB = false;
    switch (meta.format) {
        case DXGI_FORMAT_BC1_UNORM:        glFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; break;
        case DXGI_FORMAT_BC1_UNORM_SRGB:   glFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT; isSRGB = true; break;
        case DXGI_FORMAT_BC2_UNORM:        glFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
        case DXGI_FORMAT_BC2_UNORM_SRGB:   glFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT; isSRGB = true; break;
        case DXGI_FORMAT_BC3_UNORM:        glFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
        case DXGI_FORMAT_BC3_UNORM_SRGB:   glFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT; isSRGB = true; break;
        default: glFormat = 0; break;
    }

    if (glFormat == 0) {
        DirectX::ScratchImage conv;
        const DirectX::Image* src = img;
        if (meta.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) isSRGB = true;
        if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            meta.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            HRESULT hr2 = DirectX::Decompress(*img, DXGI_FORMAT_R8G8B8A8_UNORM, conv);
            if (FAILED(hr2)) {
                std::cerr << "[TEX ERR] Decompress failed: " << norm << "\n";
                return nullptr;
            }
            src = conv.GetImage(0, 0, 0);
        }

        TextureDesc texDesc;
        texDesc.width = src->width;
        texDesc.height = src->height;
        texDesc.type = TextureType::Texture2D;
        texDesc.mipLevels = 1;
        auto texture = std::make_shared<OpenGLTexture>(texDesc);
        GLuint tex = *(GLuint*)texture->GetNativeHandle();
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, isSRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8,
            (GLsizei)src->width, (GLsizei)src->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, src->pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);

        return texture;
    } else {
        TextureDesc texDesc;
        texDesc.width = meta.width;
        texDesc.height = meta.height;
        texDesc.type = TextureType::Texture2D;
        texDesc.mipLevels = meta.mipLevels;
        auto texture = std::make_shared<OpenGLTexture>(texDesc);
        GLuint tex = *(GLuint*)texture->GetNativeHandle();
        glBindTexture(GL_TEXTURE_2D, tex);

        for (size_t level = 0; level < meta.mipLevels; ++level) {
            const DirectX::Image* mip = image.GetImage(level, 0, 0);
            if (!mip) {
                std::cerr << "[TEX ERR] Missing mip level " << level << " for '" << norm << "'\n";
                glBindTexture(GL_TEXTURE_2D, 0);
                return nullptr;
            }
            glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)level, glFormat,
                (GLsizei)mip->width, (GLsizei)mip->height, 0,
                (GLsizei)mip->slicePitch, mip->pixels);
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
            meta.mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glBindTexture(GL_TEXTURE_2D, 0);

        return texture;
    }
#endif
}

std::shared_ptr<ITexture> OpenGLTextureLoader::CreateFallbackTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = Format::RGBA8_UNORM;

    uint8_t data[] = {r, g, b, a};
    auto texture = std::make_shared<OpenGLTexture>(desc);
    GLuint tex = *(GLuint*)texture->GetNativeHandle();
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

std::shared_ptr<ITexture> OpenGLTextureLoader::CreateWhiteTexture() {
    return CreateFallbackTexture(255, 255, 255, 255);
}

std::shared_ptr<ITexture> OpenGLTextureLoader::CreateBlackTexture() {
    return CreateFallbackTexture(0, 0, 0, 255);
}

std::shared_ptr<ITexture> OpenGLTextureLoader::CreateNormalTexture() {
    return CreateFallbackTexture(128, 128, 255, 255);
}

}
