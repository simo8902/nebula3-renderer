// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERCONTEXT_H
#define NDEVC_RENDERCONTEXT_H

#include <cstdint>
#include <functional>

#include "glm.hpp"
#include "Rendering/Camera.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Rendering/Interfaces/IShaderManager.h"
#include "Rendering/Interfaces/ISampler.h"
#include "Rendering/Interfaces/IRenderState.h"
#include "Rendering/RenderResourceRegistry.h"
#include "Engine/SceneManager.h"

// ── FrameProfile ──────────────────────────────────────────────────────────
struct FrameProfile {
    double inputPoll          = 0.0;
    double shaderReload       = 0.0;
    double sceneTick          = 0.0;
    double prePassSetup       = 0.0;
    double editorClear        = 0.0;
    double swapBuffers        = 0.0;
    double frameTotal         = 0.0;
    double fenceWait          = 0.0;
    double fps                = 0.0;
    int    renderMode         = 0;
    int    dirtyFrameSkipped  = 0;
    float  panelUpdateHzEffective = 0.0f;
};

// ── FrameFlags ────────────────────────────────────────────────────────────
struct FrameFlags {
    bool animationsDisabled      = false;
    bool faceCullingDisabled     = false;
};

// ── RenderContext ─────────────────────────────────────────────────────────
// Per-frame state bundle passed to every IRenderPass::Execute().
struct RenderContext {
    // ── Camera / transform data ───────────────────────────────────────
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::mat4 invView{1.0f};
    glm::mat4 invProjection{1.0f};
    glm::vec3 cameraPos{0.0f};
    glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
    const Camera* camera = nullptr;
    float cameraNear = 0.1f;
    float cameraFar = 1.0f;

    // ── Timing ───────────────────────────────────────────────────────
    double frameTime = 0.0;
    double deltaTime = 0.0;

    // ── Viewport ─────────────────────────────────────────────────────
    int viewportWidth  = 0;
    int viewportHeight = 0;

    // ── Scene ─────────────────────────────────────────────────────────
    SceneManager* scene = nullptr;

    // ── Device + shaders ─────────────────────────────────────────────
    NDEVC::Graphics::IGraphicsDevice*  device        = nullptr;
    NDEVC::Graphics::IShaderManager*   shaderManager = nullptr;

    // ── Shared GPU resources ─────────────────────────────────────────
    uint32_t whiteTex       = 0;
    uint32_t blackTex       = 0;
    uint32_t normalTex      = 0;
    uint32_t blackCubeTex   = 0;
    uint32_t gSamplerRepeat = 0;
    uint32_t gSamplerClamp  = 0;
    uint32_t gSamplerShadow = 0;
    uint32_t screenVAO      = 0;

    NDEVC::Graphics::ISampler* samplerRepeat = nullptr;
    NDEVC::Graphics::ISampler* samplerClamp  = nullptr;
    NDEVC::Graphics::ISampler* samplerShadow = nullptr;

    // ── Named resource registry ───────────────────────────────────────
    RenderResourceRegistry* resources = nullptr;

    // ── Cached render states ─────────────────────────────────────────
    NDEVC::Graphics::IRenderState* cachedBlitState = nullptr;

    // ── Per-frame flags ────────────────────────────────────────────────
    FrameFlags flags{};

    // ── Render options ────────────────────────────────────────────────
    bool optRenderLOG      = false;
    bool optCheckGLErrors  = false;
    bool editorModeEnabled = false;

    // ── UI callback ───────────────────────────────────────────────────
    std::function<void()> uiRenderCallback;

    // ── Phase 5 pipeline signal ───────────────────────────────────────
    // Called immediately after PrepareDrawLists() in the first render pass.
    // The main thread blocks on FrameGate::WaitDrawlistsDone() until this fires,
    // then is free to start Tick for the next frame.
    std::function<void()> onDrawlistsReady;

    // ── Frame draw-call counter + profiling ───────────────────────────
    int*          frameDrawCalls = nullptr;
    FrameProfile* profile        = nullptr;
};

// ── Inline render helpers ─────────────────────────────────────────────────
// Equivalent to DeferredRenderer::bindTexture / bindSampler but accessible
// from all pass translation units.

#include "glad/glad.h"

inline void RC_BindTexture(uint32_t slot, uint32_t texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
}

inline void RC_BindTextureCube(uint32_t slot, uint32_t texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
}

inline void RC_BindSampler(const RenderContext& ctx, uint32_t slot, uint32_t sampler) {
    if (ctx.device) {
        if (sampler == 0) { ctx.device->BindSampler(nullptr, slot); return; }
        if (sampler == ctx.gSamplerRepeat && ctx.samplerRepeat) {
            ctx.device->BindSampler(ctx.samplerRepeat, slot); return;
        }
        if (sampler == ctx.gSamplerClamp && ctx.samplerClamp) {
            ctx.device->BindSampler(ctx.samplerClamp, slot); return;
        }
        if (sampler == ctx.gSamplerShadow && ctx.samplerShadow) {
            ctx.device->BindSampler(ctx.samplerShadow, slot); return;
        }
    }
    glBindSampler(slot, sampler);
}

inline void RC_CheckGLError(const RenderContext& ctx, const char* label) {
    if (!ctx.optCheckGLErrors) return;
    extern bool gEnableGLErrorChecking;
    if (!gEnableGLErrorChecking) return;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        if (label && label[0])
            fprintf(stderr, "[GL] Error at %s: 0x%x\n", label, err);
        else
            fprintf(stderr, "[GL] Error: 0x%x\n", err);
    }
}

#endif
