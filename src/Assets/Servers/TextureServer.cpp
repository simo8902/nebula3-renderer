// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Assets/Servers/TextureServer.h"
#include "Core/Logger.h"
#include "Core/VFS.h"
#if defined(NDEVC_HAS_DIRECTXTEX) && NDEVC_HAS_DIRECTXTEX
#include "DirectXTex/DirectXTex.h"
#endif

namespace {

#if defined(NDEVC_HAS_DIRECTXTEX) && NDEVC_HAS_DIRECTXTEX
bool HasTransparentAlpha(const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta) {
    if (meta.IsCubemap()) {
        return false;
    }
    if (!DirectX::HasAlpha(meta.format)) {
        return false;
    }
    if (meta.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_OPAQUE) {
        return false;
    }

    const DirectX::Image* base = image.GetImage(0, 0, 0);
    if (!base) {
        return false;
    }

    DirectX::ScratchImage converted;
    const DirectX::Image* src = base;
    if (base->format != DXGI_FORMAT_R8G8B8A8_UNORM && base->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        HRESULT hr = DirectX::Decompress(*base, DXGI_FORMAT_R8G8B8A8_UNORM, converted);
        if (FAILED(hr)) {
            // If decompression fails on an alpha-capable format, assume transparency may be present.
            return true;
        }
        src = converted.GetImage(0, 0, 0);
        if (!src) {
            return false;
        }
    }

    const uint8_t kOpaqueThreshold = 250;
    for (size_t y = 0; y < src->height; ++y) {
        const uint8_t* row = src->pixels + y * src->rowPitch;
        for (size_t x = 0; x < src->width; ++x) {
            const uint8_t alpha = row[x * 4 + 3];
            if (alpha < kOpaqueThreshold) {
                return true;
            }
        }
    }
    return false;
}
#endif

} // namespace

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

GLuint TextureServer::LoadDDS(const std::string &path) {
#if !(defined(NDEVC_HAS_DIRECTXTEX) && NDEVC_HAS_DIRECTXTEX)
    NC::LOGGING::Error("[TEX] DirectXTex support disabled; cannot load DDS path=", path);
    gTexHasTransparency[path] = false;
    return 0;
#else
    auto itc = gTexCache.find(path);
    if (itc != gTexCache.end()) {
        return itc->second;
    }

    std::string norm = path;
    if (norm.rfind("tex:", 0) == 0) norm = norm.substr(4);
    for (auto& ch : norm) if (ch == '/') ch = '\\';

    DirectX::ScratchImage image;
    DirectX::TexMetadata meta;
    HRESULT hr = E_FAIL;

    // VFS first — serve directly from the memory-mapped NDPK blob.
    const NC::VFS::View vfsView = NC::VFS::Instance().Read(norm);
    if (vfsView.valid()) {
        hr = DirectX::LoadFromDDSMemory(vfsView.data, vfsView.size,
                                        DirectX::DDS_FLAGS_NONE, &meta, image);
        if (FAILED(hr)) {
            hr = DirectX::LoadFromDDSMemory(vfsView.data, vfsView.size,
                                            DirectX::DDS_FLAGS_LEGACY_DWORD |
                                            DirectX::DDS_FLAGS_NO_LEGACY_EXPANSION,
                                            &meta, image);
        }
    } else {
        // When a package is mounted, assets must come from VFS — no disk fallback.
        if (NC::VFS::Instance().IsMounted()) {
            NC::LOGGING::Error("[TEX] Asset not found in package: ", norm);
            gTexHasTransparency[path] = false;
            return 0;
        }
        hr = DirectX::LoadFromDDSFile(
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
                NC::LOGGING::Error("[TEX] Failed to open for memory load: ", norm);
                return 0;
            }
            std::streamsize sz = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes(sz);
            if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
                NC::LOGGING::Error("[TEX] Failed to read bytes for memory load: ", norm);
                return 0;
            }
            hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(),
                DirectX::DDS_FLAGS_NONE, &meta, image);
            if (FAILED(hr)) {
                hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(),
                    DirectX::DDS_FLAGS_LEGACY_DWORD | DirectX::DDS_FLAGS_NO_LEGACY_EXPANSION,
                    &meta, image);
            }
        }
    }
    if (FAILED(hr)) {
        NC::LOGGING::Error("[TEX] Failed to load DDS: ", norm);
        gTexHasTransparency[path] = false;
        return 0;
    }

    gTexHasTransparency[path] = HasTransparentAlpha(image, meta);

    /*
    std::cout << "[DDS INFO] " << norm
        << " | IsCubemap=" << (meta.IsCubemap() ? "true" : "false")
        << " | ArraySize=" << meta.arraySize
        << " | Mips=" << meta.mipLevels
        << " | Format=" << meta.format
        << " | " << meta.width << "x" << meta.height << "\n";*/

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

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

        const size_t mipLevels = (meta.mipLevels > 0) ? meta.mipLevels : 1;
        for (size_t face = 0; face < 6; ++face) {
            for (size_t level = 0; level < mipLevels; ++level) {
                const DirectX::Image* mip = image.GetImage(level, face, 0);
                if (!mip) {
                    if (level == 0) {
                        NC::LOGGING::Error("[TEX] Cubemap face has no base mip: ", norm);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                        glDeleteTextures(1, &tex);
                        return 0;
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
        gTexCache[path] = tex;
        return tex;
    }

    const DirectX::Image* img = image.GetImage(0, 0, 0);
    if (!img) {
        NC::LOGGING::Error("[TEX] No image data: ", norm);
        return 0;
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
                NC::LOGGING::Error("[TEX] Decompress failed: ", norm);
                return 0;
            }
            src = conv.GetImage(0, 0, 0);
            }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, isSRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8,
            (GLsizei)src->width, (GLsizei)src->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, src->pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);

        gTexCache[path] = tex;
        return tex;
    }
    else {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);

        for (size_t level = 0; level < meta.mipLevels; ++level) {
            const DirectX::Image* mip = image.GetImage(level, 0, 0);
            if (!mip) {
                NC::LOGGING::Error("[TEX] Missing mip level ", level, " for ", norm);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDeleteTextures(1, &tex);
                return 0;
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
        gTexCache[path] = tex;
        return tex;
    }
#endif
}
