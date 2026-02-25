// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IFRAMEBUFFER_H
#define NDEVC_IFRAMEBUFFER_H

#include <cstdint>
#include <memory>
#include <vector>
#include "ITexture.h"

namespace NDEVC::Graphics {

struct FramebufferAttachment {
    std::shared_ptr<ITexture> texture;
    uint32_t mipLevel = 0;
};

struct FramebufferDesc {
    std::vector<FramebufferAttachment> colorAttachments;
    FramebufferAttachment depthStencilAttachment;
    uint32_t width = 0;
    uint32_t height = 0;
};

class IFramebuffer {
public:
    virtual ~IFramebuffer() = default;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual void* GetNativeHandle() const = 0;
};

}
#endif