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
#include "visibility/VisibilityGrid.h"
void DeferredRenderer::BuildVisibilityGrids() {
    if (!currentMap) return;
    const MapInfo& info = currentMap->info;
    solidVisGrid_.Build(solidDraws, info);
    alphaTestVisGrid_.Build(alphaTestDraws, info);
    envVisGrid_.Build(environmentDraws, info);
    envAlphaVisGrid_.Build(environmentAlphaDraws, info);
    waterVisGrid_.Build(waterDraws, info);
    postAlphaVisGrid_.Build(postAlphaUnlitDraws, info);
    simpleLayerVisGrid_.Build(simpleLayerDraws, info);
    lastVisibleCells_.clear(); // force UpdateVisibility to run on first frame after build
}


void DeferredRenderer::UpdateVisibilityThisFrame(const Camera::Frustum& frustum) {
    if (!enableVisibilityGrid_ || !solidVisGrid_.IsBuilt()) return;

    // Use the camera's ground look-at point (ray → Y=0 intersection) as the
    // visibility centre. The camera eye is behind and above the visible area;
    // using the eye XZ makes the range expand from behind the camera outward,
    // which reads on-screen as "bottom to top". The look-at point is the centre
    // of what the player actually sees.
    const glm::vec3 camPos = camera_.getPosition();
    const glm::vec3 fwd    = camera_.getForward();
    glm::vec3 visCenter = camPos;
    if (fwd.y < -0.001f) {
        const float t = -camPos.y / fwd.y; // ray-plane Y=0
        visCenter = camPos + t * fwd;
        visCenter.y = 0.0f;
    }

    // Run the cell query every frame — it's cheap (few AABB frustum tests).
    // UpdateVisibility (the expensive part) is gated by the cell-set comparison below.
    solidVisGrid_.QueryVisibleCells(visCenter, frustum, visibleRange_, visibleCells_);
    if (visibleCells_ == lastVisibleCells_) return;

    // Only solidDraws feed the static matrix cache in DrawBatchSystem::cull().
    // alphaTestDraws and environmentDraws go through cullGeneric() which has no static
    // cache, so changing their disabled flags never requires a cache rebuild.
    const bool solidChanged = solidVisGrid_.UpdateVisibility(solidDraws, visibleCells_);
    alphaTestVisGrid_.UpdateVisibility(alphaTestDraws, visibleCells_);
    envVisGrid_.UpdateVisibility(environmentDraws, visibleCells_);
    envAlphaVisGrid_.UpdateVisibility(environmentAlphaDraws, visibleCells_);
    waterVisGrid_.UpdateVisibility(waterDraws, visibleCells_);
    postAlphaVisGrid_.UpdateVisibility(postAlphaUnlitDraws, visibleCells_);
    simpleLayerVisGrid_.UpdateVisibility(simpleLayerDraws, visibleCells_);

    if (solidChanged) {
        // The static matrix cache must be rebuilt to reflect the new disabled state
        // of solidDraws (only static-flagged objects go into that cache).
        DrawBatchSystem::instance().reset(true);
    }
    lastVisibleCells_ = visibleCells_;
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
        debugLineProgram_ = glCreateProgram();
        glAttachShader(debugLineProgram_, vert);
        glAttachShader(debugLineProgram_, frag);
        glLinkProgram(debugLineProgram_);
        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    // Lazily create VAO + VBO.
    if (debugCellVAO_ == 0) {
        glGenVertexArrays(1, &debugCellVAO_);
        glGenBuffers(1, &debugCellVBO_);
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
