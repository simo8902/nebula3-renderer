// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"
#include "Rendering/DeferredRendererAnimation.h"
#include "Rendering/SelectionRaycaster.h"
#include "Assets/Parser.h"
#include "Rendering/GLStateDebug.h"
#include "Animation/AnimationSystem.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "Rendering/OpenGL/OpenGLDevice.h"
#include "Platform/GLFWPlatform.h"
#include "Rendering/OpenGL/OpenGLShaderManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include "Rendering/DrawBatchSystem.h"
#include "Rendering/MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtc/type_ptr.hpp"
#include "gtx/string_cast.hpp"
#include "Assets/Model/ModelServer.h"
#include "Assets/Servers/MeshServer.h"
#include "Assets/Servers/TextureServer.h"
#include "Assets/Map/MapHeader.h"
#include "Assets/Particles/ParticleServer.h"
#include "Core/Logger.h"
#include "gtx/norm.hpp"
#include "Rendering/Visibility/VisibilityGrid.h"
void DeferredRenderer::BuildVisibilityGrids() {
    if (!scene_.currentMap) return;
    const MapInfo& info = scene_.currentMap->info;
    solidVisGrid_.Build(solidDraws, info);
    alphaTestVisGrid_.Build(alphaTestDraws, info);
    envVisGrid_.Build(environmentDraws, info);
    envAlphaVisGrid_.Build(environmentAlphaDraws, info);
    waterVisGrid_.Build(waterDraws, info);
    postAlphaVisGrid_.Build(postAlphaUnlitDraws, info);
    simpleLayerVisGrid_.Build(simpleLayerDraws, info);
    refractionVisGrid_.Build(refractionDraws, info);
    decalVisGrid_.Build(decalDraws, info);
    visibilityStage_.Reset();
    lastVisibleCells_.clear(); // force UpdateVisibility to run on first frame after build
}


void DeferredRenderer::UpdateVisibilityThisFrame(const Camera::Frustum& frustum) {
    // Reset Phase 2 per-frame metrics
    costControlAlphaReduced_ = 0;
    costControlDecalReduced_ = 0;
    costControlEnvAlphaReduced_ = 0;
    costControlPostAlphaReduced_ = 0;
    visCellsHighDetail_ = 0;
    visCellsLowDetail_ = 0;

    if (!enableVisibilityGrid_ || activePolicy_.forceFullSceneVisible) {
        // Only run revealAll once when transitioning from enabled → disabled
        if (!visGridRevealedAll_) {
            visResolveSkipped_ = false;
            visibilityStage_.Reset();
            auto revealAll = [](std::vector<DrawCmd>& draws) {
                bool changed = false;
                for (auto& dc : draws) {
                    const bool shouldBeDisabled = dc.userDisabled;
                    if (dc.disabled != shouldBeDisabled) {
                        dc.disabled = shouldBeDisabled;
                        changed = true;
                    }
                    dc.frustumCulled = false;
                }
                return changed;
            };

            revealAll(solidDraws);
            revealAll(alphaTestDraws);
            revealAll(environmentDraws);
            revealAll(environmentAlphaDraws);
            revealAll(simpleLayerDraws);
            revealAll(waterDraws);
            revealAll(refractionDraws);
            revealAll(postAlphaUnlitDraws);
            revealAll(decalDraws);
            revealAll(particleDraws);

            visibleCells_.clear();
            lastVisibleCells_.clear();
            visGridRevealedAll_ = true;
            visFrustumCacheValid_ = false;
        } else {
            visResolveSkipped_ = true;
        }
        // Apply cost control even when grid is disabled (distance-based alpha reduction)
        const glm::vec3 camPos = camera_.getPosition();
        ApplyDistanceCostControl(camPos);
        return;
    }

    // Camera-static fast path: if frustum hasn't changed, visible cells are identical.
    if (visFrustumCacheValid_ &&
        std::memcmp(&frustum, &visCachedFrustum_, sizeof(Camera::Frustum)) == 0) {
        visResolveSkipped_ = true;
        // Apply cost control unconditionally — restore path needed even when aggression == 0
        const glm::vec3 camPos = camera_.getPosition();
        ApplyDistanceCostControl(camPos);
        return;
    }

    const VisibilityGrid* queryGrid = nullptr;
    if (solidVisGrid_.IsBuilt()) queryGrid = &solidVisGrid_;
    else if (alphaTestVisGrid_.IsBuilt()) queryGrid = &alphaTestVisGrid_;
    else if (envVisGrid_.IsBuilt()) queryGrid = &envVisGrid_;
    else if (envAlphaVisGrid_.IsBuilt()) queryGrid = &envAlphaVisGrid_;
    else if (waterVisGrid_.IsBuilt()) queryGrid = &waterVisGrid_;
    else if (postAlphaVisGrid_.IsBuilt()) queryGrid = &postAlphaVisGrid_;
    else if (simpleLayerVisGrid_.IsBuilt()) queryGrid = &simpleLayerVisGrid_;
    else if (refractionVisGrid_.IsBuilt()) queryGrid = &refractionVisGrid_;
    else if (decalVisGrid_.IsBuilt()) queryGrid = &decalVisGrid_;
    if (!queryGrid) { visResolveSkipped_ = true; return; }
    visGridRevealedAll_ = false;

    // Phase 2: record total cell count for metrics
    frameProfile_.visCellsTotal = queryGrid->GetTotalCellCount();

    const glm::vec3 camPos = camera_.getPosition();
    const glm::vec3 fwd    = camera_.getForward();
    glm::vec3 visCenter(camPos.x, 0.0f, camPos.z);
    if (fwd.y < -0.05f) {
        float t = -camPos.y / fwd.y;
        if (std::isfinite(t) && t >= 0.0f) {
            const float maxLookAhead = (visibleRange_ > 0.0f) ? visibleRange_ : 1200.0f;
            t = std::min(t, maxLookAhead);
            visCenter = camPos + t * fwd;
            visCenter.y = 0.0f;
        }
    }

    // Effective visible range includes governor draw-distance adjustment.
    // When policy distance is unlimited (0), governor still imposes a ceiling
    // by subtracting from a large default so overloaded frames still shed cost.
    float effectiveVisRange = visibleRange_;
    if (budgetGovernor_.drawDistAdjust > 0.0f) {
        if (effectiveVisRange <= 0.0f) {
            // Unlimited base: start from a large world-space default
            effectiveVisRange = std::max(100.0f, 1400.0f - budgetGovernor_.drawDistAdjust);
        } else {
            effectiveVisRange = std::max(100.0f, effectiveVisRange - budgetGovernor_.drawDistAdjust);
        }
    }

    queryGrid->QueryVisibleCells(visCenter, frustum, effectiveVisRange, visibleCells_);
    frameProfile_.visCellsFrustum = static_cast<int>(visibleCells_.size());

    // Phase 2: PVS filtering hook
    if (activePolicy_.allowPVS && pvsProvider_ && pvsProvider_->IsAvailable()) {
        std::vector<int> pvsFiltered;
        pvsFiltered.reserve(visibleCells_.size());
        pvsProvider_->FilterCells(visCenter, visibleCells_, pvsFiltered);
        visibleCells_ = std::move(pvsFiltered);
    }
    frameProfile_.visCellsAfterPVS = static_cast<int>(visibleCells_.size());

    // Phase 2: classify cells as high/low detail for metrics
    ClassifyChunkDetail(visCenter);

    if (visibleCells_ == lastVisibleCells_) {
        visCachedFrustum_ = frustum;
        visFrustumCacheValid_ = true;
        visResolveSkipped_ = true;
        ApplyDistanceCostControl(visCenter);
        return;
    }
    visCachedFrustum_ = frustum;
    visFrustumCacheValid_ = true;
    visResolveSkipped_ = false;

    visibilityStage_.OnCullBefore(++visibilityStageFrameIndex_);
    visibilityStage_.UpdateCameraLinks(
        visibleCells_,
        {
            VisibilityQueryInput{&solidVisGrid_, &solidDraws},
            VisibilityQueryInput{&alphaTestVisGrid_, &alphaTestDraws},
            VisibilityQueryInput{&envVisGrid_, &environmentDraws},
            VisibilityQueryInput{&envAlphaVisGrid_, &environmentAlphaDraws},
            VisibilityQueryInput{&waterVisGrid_, &waterDraws},
            VisibilityQueryInput{&postAlphaVisGrid_, &postAlphaUnlitDraws},
            VisibilityQueryInput{&simpleLayerVisGrid_, &simpleLayerDraws},
            VisibilityQueryInput{&refractionVisGrid_, &refractionDraws},
            VisibilityQueryInput{&decalVisGrid_, &decalDraws},
        });

    visibilityStage_.ResolveDrawVisibility(solidDraws);
    visibilityStage_.ResolveDrawVisibility(alphaTestDraws);
    visibilityStage_.ResolveDrawVisibility(environmentDraws);
    visibilityStage_.ResolveDrawVisibility(environmentAlphaDraws);
    visibilityStage_.ResolveDrawVisibility(waterDraws);
    visibilityStage_.ResolveDrawVisibility(postAlphaUnlitDraws);
    visibilityStage_.ResolveDrawVisibility(simpleLayerDraws);
    visibilityStage_.ResolveDrawVisibility(refractionDraws);
    visibilityStage_.ResolveDrawVisibility(decalDraws);
    lastVisibleCells_ = visibleCells_;

    // Phase 2: count visible objects after cell+PVS gate
    {
        int visibleCount = 0;
        auto countEnabled = [&](const std::vector<DrawCmd>& draws) {
            for (const DrawCmd& dc : draws) {
                if (!dc.disabled) ++visibleCount;
            }
        };
        countEnabled(solidDraws);
        countEnabled(alphaTestDraws);
        countEnabled(environmentDraws);
        frameProfile_.visObjectsAfterGate = visibleCount;
    }

    // Phase 2: apply distance-based cost control for expensive categories
    ApplyDistanceCostControl(visCenter);
}

// ---------------------------------------------------------------------------
// ClassifyChunkDetail — tag visible cells as high/low detail for metrics
// ---------------------------------------------------------------------------
void DeferredRenderer::ClassifyChunkDetail(const glm::vec3& visCenter) {
    const VisibilityGrid* grid = nullptr;
    if (solidVisGrid_.IsBuilt()) grid = &solidVisGrid_;
    if (!grid || visibleCells_.empty()) {
        visCellsHighDetail_ = 0;
        visCellsLowDetail_ = 0;
        frameProfile_.visCellsHigh = 0;
        frameProfile_.visCellsLow = 0;
        return;
    }

    const float cellSizeX = grid->GetCellSizeX();
    const float cellSizeZ = grid->GetCellSizeZ();
    const float cellDiag  = std::sqrt(cellSizeX * cellSizeX + cellSizeZ * cellSizeZ);
    // lodBiasAdjust from the governor shrinks the high-detail radius (each +0.5 bias = -1 chunk)
    const float totalLodBias = activePolicy_.lodBias + budgetGovernor_.lodBiasAdjust;
    const int effectiveHighDetailChunks = std::max(0,
        activePolicy_.highDetailRadiusChunks - static_cast<int>(totalLodBias + 0.5f));
    const float highRadius = static_cast<float>(effectiveHighDetailChunks) * cellDiag;
    const auto& cells = grid->GetCells();

    int highCount = 0;
    int lowCount = 0;
    for (int idx : visibleCells_) {
        if (idx < 0 || idx >= static_cast<int>(cells.size())) continue;
        const VisibilityCell& cell = cells[idx];
        const float cx = (cell.minXZ.x + cell.maxXZ.x) * 0.5f;
        const float cz = (cell.minXZ.y + cell.maxXZ.y) * 0.5f;
        const float dx = cx - visCenter.x;
        const float dz = cz - visCenter.z;
        const float distXZ = std::sqrt(dx * dx + dz * dz);
        if (distXZ <= highRadius) {
            ++highCount;
        } else {
            ++lowCount;
        }
    }
    visCellsHighDetail_ = highCount;
    visCellsLowDetail_ = lowCount;
    frameProfile_.visCellsHigh = highCount;
    frameProfile_.visCellsLow = lowCount;
}

// ---------------------------------------------------------------------------
// ApplyDistanceCostControl — suppress expensive transparent categories at distance
// ---------------------------------------------------------------------------
void DeferredRenderer::ApplyDistanceCostControl(const glm::vec3& visCenter) {
    const float aggression = budgetGovernor_.alphaAggressiveness;
    const float nearDist = activePolicy_.alphaReductionNearDist;
    const float farDist  = activePolicy_.alphaReductionFarDist;

    costControlAlphaReduced_ = 0;
    costControlDecalReduced_ = 0;
    costControlEnvAlphaReduced_ = 0;
    costControlPostAlphaReduced_ = 0;

    // Always restore previously cost-control-disabled draws first.
    // This guarantees that when aggression drops to 0 the draws re-enable.
    auto restoreCategory = [](std::vector<DrawCmd>& draws) {
        for (DrawCmd& dc : draws) {
            if (dc.costControlDisabled) {
                dc.disabled          = false;
                dc.costControlDisabled = false;
            }
        }
    };
    restoreCategory(alphaTestDraws);
    restoreCategory(decalDraws);
    restoreCategory(environmentAlphaDraws);
    restoreCategory(postAlphaUnlitDraws);

    if (aggression <= 0.0f || farDist <= nearDist) {
        frameProfile_.alphaReduced     = 0;
        frameProfile_.decalReduced     = 0;
        frameProfile_.envAlphaReduced  = 0;
        frameProfile_.postAlphaReduced = 0;
        return;
    }

    const float invRange = 1.0f / (farDist - nearDist);
    const float threshold = 1.0f - aggression;

    auto reduceCategory = [&](std::vector<DrawCmd>& draws) -> int {
        int reduced = 0;
        for (DrawCmd& dc : draws) {
            if (dc.disabled || dc.userDisabled) continue;
            const glm::vec3 drawPos(dc.worldMatrix[3]);
            const float dx = drawPos.x - visCenter.x;
            const float dz = drawPos.z - visCenter.z;
            const float distXZ = std::sqrt(dx * dx + dz * dz);
            const float t = (distXZ - nearDist) * invRange;
            if (t > threshold) {
                dc.disabled          = true;
                dc.costControlDisabled = true;
                ++reduced;
            }
        }
        return reduced;
    };

    costControlAlphaReduced_     = reduceCategory(alphaTestDraws);
    costControlDecalReduced_     = reduceCategory(decalDraws);
    costControlEnvAlphaReduced_  = reduceCategory(environmentAlphaDraws);
    costControlPostAlphaReduced_ = reduceCategory(postAlphaUnlitDraws);

    frameProfile_.alphaReduced     = costControlAlphaReduced_;
    frameProfile_.decalReduced     = costControlDecalReduced_;
    frameProfile_.envAlphaReduced  = costControlEnvAlphaReduced_;
    frameProfile_.postAlphaReduced = costControlPostAlphaReduced_;
}


void DeferredRenderer::DrawVisibilityCellsDebug() {
    if (!debugShowVisibilityCells_ || !solidVisGrid_.IsBuilt() || visibleCells_.empty()) return;

    // Lazily compile a minimal line-drawing shader.
    if (debugLineProgram_ == 0) {
        const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";
        const char* fragSrc = R"(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)";
        auto compileShader = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            return s;
        };
        GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);
        debugLineProgram_.id = glCreateProgram();
        glAttachShader(debugLineProgram_, vert);
        glAttachShader(debugLineProgram_, frag);
        glLinkProgram(debugLineProgram_);
        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    // Lazily create VAO + VBO.
    if (debugCellVAO_ == 0) {
        glGenVertexArrays(1, debugCellVAO_.put());
        glGenBuffers(1, debugCellVBO_.put());
        glBindVertexArray(debugCellVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, debugCellVBO_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Build line-segment vertices for every visible cell (wireframe AABB).
    const auto& cells = solidVisGrid_.GetCells();
    const float halfH = std::max(solidVisGrid_.GetCellSizeX(), solidVisGrid_.GetCellSizeZ()) * 0.5f;
    std::vector<glm::vec3> verts;
    verts.reserve(visibleCells_.size() * 24);

    for (int idx : visibleCells_) {
        if (idx < 0 || idx >= (int)cells.size()) continue;
        const VisibilityCell& cell = cells[idx];

        const float yCenter = (cell.minY < cell.maxY)
            ? (cell.minY + cell.maxY) * 0.5f
            : 0.0f;
        const float yLo = yCenter - halfH;
        const float yHi = yCenter + halfH;

        const float x0 = cell.minXZ.x, x1 = cell.maxXZ.x;
        const float z0 = cell.minXZ.y, z1 = cell.maxXZ.y;

        const glm::vec3 b0(x0, yLo, z0), b1(x1, yLo, z0);
        const glm::vec3 b2(x1, yLo, z1), b3(x0, yLo, z1);
        const glm::vec3 t0(x0, yHi, z0), t1(x1, yHi, z0);
        const glm::vec3 t2(x1, yHi, z1), t3(x0, yHi, z1);

        // Bottom ring
        verts.insert(verts.end(), {b0, b1, b1, b2, b2, b3, b3, b0});
        // Top ring
        verts.insert(verts.end(), {t0, t1, t1, t2, t2, t3, t3, t0});
        // Vertical pillars
        verts.insert(verts.end(), {b0, t0, b1, t1, b2, t2, b3, t3});
    }

    if (verts.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, debugCellVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(glm::vec3)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const glm::mat4 viewProj = camera_.getProjectionMatrix() * camera_.getViewMatrix();

    glUseProgram(debugLineProgram_);
    glUniformMatrix4fv(glGetUniformLocation(debugLineProgram_, "uViewProj"),
                       1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform4f(glGetUniformLocation(debugLineProgram_, "uColor"), 0.0f, 1.0f, 0.0f, 1.0f);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(debugCellVAO_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
}

bool DeferredRenderer::IsInstanceVisible(const Instance& inst, const Template& tmpl,
                                   const Camera::Frustum& frustum, const glm::vec3& camPos) {
    glm::vec3 instPos(inst.pos.x, inst.pos.y, inst.pos.z);

    glm::vec3 scale = inst.use_scaling
        ? glm::vec3(inst.scale.x, inst.scale.y, inst.scale.z)
        : glm::vec3(1.0f);

    glm::vec3 worldCenter = instPos + glm::vec3(tmpl.center) * scale;
    glm::vec3 worldExtents = glm::vec3(tmpl.extents) * scale;

    float radius = glm::length(worldExtents) + frustumMargin;

    for (int i = 0; i < 6; ++i) {
        float dist = glm::dot(frustum.planes[i].normal, worldCenter) + frustum.planes[i].d;
        if (dist < -radius) {
            return false;
        }
    }

    return true;
}
