// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ISAMPLER_H
#define NDEVC_ISAMPLER_H

#include "Rendering/Interfaces/RenderingTypes.h"

namespace NDEVC::Graphics {

struct SamplerDesc {
    SamplerFilter minFilter = SamplerFilter::Linear;
    SamplerFilter magFilter = SamplerFilter::Linear;
    SamplerWrap wrapS = SamplerWrap::Repeat;
    SamplerWrap wrapT = SamplerWrap::Repeat;
    SamplerWrap wrapR = SamplerWrap::Repeat;
    CompareFunc compareFunc = CompareFunc::Always;
    bool useCompare = false;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    float lodBias = 0.0f;
};

class ISampler {
public:
    virtual ~ISampler() = default;
    virtual const SamplerDesc& GetDesc() const = 0;
    virtual void* GetNativeHandle() const = 0;
};

}
#endif