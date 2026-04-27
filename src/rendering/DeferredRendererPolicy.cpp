// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.
#include "Rendering/DeferredRenderer.h"
#include "Core/Logger.h"
#include <cstdio>
#include <cstring>

FramePolicy BuildPolicy(RenderMode mode) {
    FramePolicy p{};
    switch (mode) {
    case RenderMode::Work:
        p.editorLayoutEnabled     = true;
        p.viewportOnlyUI          = false;
        p.dirtyRenderingEnabled   = false;
        p.limitEditorPanelRefresh = true;
        p.vsyncEnabled            = true;
        p.targetFps               = 60;
        break;
    case RenderMode::Play:
        p.editorLayoutEnabled     = true;
        p.viewportOnlyUI          = false;
        p.dirtyRenderingEnabled   = false;
        p.limitEditorPanelRefresh = false;
        p.vsyncEnabled            = false;
        p.targetFps               = 0;
        break;
    }
    return p;
}

void DeferredRenderer::ApplyPolicy(const FramePolicy& policy) {
    activePolicy_ = policy;
    editorModeEnabled_ = policy.editorLayoutEnabled;
    dirtyFlag_ = true;

    if (window_) {
        window_->SetSwapInterval(policy.vsyncEnabled ? 1 : 0);
    }
}

void DeferredRenderer::LogPolicySnapshot(const char* reason) const {
    const char* modeName = (renderMode_ == RenderMode::Work) ? "Work" : "Play";
    const FramePolicy& p = activePolicy_;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "[MODE] %s mode=%s editorLayout=%d viewportOnly=%d "
        "dirtyRendering=%d targetFps=%d(%s)",
        reason ? reason : "snapshot",
        modeName,
        p.editorLayoutEnabled ? 1 : 0,
        p.viewportOnlyUI ? 1 : 0,
        p.dirtyRenderingEnabled ? 1 : 0,
        p.targetFps,
        (p.targetFps <= 0) ? "uncapped" : "capped");
    NC::LOGGING::Log(buf);
}

void DeferredRenderer::MarkDirty() {
    dirtyFlag_ = true;
}

bool DeferredRenderer::ConsumeDirty() {
    if (dirtyFlag_) {
        dirtyFlag_ = false;
        return true;
    }
    return false;
}

RenderMode DeferredRenderer::GetRenderMode() const {
    return renderMode_;
}

void DeferredRenderer::SetRenderMode(RenderMode mode) {
    if (mode == renderMode_) return;
    renderMode_ = mode;
    FramePolicy policy = BuildPolicy(mode);
    if (editorHosted_) {
        policy.editorLayoutEnabled    = true;
    }
    ApplyPolicy(policy);
    LogPolicySnapshot("SWITCH");
}

void DeferredRenderer::SetEditorHosted(bool hosted) {
    if (editorHosted_ == hosted) return;
    editorHosted_ = hosted;
    FramePolicy policy = BuildPolicy(renderMode_);
    if (editorHosted_) {
        policy.editorLayoutEnabled    = true;
    }
    ApplyPolicy(policy);
    LogPolicySnapshot(editorHosted_ ? "EDITOR_HOST_ON" : "EDITOR_HOST_OFF");
}
