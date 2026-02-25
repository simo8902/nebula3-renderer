// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IBUFFER_H
#define NDEVC_IBUFFER_H

#include <cstdint>
#include "RenderingTypes.h"

namespace NDEVC::Graphics {

struct BufferDesc {
    BufferType type = BufferType::Vertex;
    uint32_t size = 0;
    const void* initialData = nullptr;
};

class IBuffer {
public:
    virtual ~IBuffer() = default;
    virtual uint32_t GetSize() const = 0;
    virtual BufferType GetType() const = 0;
    virtual void* GetNativeHandle() const = 0;
    virtual void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
};

}

#endif