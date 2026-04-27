// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Rendering.h"
#include "Rendering/DeferredRenderer.h"
#include "Assets/Map/MapHeader.h"
#include "Assets/Servers/MeshServer.h"
#include "Core/GlobalState.h"
#include "Core/Logger.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

uint64_t ReadFrameLoopEnvUint64(const char* name, uint64_t fallbackValue) {
    if (!name || !name[0]) return fallbackValue;
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return fallbackValue;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    const bool ok = end != value && end && *end == '\0';
    std::free(value);
    return ok ? static_cast<uint64_t>(parsed) : fallbackValue;
#else
    const char* value = std::getenv(name);
    if (!value || !value[0]) return fallbackValue;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    return (end != value && end && *end == '\0') ? static_cast<uint64_t>(parsed) : fallbackValue;
#endif
}

bool FrameLoopAnimationsDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ANIMATIONS");
    return disabled;
}

bool FrameLoopFaceCullingDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_FACE_CULLING");
    return disabled;
}

bool FrameLoopFenceWaitEnabled() {
    static const bool enabled = ReadEnvToggle("NDEVC_ENABLE_FRAME_FENCE_WAIT");
    return enabled;
}

uint64_t FrameLoopFenceWaitBudgetNs() {
    static const uint64_t budgetNs = ReadFrameLoopEnvUint64("NDEVC_FRAME_FENCE_WAIT_NS", 1000000ull);
    return budgetNs;
}

bool FrameLoopCameraTraceEnabled() {
    const char* value = std::getenv("NDEVC_CAMERA_TRACE");
    return value == nullptr || value[0] != '0';
}

const char* FrameLoopEventModeText(bool enabled) {
    return enabled ? "event-filtered" : "all";
}

void FrameLoopDebugPrintNearestInstances(const MapData* map, const glm::vec3& target, size_t maxResults) {
    if (!map) return;

    struct NearestInstance {
        size_t index;
        float distSq;
    };

    std::vector<NearestInstance> nearest;
    nearest.reserve(map->instances.size());

    for (size_t i = 0; i < map->instances.size(); ++i) {
        const auto& inst = map->instances[i];
        const glm::vec3 p(inst.pos.x, inst.pos.y, inst.pos.z);
        const glm::vec3 d = p - target;
        nearest.push_back({i, glm::dot(d, d)});
    }

    if (nearest.empty()) {
        NC::LOGGING::Debug(NC::LOGGING::Category::Graphics, "No instances in map");
        return;
    }

    const size_t count = std::min(maxResults, nearest.size());
    std::partial_sort(nearest.begin(), nearest.begin() + count, nearest.end(),
        [](const NearestInstance& a, const NearestInstance& b) {
            return a.distSq < b.distSq;
        });

    NC::LOGGING::Debug(NC::LOGGING::Category::Graphics, "Nearest map instances to camera:");
    for (size_t rank = 0; rank < count; ++rank) {
        const auto& entry = nearest[rank];
        const auto& inst = map->instances[entry.index];
        NC::LOGGING::Debug(NC::LOGGING::Category::Graphics,
            "  #", rank,
            " idx=", entry.index,
            " pos=(", inst.pos.x, ",", inst.pos.y, ",", inst.pos.z, ")",
            " dist=", std::sqrt(entry.distSq));
    }
}

}

void DeferredRenderer::HandleModeSwitchInput() {
    static bool f1WasPressed = false;
    const bool f1Pressed = window_ && window_->IsKeyPressed(GLFW_KEY_F1);
    if (f1Pressed && !f1WasPressed) {
        SetRenderMode(renderMode_ == RenderMode::Work ? RenderMode::Play : RenderMode::Work);
    }
    f1WasPressed = f1Pressed;
}

void DeferredRenderer::UpdateDirtyFrameState() {
    static const bool cameraTrace = FrameLoopCameraTraceEnabled();
    static uint64_t dirtySeq = 0;
    ++dirtySeq;

    static glm::dvec3 lastCameraPos = glm::dvec3(0.0);
    static double lastYaw = 0.0;
    static double lastPitch = 0.0;

    glm::dvec3 curPos = camera.getPosition();
    double curYaw = camera.getYaw();
    double curPitch = camera.getPitch();

    const bool cameraChanged = (curPos != lastCameraPos) || (curYaw != lastYaw) || (curPitch != lastPitch);

    if (!cameraChanged && width == dirtyLastWidth_ && height == dirtyLastHeight_) {
        return;
    }

    lastCameraPos = curPos;
    lastYaw = curYaw;
    lastPitch = curPitch;

    dirtyFlag_ = true;

    if (width != dirtyLastWidth_ || height != dirtyLastHeight_) {
        dirtyFlag_ = true;
        dirtyLastWidth_ = width;
        dirtyLastHeight_ = height;
    }
}


void DeferredRenderer::ApplyRenderPassBaseline(bool shouldExecuteRenderPasses) {
    if (shouldExecuteRenderPasses) {
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_TRUE);
        if (FrameLoopFaceCullingDisabled()) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, device_ ? device_->GetDefaultFramebuffer() : 0);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.08f, 0.085f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void DeferredRenderer::WaitForFrameFence() {
    if (!FrameLoopFenceWaitEnabled()) return;

    const int waitIdx = (frameFenceIdx_ + 1) % (kMaxFramesInFlight + 1);
    if (!frameFences_[waitIdx]) return;

    const auto tFence = std::chrono::high_resolution_clock::now();
    const uint64_t waitBudgetNs = FrameLoopFenceWaitBudgetNs();
    const GLenum waitResult = glClientWaitSync(frameFences_[waitIdx], 0, waitBudgetNs);
    if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED || waitResult == GL_WAIT_FAILED) {
        glDeleteSync(frameFences_[waitIdx]);
        frameFences_[waitIdx] = nullptr;
    }
    frameProfile_.fenceWait = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - tFence).count();
}

void DeferredRenderer::CreateFrameFence() {
    if (!FrameLoopFenceWaitEnabled()) return;

    if (frameFences_[frameFenceIdx_]) {
        glDeleteSync(frameFences_[frameFenceIdx_]);
        frameFences_[frameFenceIdx_] = nullptr;
    }
    frameFences_[frameFenceIdx_] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    frameFenceIdx_ = (frameFenceIdx_ + 1) % (kMaxFramesInFlight + 1);
}

void DeferredRenderer::UpdateFrameProfileLogs() {
    frameProfile_.fps = frameProfile_.frameTotal > 0.0 ? 1000.0 / frameProfile_.frameTotal : 0.0;
    rsFrame_.Push(static_cast<float>(frameProfile_.frameTotal));
    rsCpu_.Push(0.0f);
    rsSwap_.Push(static_cast<float>(frameProfile_.swapBuffers));

    ++profileFrameCounter_;
    if (profileFrameCounter_ < kProfileLogInterval) return;
    profileFrameCounter_ = 0;

    char profBuf[256];
    std::snprintf(profBuf, sizeof(profBuf),
        "[PROFILE] fps=%.1f frame=%.2fms swap=%.2f fence=%.2f "
        "input=%.3f shdrRld=%.3f tick=%.2f prePass=%.3f "
        "mode=%s dirtySkip=%d "
        "frame_p50=%.2f p95=%.2f p99=%.2f",
        frameProfile_.fps, frameProfile_.frameTotal,
        frameProfile_.swapBuffers, frameProfile_.fenceWait,
        frameProfile_.inputPoll, frameProfile_.shaderReload,
        frameProfile_.sceneTick, frameProfile_.prePassSetup,
        (renderMode_ == RenderMode::Work) ? "Work" : "Play",
        frameProfile_.dirtyFrameSkipped,
        rsFrame_.Percentile(50), rsFrame_.Percentile(95), rsFrame_.Percentile(99));
    NC::LOGGING::Log(profBuf);
}

void DeferredRenderer::HandleFrameDebugInput(bool allowViewportKeyboardInput) {
    {
        static double lastEventDebugKeyTime = 0.0;
        const double now = GetTimeSeconds();
        auto pressed = [this, allowViewportKeyboardInput](int key) {
            return allowViewportKeyboardInput && inputSystem_ && inputSystem_ && inputSystem_->IsKeyPressed(key);
        };
        auto debounce = [&](bool cond) {
            return cond && (now - lastEventDebugKeyTime) > 0.2;
        };

        const size_t eventsCount = scene_->currentMap ? scene_->currentMap->event_mapping.size() : 0;

        if (debounce(pressed(GLFW_KEY_F5))) {
            filterEventsOnly = !filterEventsOnly;
            lastEventDebugKeyTime = now;
            NC::LOGGING::Info(NC::LOGGING::Category::Input, "[EVENT] mode=", FrameLoopEventModeText(filterEventsOnly),
                      " bit=", activeEventFilterIndex);
            scene_->UpdateIncrementalStreaming(true);
        }
        if (debounce(pressed(GLFW_KEY_F6))) {
            if (eventsCount > 0) {
                activeEventFilterIndex = (activeEventFilterIndex + 1) % (int)eventsCount;
            } else {
                activeEventFilterIndex = 0;
            }
            lastEventDebugKeyTime = now;
            NC::LOGGING::Debug(NC::LOGGING::Category::Input, "[EVENT] bit -> ", activeEventFilterIndex);
            if (filterEventsOnly) scene_->UpdateIncrementalStreaming(true);
        }
        if (debounce(pressed(GLFW_KEY_F7))) {
            if (eventsCount > 0) {
                activeEventFilterIndex = (activeEventFilterIndex - 1 + (int)eventsCount) % (int)eventsCount;
            } else {
                activeEventFilterIndex = 0;
            }
            lastEventDebugKeyTime = now;
            NC::LOGGING::Debug(NC::LOGGING::Category::Input, "[EVENT] bit -> ", activeEventFilterIndex);
            if (filterEventsOnly) scene_->UpdateIncrementalStreaming(true);
        }
        if (debounce(pressed(GLFW_KEY_F8))) {
            lastEventDebugKeyTime = now;
            FrameLoopDebugPrintNearestInstances(scene_->currentMap, camera.getPosition(), 25);
        }
    }

    static bool rKeyWasPressed = false;
    bool rKeyPressed = allowViewportKeyboardInput && inputSystem_ && inputSystem_->IsKeyPressed(GLFW_KEY_R);
    if (rKeyPressed && !rKeyWasPressed) {
        NC::LOGGING::Info(NC::LOGGING::Category::Engine, "[R] Reloading map from camera position...");
        ReloadMapWithCurrentMode();
    }
    rKeyWasPressed = rKeyPressed;

    static bool f11WasPressed = false;
    bool f11Pressed = allowViewportKeyboardInput && inputSystem_ && inputSystem_->IsKeyPressed(GLFW_KEY_F11);
    f11WasPressed = f11Pressed;
}

void DeferredRenderer::HandleViewportPicking(bool allowViewportKeyboardInput) {
    (void)allowViewportKeyboardInput;
    const bool uiWantsMouse = false;
    static bool lmbWasPressed = false;
    bool lmbPressed = false;
    if (window_) {
        lmbPressed = window_->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    }

    bool allowViewportMousePick = false;
    if (editorModeEnabled_ && editorViewportInputRouting_) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        window_->GetCursorPos(cursorX, cursorY);
        allowViewportMousePick = IsSceneViewportPointerInside(cursorX, cursorY);
    } else {
        allowViewportMousePick = !uiWantsMouse;
    }
    lmbWasPressed = lmbPressed;
}
