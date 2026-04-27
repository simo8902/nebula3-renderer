// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ITEXTURE_H
#define NDEVC_ITEXTURE_H

#include <cstdint>
#include "Rendering/Interfaces/RenderingTypes.h"

#include <string>

namespace NDEVC::Graphics {

struct TextureDesc {
    std::string debugName;
    TextureType type = TextureType::Texture2D;
    Format format = Format::RGBA8_UNORM;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    bool isCubemap = false;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual Format GetFormat() const = 0;
    virtual TextureType GetType() const = 0;
    virtual void* GetNativeHandle() const = 0;
    virtual uint64_t GetBindlessHandle() const { return 0; }
};

}
#endif