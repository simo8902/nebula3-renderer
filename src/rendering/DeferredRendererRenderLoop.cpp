// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"
#include "Core/GlobalState.h"
#include "Core/Logger.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "Rendering/Rendering.h"

extern bool gEnableGLErrorChecking;

namespace {

bool NoPresentWhenViewportDisabled() {
    static const bool enabled = ReadEnvToggle("NDEVC_NO_PRESENT_WHEN_VIEWPORT_DISABLED");
    return enabled;
}

void checkGLError(const char* label) {
    if (!gEnableGLErrorChecking) return;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::stringstream ss;
        ss << "OpenGL Error at " << label << ": 0x" << std::hex << err << std::dec;
        switch (err) {
            case GL_INVALID_ENUM: ss << " (INVALID_ENUM)"; break;
            case GL_INVALID_VALUE: ss << " (INVALID_VALUE)"; break;
            case GL_INVALID_OPERATION: ss << " (INVALID_OPERATION)"; break;
            case GL_OUT_OF_MEMORY: ss << " (OUT_OF_MEMORY)"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: ss << " (INVALID_FRAMEBUFFER_OPERATION)"; break;
        }
        NC::LOGGING::Warn(NC::LOGGING::Category::Graphics, ss.str());
    }
}

} // namespace

// ── renderSingleFrame ─────────────────────────────────────────────────────────
// Runs on the MAIN thread. Handles scene tick and FrameGate handoff only.
// All GL work is delegated to RenderThreadLoop().
//
// Pipeline:
//   Main:   WaitFrameDone(N-1) → Tick(N) → PostFrame(N) → WaitDrawlistsDone(N) → return
//   Render: WaitFrame(N) → PrepareDrawLists(N) → SignalDrawlistsDone(N) → GL+Swap → SignalFrameDone(N)
//
// Overlap: Tick(N+1) [CPU, main] runs concurrently with GL passes + SwapBuffers(N) [GPU, render].
void DeferredRenderer::renderSingleFrame() {
    // ── Phase 5: GL context transfer on first call ────────────────────────
    // After Initialize() all GL setup is done on the main thread. Hand the
    // context to the render thread on the very first frame.
    if (!contextTransferred_) {
        if (window_) window_->ReleaseContext();
        contextTransferred_ = true;
        renderThread_ = std::jthread([this] { RenderThreadLoop(); });
    }

    UpdateCameraFixed();

    try {
        const double currentFrame = GetTimeSeconds();

        frameProfile_ = FrameProfile{};
        using ProfileClock = std::chrono::high_resolution_clock;
        const auto profFrameStart = ProfileClock::now();
        auto profLast = profFrameStart;
        auto profElapsed = [&]() -> double {
            auto now = ProfileClock::now();
            double ms = std::chrono::duration<double, std::milli>(now - profLast).count();
            profLast = now;
            return ms;
        };
        auto tickFpsCounter = [&]() {
            ++fpsCounterFrames_;
            if (fpsCounterTime_ == 0.0) fpsCounterTime_ = currentFrame;
            if (currentFrame - fpsCounterTime_ >= 0.5) {
                displayFps_ = (float)(fpsCounterFrames_ / (currentFrame - fpsCounterTime_));
                fpsCounterFrames_ = 0;
                fpsCounterTime_ = currentFrame;
            }
        };

        // ── Input routing (no GL) ─────────────────────────────────────────
        {
            bool allow = editorHosted_ || !editorViewportInputRouting_;
            if (!allow && editorModeEnabled_ && editorViewportInputRouting_) {
                if (sceneViewportValid_) {
                    double cx = 0.0, cy = 0.0;
                    if (window_) window_->GetCursorPos(cx, cy);
                    allow = IsSceneViewportInputActive() || IsSceneViewportPointerInside(cx, cy);
                } else {
                    allow = true;
                }
            }
            pendingAllowViewportInput_ = allow;
        }
        frameProfile_.inputPoll = profElapsed();

        HandleModeSwitchInput();
        UpdateDirtyFrameState();
        frameProfile_.renderMode = static_cast<int>(renderMode_);

        // ── Stage pending flags for the render thread ─────────────────────
        // Written here (before PostFrame) and read by render thread after WaitFrame.
        // FrameGate provides the happens-before ordering — no atomics needed.
        pendingViewportDisabled_ = viewportDisabled_;

        // ── Wait for render thread to finish previous frame ───────────────
        // Guarantees scene internals are not being read by PrepareDrawLists(N-1)
        // when Tick(N) writes to them below.
        gate_.WaitFrameDone();

        // ── Scene tick — CPU only, no GL ──────────────────────────────────
        const bool drawListsWereDirty = scene_->IsDrawListsDirty();
        if (drawListsWereDirty) dirtyFlag_ = true;

        bool droppedMapProcessed = false;
        if (!pendingViewportDisabled_) {
            if (ProcessPendingDroppedMapLoad(currentFrame)) {
                droppedMapProcessed = true;
            }
        }

        dirtyFrameSkippedLast_ = false;
        pendingDirtySkip_ = activePolicy_.dirtyRenderingEnabled && !dirtyFlag_ && !droppedMapProcessed;

        if (!pendingViewportDisabled_ && !pendingDirtySkip_ && !droppedMapProcessed) {
            frameArenas_[arenaFlip_].Reset();
            scene_->Tick(deltaTime, camera);
            lastSnapshot_ = scene_->BuildSnapshot(frameArenas_[arenaFlip_], camera);
            frameProfile_.sceneTick = profElapsed();

            dirtyFlag_ = drawListsWereDirty; // re-assert if mutation was detected
            NC::LOGGING::Log("[DIRTY] TICK drawListsDirty=", (drawListsWereDirty ? 1 : 0));
        } else {
            profElapsed();
            if (!pendingDirtySkip_) dirtyFlag_ = false;
        }

        // ── Wake the render thread ────────────────────────────────────────
        if (editorHosted_) {
            RenderFrameGL(true);
        } else {
            gate_.PostFrame();
            gate_.WaitDrawlistsDone();
        }

        // Flip arena: the slot just used is stable until the render thread
        // signals FrameDone, which we wait for at the top of the next frame.
        arenaFlip_ ^= 1;

        // ── Profile + FPS ─────────────────────────────────────────────────
        frameProfile_.frameTotal = std::chrono::duration<double, std::milli>(
            ProfileClock::now() - profFrameStart).count();
        tickFpsCounter();
        UpdateFrameProfileLogs();
        ++profileFrameCounter_;

        // ── CPU frame cap (main thread — controls tick pacing) ────────────
        {
            const int policyFps = editorHosted_ ? 0 : activePolicy_.targetFps;
            if (policyFps > 0) {
                const double targetSec = 1.0 / static_cast<double>(policyFps);
                using Dur = std::chrono::duration<double>;
                const auto deadline = profFrameStart +
                    std::chrono::duration_cast<ProfileClock::duration>(Dur(targetSec - 0.001));
                std::this_thread::sleep_until(deadline);
            }
        }
    } catch (...) {
        gate_.Stop();
        NC::LOGGING::Error(NC::LOGGING::Category::Graphics,
                           "renderSingleFrame (main thread) failed with unknown exception");
        throw;
    }
}

// ── RenderThreadLoop ──────────────────────────────────────────────────────────
// Runs on the dedicated RENDER thread. Owns the GL context for its lifetime.
// Woken each frame by FrameGate::PostFrame() from the main thread.
void DeferredRenderer::RenderThreadLoop() {
    if (window_) window_->MakeCurrent();
    NC::LOGGING::Log("[RENDER_THREAD] Started — GL context acquired");

    while (gate_.WaitFrame()) {
        RenderFrameGL(false);
        gate_.SignalFrameDone();
    }

    if (window_) window_->ReleaseContext();
    NC::LOGGING::Log("[RENDER_THREAD] Exiting — GL context released");
}

void DeferredRenderer::RenderFrameGL(bool synchronous) {
    try {
        // ── Fast path: viewport disabled ──────────────────────────────
        if (pendingViewportDisabled_) {
            if (!synchronous) gate_.SignalDrawlistsDone();
            if (!NoPresentWhenViewportDisabled()) {
                window_->SwapBuffers();
            } else {
                static double lastDisabledPresent = 0.0;
                constexpr double kInterval = 1.0 / 120.0;
                const double now = GetTimeSeconds();
                if (lastDisabledPresent <= 0.0 || (now - lastDisabledPresent) >= kInterval) {
                    window_->SwapBuffers();
                    lastDisabledPresent = now;
                }
            }
            return;
        }

        // ── Re-assert reversed-Z GL state every frame ─────────────────
        if (device_ && window_)
            device_->SetDefaultFramebuffer(window_->GetDefaultFramebuffer());
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        glClearDepth(0.0f);
        glDepthFunc(GL_GREATER);

        if (shaderManager) shaderManager->ProcessPendingReloads();

        if (!synchronous) WaitForFrameFence();

        // ── Dirty-frame skip ──────────────────────────────────────────
        if (pendingDirtySkip_) {
            dirtyFrameSkippedLast_ = true;
            frameProfile_.dirtyFrameSkipped = 1;
            // Signal main before scene reads so it can start next Tick.
            // scene_->currentMap / GetAuthoredEntities() are read-only here
            // and change only during explicit load operations, not during Tick.
            if (!synchronous) gate_.SignalDrawlistsDone();
            if (scene_->currentMap == nullptr && scene_->GetAuthoredEntities().empty()) {
                DrawEditorPreviewViewport();
            }
            window_->SwapBuffers();
            return;
        }
        frameProfile_.dirtyFrameSkipped = 0;

        if (width <= 0 || height <= 0) {
            if (!synchronous) gate_.SignalDrawlistsDone();
            return;
        }

        frameDrawCalls_ = 0;

        ApplyRenderPassBaseline(true);
        if (device_) device_->InvalidateRenderStateCache();

        // ── Execute render passes ─────────────────────────────────────
        // The onDrawlistsReady lambda fires inside StandardGBufferPass::Execute()
        // immediately after PrepareDrawLists() returns, unblocking the main
        // thread so Tick(N+1) can start while we finish the remaining GL passes.
        ExecuteModernRenderPasses(true, [this, synchronous]() { 
            if (!synchronous) gate_.SignalDrawlistsDone(); 
        });

        const bool drewEditorPreview =
            scene_->currentMap == nullptr && scene_->GetAuthoredEntities().empty();
        if (drewEditorPreview) {
            DrawEditorPreviewViewport();
        }

        HandleFrameDebugInput(pendingAllowViewportInput_);
        HandleViewportPicking(pendingAllowViewportInput_);

        if (drewEditorPreview || !activePolicy_.limitEditorPanelRefresh || panelLastUpdateTime_ != 0.0)
            window_->SwapBuffers();

        if (!synchronous) CreateFrameFence();

        NC::LOGGING::Log("[DIRTY] RENDER_DONE renderPassesMs=", frameProfile_.editorClear,
                         " swapMs=", frameProfile_.swapBuffers,
                         " drawCalls=", frameDrawCalls_);

    } catch (...) {
        NC::LOGGING::Error(NC::LOGGING::Category::Graphics,
                           "RenderFrameGL failed with unknown exception");
        // Prevent the main thread from deadlocking on either gate barrier.
        if (!synchronous) gate_.SignalDrawlistsDone();
    }
}