// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Rendering.h"
#include "Rendering/DeferredRenderer.h"
#include "Core/GlobalState.h"

#include "glad/glad.h"

namespace {
bool AnimationsDisabledForContext() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ANIMATIONS");
    return disabled;
}

bool FaceCullingDisabledForContext() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_FACE_CULLING");
    return disabled;
}
} // namespace

FrameFlags DeferredRenderer::BuildFrameFlags() const {
    FrameFlags flags{};
    flags.animationsDisabled     = AnimationsDisabledForContext();
    flags.faceCullingDisabled    = FaceCullingDisabledForContext();
    return flags;
}

RenderContext DeferredRenderer::BuildRenderContext() {
    RenderContext ctx;
    ctx.scene         = scene_;
    ctx.device        = device_.get();
    ctx.shaderManager = shaderManager.get();

    ctx.cameraPos     = camera.getPosition();
    ctx.cameraForward = camera.getForward();
    ctx.camera        = &camera;
    ctx.cameraNear    = camera.getNearPlane();
    ctx.cameraFar     = camera.getFarPlane();
    ctx.view          = camera.getViewMatrix();
    ctx.projection    = camera.getProjectionMatrix();
    ctx.viewProjection = ctx.projection * ctx.view;
    ctx.viewportWidth  = width;
    ctx.viewportHeight = height;

    ctx.whiteTex       = static_cast<uint32_t>(static_cast<GLuint>(whiteTex));
    ctx.blackTex       = static_cast<uint32_t>(static_cast<GLuint>(blackTex));
    ctx.normalTex      = static_cast<uint32_t>(static_cast<GLuint>(normalTex));
    ctx.blackCubeTex   = static_cast<uint32_t>(static_cast<GLuint>(blackCubeTex));
    ctx.gSamplerRepeat = static_cast<uint32_t>(static_cast<GLuint>(gSamplerRepeat));
    ctx.gSamplerClamp  = static_cast<uint32_t>(static_cast<GLuint>(gSamplerClamp));
    ctx.gSamplerShadow = static_cast<uint32_t>(static_cast<GLuint>(gSamplerShadow));
    ctx.screenVAO      = static_cast<uint32_t>(static_cast<GLuint>(screenVAO));

    ctx.samplerRepeat = samplerRepeat_abstracted.get();
    ctx.samplerClamp  = samplerClamp_abstracted.get();
    ctx.samplerShadow = samplerShadow_abstracted.get();

    ctx.resources      = renderResources_.get();
    ctx.cachedBlitState = cachedBlitState_.get();

    ctx.flags          = BuildFrameFlags();
    ctx.optRenderLOG   = optRenderLOG;
    ctx.optCheckGLErrors = optCheckGLErrors;
    ctx.editorModeEnabled = editorModeEnabled_;
    ctx.uiRenderCallback  = {};

    ctx.frameDrawCalls = &frameDrawCalls_;
    ctx.profile        = &frameProfile_;

    return ctx;
}

void DeferredRenderer::ApplyRenderContextResults(const RenderContext& ctx) {
}
