// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_SCENE_EXPORT_OPTIMIZER_H
#define NDEVC_SCENE_EXPORT_OPTIMIZER_H

#include "Export/ExportTypes.h"
#include <vector>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// SceneExportOptimizer — offline scene optimization passes
//
// All methods are stateless and take only export-safe types (no GL, no engine
// internals). Designed to run headlessly from both editor and CI contexts.
// ---------------------------------------------------------------------------
class SceneExportOptimizer {
public:
    // A cell is treated as an opaque occluder when it contains >= this many draws
    static constexpr int kOccluderMinDraws = 3;

    // PVS bake
    // -------------------------------------------------------------------------
    // For every pair of cells (A, B), walks the Bresenham line from A's grid
    // coord to B's grid coord. If any intermediate cell is a dense occluder, the
    // pair is marked non-visible. Each cell always sees itself.
    // Complexity: O(N² × sqrt(N)) where N = gridW × gridH.
    // For a 64×64 grid (~4096 cells) this runs in ~1–2 s on a single core.
    static PVSData BakePVS(
        const std::vector<ExportVisCell>& cells,
        int gridW, int gridH,
        const glm::vec2& gridOriginXZ,
        float cellSizeX, float cellSizeZ);

    // HLOD / impostor record generation
    // -------------------------------------------------------------------------
    // Cells outside the high-detail radius (measured in cells from worldCenter)
    // become HLODCell records carrying their merged world AABB and draw count.
    // These records are stored in the package and used by the runtime LOD system
    // to replace far-field draws with lower-cost impostor geometry.
    static HLODData BuildHLOD(
        const std::vector<ExportVisCell>& cells,
        const std::vector<ExportDrawProxy>& draws,
        int gridW, int gridH,
        const glm::vec2& gridOriginXZ,
        float cellSizeX, float cellSizeZ,
        int highDetailRadiusChunks,
        const glm::vec3& worldCenter);

    // Shader variant pruning
    // -------------------------------------------------------------------------
    // Groups draw proxies by shader name and collects the set of variant-define
    // strings actually referenced. Any variant combo not present in the result
    // set is safe to prune from shader compilation.
    static PrunedVariants PruneShaderVariants(
        const std::vector<ExportDrawProxy>& solidDraws,
        const std::vector<ExportDrawProxy>& alphaDraws);

private:
    // Map a cell to its (x, z) grid coordinates
    static glm::ivec2 CellToGrid(
        const ExportVisCell& cell,
        const glm::vec2& gridOriginXZ,
        float cellSizeX, float cellSizeZ);

    // Bresenham 2D line walk; returns false as soon as a blocking cell is found
    static bool WalkLineBlocked(
        int x0, int y0, int x1, int y1,
        int srcIdx, int dstIdx,
        int gridW, int gridH,
        const std::vector<ExportVisCell>& cells);
};

} // namespace NDEVC::Export

#endif // NDEVC_SCENE_EXPORT_OPTIMIZER_H
