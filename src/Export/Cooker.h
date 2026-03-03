// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_COOKER_H
#define NDEVC_COOKER_H

#include "Export/ExportTypes.h"
#include "Assets/NDEVcStructure.h"
#include <vector>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// Cooker — profile-derived settings and per-asset cooking operations
//
// Game data enters through stable descriptors (MeshDesc, TextureDesc, etc.).
// Never takes direct game-code pointers or engine-internal GL state.
// ---------------------------------------------------------------------------
class Cooker {
public:
    // Build the canonical CookSettings for a given export profile
    static CookSettings BuildSettings(ExportProfile profile);

    // Asset cooking: update ctx.report with VRAM/draw estimates, issue warnings
    static void CookMesh(const MeshDesc& desc, const CookSettings& s, ExportContext& ctx);
    static void CookTexture(const TextureDesc& desc, const CookSettings& s, ExportContext& ctx);
    static void CookMaterial(const MaterialDesc& desc, const CookSettings& s, ExportContext& ctx);

    // Build draw proxies and visibility cells from scene entities + mesh descs
    static void CookScene(const SceneDesc& scene,
                          const std::vector<MeshDesc>& meshDescs,
                          const CookSettings& s,
                          ExportContext& ctx);

    // Keyframe reduction (in-place on the output AnimSection)
    static void CookAnimation(const AnimSection& src, const CookSettings& s, AnimSection& out);

    // Estimation helpers (public so ValidationGates can call them)
    static uint64_t EstimateTextureVRAM(const TextureDesc& desc, const CookSettings& s);
    static uint64_t EstimateMeshVRAM(const MeshDesc& desc, const CookSettings& s);

private:
    // Greedy forward-pass keyframe reducer for float and vec4 key channels
    static std::vector<AnimKey<float>>     ReduceFloatKeys(
        const std::vector<AnimKey<float>>& keys, float epsilon);
    static std::vector<AnimKey<glm::vec4>> ReduceVec4Keys(
        const std::vector<AnimKey<glm::vec4>>& keys, float epsilon);

    // Build world matrix from entity position / rotation (xyzw quat) / scale
    static glm::mat4 EntityWorldMatrix(const EntityDesc& e);
};

} // namespace NDEVC::Export

#endif // NDEVC_COOKER_H
