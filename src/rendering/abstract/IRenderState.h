// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_IRENDERSTATE_H
#define NDEVC_IRENDERSTATE_H
#include "RenderingTypes.h"

namespace NDEVC::Graphics {

class IRenderState {
public:
    virtual ~IRenderState() = default;
    virtual const RenderStateDesc& GetDesc() const = 0;
};

}
#endif