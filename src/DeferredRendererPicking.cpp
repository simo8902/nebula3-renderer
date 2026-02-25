// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "DeferredRenderer.h"
#include "DeferredRendererAnimation.h"
#include "SelectionRaycaster.h"
#include "Parser.h"
#include "GLStateDebug.h"
#include "AnimationSystem.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "rendering/opengl/OpenGLDevice.h"
#include "rendering/opengl/GLFWPlatform.h"
#include "rendering/opengl/OpenGLShaderManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include "DrawBatchSystem.h"
#include "MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"
#include "Model/ModelServer.h"
#include "Servers/MeshServer.h"
#include "Servers/TextureServer.h"
#include "Map/MapHeader.h"
#include "ParticleData/ParticleServer.h"
#include "NC.Logger.h"
#include "gtx/norm.hpp"
DeferredRenderer::DisabledDrawKey DeferredRenderer::MakeDisabledDrawKey(const DrawCmd& dc) const {
    DisabledDrawKey key;
    key.instance = dc.instance;
    key.nodeName = dc.nodeName;
    key.shdr = dc.shdr;
    key.group = dc.group;
    return key;
}

bool DeferredRenderer::IsDrawDisabled(const DrawCmd& dc) const {
    const DisabledDrawKey key = MakeDisabledDrawKey(dc);
    return disabledDrawSet.find(key) != disabledDrawSet.end();
}

void DeferredRenderer::SetDrawDisabled(const DrawCmd& dc, bool disabled) {
    const DisabledDrawKey key = MakeDisabledDrawKey(dc);
    if (disabled) {
        auto [it, inserted] = disabledDrawSet.insert(key);
        if (inserted) {
            disabledDrawOrder.push_back(key);
        }
    } else {
        disabledDrawSet.erase(key);
        if (disabledSelectionIndex >= 0 &&
            disabledSelectionIndex < static_cast<int>(disabledDrawOrder.size()) &&
            disabledDrawOrder[disabledSelectionIndex] == key) {
            disabledSelectionIndex = -1;
        }
    }
    ApplyDisabledDrawFlags();
    // Disabled flags changed — the static batch cache must be rebuilt so it
    // excludes any newly-disabled objects (or re-includes newly-enabled ones).
    lastVisibleCells_.clear(); // force UpdateVisibilityThisFrame to re-run fully next frame
    DrawBatchSystem::instance().reset(true);
}


void DeferredRenderer::ApplyDisabledDrawFlags() {
    auto apply = [this](std::vector<DrawCmd>& draws) {
        for (auto& dc : draws) {
            dc.disabled = IsDrawDisabled(dc);
        }
    };
    apply(solidDraws);
    apply(alphaTestDraws);
    apply(simpleLayerDraws);
    apply(environmentDraws);
    apply(environmentAlphaDraws);
    apply(waterDraws);
    apply(decalDraws);
    apply(refractionDraws);
    apply(postAlphaUnlitDraws);
}


void DeferredRenderer::ClearDisabledDraws() {
    disabledDrawSet.clear();
    disabledDrawOrder.clear();
    disabledSelectionIndex = -1;
    ApplyDisabledDrawFlags();
    lastVisibleCells_.clear();
    DrawBatchSystem::instance().reset(true);
}


void DeferredRenderer::UpdateLookAtSelection(bool force) {
    if (!clickPickEnabled && !force) return;

    const double beginTime = glfwGetTime();

    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window_ ? window_->GetNativeHandle() : nullptr);
    if (!glfwWin) return;

    double cursorX = 0.0;
    double cursorY = 0.0;
    window_->GetCursorPos(cursorX, cursorY);

    int winW = 0;
    int winH = 0;
    glfwGetWindowSize(glfwWin, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
        window_->GetFramebufferSize(winW, winH);
    }
    if (winW <= 0 || winH <= 0) return;

    const float ndcX = static_cast<float>((2.0 * cursorX) / static_cast<double>(winW) - 1.0);
    const float ndcY = static_cast<float>(1.0 - (2.0 * cursorY) / static_cast<double>(winH));
    const glm::mat4 invViewProj = glm::inverse(camera_.getProjectionMatrix() * camera_.getViewMatrix());

    glm::vec4 nearH = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearH.w) < 1e-8f || std::abs(farH.w) < 1e-8f) return;
    nearH /= nearH.w;
    farH /= farH.w;

    glm::vec3 rayOrigin = glm::vec3(nearH);
    glm::vec3 rayDir = glm::vec3(farH - nearH);
    const float rayDirLen2 = glm::dot(rayDir, rayDir);
    if (!std::isfinite(rayDirLen2) || rayDirLen2 <= 1e-8f) {
        selectedObject = nullptr;
        selectedIndex = -1;
        pickCandidateCount = 0;
        pickLastUpdateMs = (glfwGetTime() - beginTime) * 1000.0;
        return;
    }
    rayDir = glm::normalize(rayDir);

    DrawCmd* best = nullptr;
    float bestT = std::numeric_limits<float>::max();
    int bestIndex = -1;
    int runningIndex = 0;
    int candidateCount = 0;

    auto testDrawList = [&](std::vector<DrawCmd>& draws) {
        for (auto& dc : draws) {
            const int currentIndex = runningIndex++;
            if (!dc.mesh) continue;
            if (dc.disabled) continue;

            glm::vec3 centerWS(0.0f);
            float radiusWS = 0.0f;
            if (!SelectionRaycaster::ComputeDrawBoundingSphereWS(dc, centerWS, radiusWS)) continue;

            float tHit = 0.0f;
            if (!SelectionRaycaster::RayIntersectsSphere(rayOrigin, rayDir, centerWS, radiusWS, tHit)) continue;
            candidateCount++;
            if (tHit < bestT) {
                bestT = tHit;
                best = &dc;
                bestIndex = currentIndex;
            }
        }
    };

    testDrawList(solidDraws);
    testDrawList(alphaTestDraws);
    testDrawList(simpleLayerDraws);
    testDrawList(environmentDraws);
    testDrawList(environmentAlphaDraws);
    testDrawList(waterDraws);
    if (pickIncludeTransparent) {
        testDrawList(refractionDraws);
        testDrawList(postAlphaUnlitDraws);
    } else {
        runningIndex += static_cast<int>(refractionDraws.size() + postAlphaUnlitDraws.size());
    }
    if (pickIncludeDecals) {
        testDrawList(decalDraws);
    } else {
        runningIndex += static_cast<int>(decalDraws.size());
    }

    selectedObject = best;
    selectedIndex = bestIndex;
    pickCandidateCount = candidateCount;

    if (selectedObject) {
        cachedObj = *selectedObject;
        cachedIndex = selectedIndex;
    } else {
        cachedObj = DrawCmd{};
        cachedIndex = -1;
    }

    pickLastUpdateMs = (glfwGetTime() - beginTime) * 1000.0;
}

