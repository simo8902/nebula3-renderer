// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_RENDER_STATE_H
#define NDEVC_GL_RENDER_STATE_H
#include "../abstract/IRenderState.h"

namespace NDEVC::Graphics::OpenGL {

class OpenGLRenderState : public IRenderState {
public:
    OpenGLRenderState(const RenderStateDesc& desc);
    ~OpenGLRenderState() = default;

    const RenderStateDesc& GetDesc() const override { return desc_; }

private:
    RenderStateDesc desc_;
};

}
#endif