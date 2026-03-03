// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/SceneExportOptimizer.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// CellToGrid — convert a cell's XZ centre to integer grid coordinates
// ---------------------------------------------------------------------------
glm::ivec2 SceneExportOptimizer::CellToGrid(
    const ExportVisCell& cell,
    const glm::vec2& gridOriginXZ,
    float cellSizeX, float cellSizeZ)
{
    const glm::vec2 centre = (cell.minXZ + cell.maxXZ) * 0.5f;
    const int gx = static_cast<int>(std::floor((centre.x - gridOriginXZ.x) / cellSizeX));
    const int gz = static_cast<int>(std::floor((centre.y - gridOriginXZ.y) / cellSizeZ));
    return { gx, gz };
}

// ---------------------------------------------------------------------------
// WalkLineBlocked — Bresenham 2-D line walk in grid space
// Returns true if an intermediate cell (not src or dst) is a dense occluder.
// ---------------------------------------------------------------------------
bool SceneExportOptimizer::WalkLineBlocked(
    int x0, int y0, int x1, int y1,
    int srcIdx, int dstIdx,
    int gridW, int gridH,
    const std::vector<ExportVisCell>& cells)
{
    const int dx =  std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        // Check current cell (skip src and dst)
        if (x0 >= 0 && x0 < gridW && y0 >= 0 && y0 < gridH) {
            const int idx = y0 * gridW + x0;
            if (idx != srcIdx && idx != dstIdx &&
                idx >= 0 && idx < static_cast<int>(cells.size())) {
                if (cells[idx].drawCount() >= kOccluderMinDraws) return true;
            }
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// BakePVS — cell-to-cell visibility using draw density as occluders
// ---------------------------------------------------------------------------
PVSData SceneExportOptimizer::BakePVS(
    const std::vector<ExportVisCell>& cells,
    int gridW, int gridH,
    const glm::vec2& gridOriginXZ,
    float cellSizeX, float cellSizeZ)
{
    PVSData pvs;
    const int N = static_cast<int>(cells.size());
    if (N == 0 || gridW <= 0 || gridH <= 0) {
        NC::LOGGING::Warning("[PVS] Empty cell set — skipping bake");
        return pvs;
    }

    pvs.cellCount = N;
    pvs.cellVisibility.resize(N);

    const auto tStart = std::chrono::steady_clock::now();

    // Pre-compute grid coords for all cells
    std::vector<glm::ivec2> gridCoords(N);
    for (int i = 0; i < N; ++i) {
        gridCoords[i] = CellToGrid(cells[i], gridOriginXZ, cellSizeX, cellSizeZ);
    }

    int visiblePairs = 0;
    for (int src = 0; src < N; ++src) {
        pvs.cellVisibility[src].push_back(src); // every cell sees itself

        for (int dst = src + 1; dst < N; ++dst) {
            const glm::ivec2& a = gridCoords[src];
            const glm::ivec2& b = gridCoords[dst];

            const bool blocked = WalkLineBlocked(
                a.x, a.y, b.x, b.y,
                src, dst, gridW, gridH, cells);

            if (!blocked) {
                pvs.cellVisibility[src].push_back(dst);
                pvs.cellVisibility[dst].push_back(src);
                ++visiblePairs;
            }
        }
    }

    // Sort each cell's visibility list for fast binary-search at runtime
    for (auto& vis : pvs.cellVisibility) {
        std::sort(vis.begin(), vis.end());
    }

    const auto tEnd = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    pvs.valid = true;
    NC::LOGGING::Log("[PVS] Bake complete: cells=", N, " visiblePairs=", visiblePairs,
                     " time=", ms, "ms");
    return pvs;
}

// ---------------------------------------------------------------------------
// BuildHLOD — generate impostor cell records for far-field content
// ---------------------------------------------------------------------------
HLODData SceneExportOptimizer::BuildHLOD(
    const std::vector<ExportVisCell>& cells,
    const std::vector<ExportDrawProxy>& draws,
    int gridW, int gridH,
    const glm::vec2& gridOriginXZ,
    float cellSizeX, float cellSizeZ,
    int highDetailRadiusChunks,
    const glm::vec3& worldCenter)
{
    HLODData hlod;
    hlod.highDetailRadiusChunks = highDetailRadiusChunks;

    if (cells.empty() || gridW <= 0 || gridH <= 0) {
        NC::LOGGING::Warning("[HLOD] Empty cell set — skipping build");
        return hlod;
    }

    // Grid coord of the world center
    const int centerGX = static_cast<int>(std::floor(
        (worldCenter.x - gridOriginXZ.x) / cellSizeX));
    const int centerGZ = static_cast<int>(std::floor(
        (worldCenter.z - gridOriginXZ.y) / cellSizeZ));

    for (const ExportVisCell& cell : cells) {
        const glm::ivec2 gc = CellToGrid(cell, gridOriginXZ, cellSizeX, cellSizeZ);
        const int distChebyshev = std::max(std::abs(gc.x - centerGX),
                                           std::abs(gc.y - centerGZ));

        if (distChebyshev <= highDetailRadiusChunks) continue; // high-detail zone
        if (cell.drawIndices.empty()) continue;                 // empty cell

        HLODCell hc;
        hc.cellIndex = cell.cellIndex;
        hc.drawCount = static_cast<int>(cell.drawIndices.size());

        // Merge world AABBs of all draws in this cell
        glm::vec3 mergedMin(std::numeric_limits<float>::max());
        glm::vec3 mergedMax(std::numeric_limits<float>::lowest());
        for (uint32_t di : cell.drawIndices) {
            if (di >= static_cast<uint32_t>(draws.size())) continue;
            mergedMin = glm::min(mergedMin, draws[di].worldBoundsMin);
            mergedMax = glm::max(mergedMax, draws[di].worldBoundsMax);
        }

        if (mergedMin.x <= mergedMax.x) {
            hc.boundsMin = mergedMin;
            hc.boundsMax = mergedMax;
        } else {
            // Fallback: cell XZ extent + zero Y
            hc.boundsMin = glm::vec3(cell.minXZ.x, 0.0f, cell.minXZ.y);
            hc.boundsMax = glm::vec3(cell.maxXZ.x, 0.0f, cell.maxXZ.y);
        }

        hlod.cells.push_back(hc);
    }

    hlod.valid = !hlod.cells.empty();
    NC::LOGGING::Log("[HLOD] Built ", hlod.cells.size(), " impostor cells",
                     " (highDetailRadius=", highDetailRadiusChunks, " chunks)");
    return hlod;
}

// ---------------------------------------------------------------------------
// PruneShaderVariants — collect actually-used variant combos per shader
// ---------------------------------------------------------------------------
PrunedVariants SceneExportOptimizer::PruneShaderVariants(
    const std::vector<ExportDrawProxy>& solidDraws,
    const std::vector<ExportDrawProxy>& alphaDraws)
{
    PrunedVariants result;

    // Variant key = space-separated sorted define flags derived from draw properties
    auto variantKey = [](const ExportDrawProxy& d) -> std::string {
        std::vector<std::string> defines;
        if (d.isAlpha)  defines.push_back("ALPHA_TEST");
        if (d.isDecal)  defines.push_back("DECAL");
        if (d.isStatic) defines.push_back("STATIC");
        // Default variant if no flags
        if (defines.empty()) defines.push_back("DEFAULT");
        std::sort(defines.begin(), defines.end());
        std::string key;
        for (size_t i = 0; i < defines.size(); ++i) {
            if (i > 0) key += ' ';
            key += defines[i];
        }
        return key;
    };

    // Collect all variants per shader name
    std::unordered_map<std::string, std::unordered_set<std::string>> variantSets;
    int totalDraws = 0;

    auto collect = [&](const std::vector<ExportDrawProxy>& draws) {
        for (const ExportDrawProxy& d : draws) {
            if (d.shaderName.empty()) continue;
            variantSets[d.shaderName].insert(variantKey(d));
            ++totalDraws;
        }
    };
    collect(solidDraws);
    collect(alphaDraws);

    // Enumerate all possible combos (2^flags) per shader to count pruneable ones
    // Possible flags: DEFAULT, ALPHA_TEST, DECAL, STATIC → 4 bits = 16 combos max
    constexpr int kMaxCombos = 16;
    result.totalPossibleCount = static_cast<int>(variantSets.size()) * kMaxCombos;

    for (const auto& [shaderName, usedSet] : variantSets) {
        std::vector<std::string> active(usedSet.begin(), usedSet.end());
        std::sort(active.begin(), active.end());
        result.activeVariants[shaderName] = std::move(active);
    }

    // Count pruned = possible - actually used
    int usedCount = 0;
    for (const auto& [name, variants] : result.activeVariants) {
        usedCount += static_cast<int>(variants.size());
    }
    result.prunedCount = result.totalPossibleCount - usedCount;

    result.valid = true;
    NC::LOGGING::Log("[VARIANTS] Shaders=", result.activeVariants.size(),
                     " usedVariants=", usedCount,
                     " prunable=", result.prunedCount,
                     " draws=", totalDraws);
    return result;
}

} // namespace NDEVC::Export
