// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/Cooker.h"
#include "Core/Logger.h"

#include "gtc/quaternion.hpp"
#include "gtc/matrix_transform.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// BuildSettings — canonical profile → settings mapping
// ---------------------------------------------------------------------------
CookSettings Cooker::BuildSettings(ExportProfile profile) {
    CookSettings s{};
    switch (profile) {
    case ExportProfile::Work:
        // Fast export: no compression, no pruning, all debug info kept
        s.compressTextures    = false;
        s.generateMips        = true;
        s.maxTextureDim       = 0;
        s.reorderForGPUCache  = false;
        s.quantizePositions   = false;
        s.pruneUnusedVariants = false;
        s.validateUniforms    = true;
        s.stripEditorData     = false;
        s.stripDebugSymbols   = false;
        s.compressPackage     = false;
        s.bakePVS             = false;
        s.buildHLOD           = false;
        s.reduceAnimKeyframes = false;
        s.includeDebugNames   = true;
        s.lodReductionRatio[0] = 1.0f;
        s.lodReductionRatio[1] = 0.5f;
        s.lodReductionRatio[2] = 0.25f;
        break;

    case ExportProfile::Playtest:
        // Medium optimization: mip generation, mild keyframe reduction
        s.compressTextures    = false;
        s.generateMips        = true;
        s.maxTextureDim       = 2048;
        s.reorderForGPUCache  = true;
        s.quantizePositions   = false;
        s.pruneUnusedVariants = true;
        s.validateUniforms    = true;
        s.stripEditorData     = true;
        s.stripDebugSymbols   = false;
        s.compressPackage     = false;
        s.bakePVS             = true;
        s.buildHLOD           = true;
        s.reduceAnimKeyframes = true;
        s.animKeyEpsilon      = 0.002f;
        s.includeDebugNames   = true;
        s.lodReductionRatio[0] = 1.0f;
        s.lodReductionRatio[1] = 0.4f;
        s.lodReductionRatio[2] = 0.2f;
        break;

    case ExportProfile::Shipping:
        // Full optimization: compression, strict stripping, all passes enabled
        s.compressTextures    = true;
        s.generateMips        = true;
        s.maxTextureDim       = 2048;
        s.reorderForGPUCache  = true;
        s.quantizePositions   = true;
        s.pruneUnusedVariants = true;
        s.validateUniforms    = true;
        s.stripEditorData     = true;
        s.stripDebugSymbols   = true;
        s.compressPackage     = true;
        s.bakePVS             = true;
        s.buildHLOD           = true;
        s.reduceAnimKeyframes = true;
        s.animKeyEpsilon      = 0.003f;
        s.includeDebugNames   = false;
        s.lodReductionRatio[0] = 1.0f;
        s.lodReductionRatio[1] = 0.35f;
        s.lodReductionRatio[2] = 0.15f;
        break;
    }
    return s;
}

// ---------------------------------------------------------------------------
// EstimateTextureVRAM — raw VRAM bytes for a texture after cooking
// ---------------------------------------------------------------------------
uint64_t Cooker::EstimateTextureVRAM(const TextureDesc& desc, const CookSettings& s) {
    if (desc.width <= 0 || desc.height <= 0) return 0;

    int w = desc.width;
    int h = desc.height;
    if (s.maxTextureDim > 0) {
        w = std::min(w, s.maxTextureDim);
        h = std::min(h, s.maxTextureDim);
    }

    // Base: 4 bytes per pixel (RGBA8 uncompressed)
    uint64_t baseBytes = static_cast<uint64_t>(w) * h * 4;

    // Mip chain adds ~33% overhead
    if (s.generateMips) baseBytes = baseBytes * 4 / 3;

    // BC7 / DXT compression reduces to ~1 byte per pixel (4:1 for BC1, 1:1 for BC7)
    if (s.compressTextures) baseBytes /= 4;

    return baseBytes;
}

// ---------------------------------------------------------------------------
// EstimateMeshVRAM — VBO + IBO estimate
// ---------------------------------------------------------------------------
uint64_t Cooker::EstimateMeshVRAM(const MeshDesc& desc, const CookSettings& s) {
    // Stride: 12 (pos) + 12 (normal) + 12 (tangent) + 8 (uv0) + 16 (color) = 60 bytes
    // With quantization: ~40 bytes per vertex
    const uint32_t vertexStride = s.quantizePositions ? 40u : 60u;
    const uint64_t vboBytes = static_cast<uint64_t>(desc.vertexCount) * vertexStride;
    // IBO: 3 indices × 4 bytes per triangle
    const uint64_t iboBytes = static_cast<uint64_t>(desc.triangleCount) * 3 * 4;
    return vboBytes + iboBytes;
}

// ---------------------------------------------------------------------------
// CookMesh — validate + accumulate VRAM estimate
// ---------------------------------------------------------------------------
void Cooker::CookMesh(const MeshDesc& desc, const CookSettings& s, ExportContext& ctx) {
    if (desc.sourcePath.empty()) {
        ctx.AddWarning(desc.name, "Mesh has empty sourcePath");
        return;
    }
    const uint64_t vram = EstimateMeshVRAM(desc, s);
    ctx.report.totalVRAMEstimateBytes += vram;

    if (vram > 64 * 1024 * 1024u) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Mesh VRAM estimate %.1f MB exceeds 64 MB per-mesh budget",
                      static_cast<double>(vram) / (1024.0 * 1024.0));
        ctx.AddWarning(desc.sourcePath, buf);
    }
    NC::LOGGING::Log("[COOK][MESH] ", desc.name, " verts=", desc.vertexCount,
                     " tris=", desc.triangleCount, " vramEst=", vram / 1024, "KB");
}

// ---------------------------------------------------------------------------
// CookTexture — validate + accumulate VRAM estimate
// ---------------------------------------------------------------------------
void Cooker::CookTexture(const TextureDesc& desc, const CookSettings& s, ExportContext& ctx) {
    if (desc.sourcePath.empty()) {
        ctx.AddWarning(desc.name, "Texture has empty sourcePath");
        return;
    }
    if (desc.width <= 0 || desc.height <= 0) {
        ctx.AddWarning(desc.sourcePath, "Texture has zero dimensions; skipping VRAM estimate");
        return;
    }

    // Check power-of-two (warning only)
    auto isPOT = [](int v) { return v > 0 && (v & (v - 1)) == 0; };
    if (!isPOT(desc.width) || !isPOT(desc.height)) {
        ctx.AddWarning(desc.sourcePath, "Texture is not power-of-two; mip generation may be suboptimal");
    }

    const uint64_t vram = EstimateTextureVRAM(desc, s);
    ctx.report.totalVRAMEstimateBytes += vram;
    ctx.report.topOffenders.push_back({ desc.name, vram });

    NC::LOGGING::Log("[COOK][TEX] ", desc.name, " ", desc.width, "×", desc.height,
                     " sRGB=", desc.isSRGB, " vramEst=", vram / 1024, "KB");
}

// ---------------------------------------------------------------------------
// CookMaterial — validate texture slot references
// ---------------------------------------------------------------------------
void Cooker::CookMaterial(const MaterialDesc& desc, const CookSettings& s, ExportContext& ctx) {
    if (desc.shaderName.empty()) {
        ctx.AddWarning(desc.name, "Material references empty shader name");
    }
    if (s.stripEditorData && desc.floatParams.count("editorVisible")) {
        NC::LOGGING::Log("[COOK][MAT] ", desc.name, " editor-only params stripped");
    }
    NC::LOGGING::Log("[COOK][MAT] ", desc.name, " shader=", desc.shaderName,
                     " texSlots=", desc.textureSlots.size());
}

// ---------------------------------------------------------------------------
// EntityWorldMatrix — position + xyzw quaternion + scale → mat4
// ---------------------------------------------------------------------------
glm::mat4 Cooker::EntityWorldMatrix(const EntityDesc& e) {
    const glm::mat4 T = glm::translate(glm::mat4(1.0f), e.position);
    const glm::quat q(e.rotation.w, e.rotation.x, e.rotation.y, e.rotation.z);
    const glm::mat4 R = glm::mat4_cast(q);
    const glm::mat4 S = glm::scale(glm::mat4(1.0f), e.scale);
    return T * R * S;
}

// ---------------------------------------------------------------------------
// CookScene — build ExportDrawProxy list and ExportVisCell grid
// ---------------------------------------------------------------------------
void Cooker::CookScene(const SceneDesc& scene,
                       const std::vector<MeshDesc>& meshDescs,
                       const CookSettings& s,
                       ExportContext& ctx) {
    // Build a name → MeshDesc lookup
    std::unordered_map<std::string, const MeshDesc*> meshByName;
    for (const MeshDesc& m : meshDescs) meshByName[m.name] = &m;

    // Build draw proxies from entities
    ctx.solidDraws.clear();
    ctx.alphaDraws.clear();

    for (const EntityDesc& e : scene.entities) {
        ExportDrawProxy proxy;
        proxy.worldMatrix = EntityWorldMatrix(e);
        proxy.isStatic    = e.isStatic;
        proxy.shaderName  = e.templateName; // conservative: template as shader hint

        proxy.isAlpha  = e.isAlpha;
        proxy.isDecal  = e.isDecal;

        const MeshDesc* mesh = nullptr;
        auto it = meshByName.find(e.templateName);
        if (it != meshByName.end()) mesh = it->second;

        if (mesh) {
            // Transform local bounds to world space
            const glm::vec3 corners[8] = {
                { mesh->boundsMin.x, mesh->boundsMin.y, mesh->boundsMin.z },
                { mesh->boundsMax.x, mesh->boundsMin.y, mesh->boundsMin.z },
                { mesh->boundsMin.x, mesh->boundsMax.y, mesh->boundsMin.z },
                { mesh->boundsMax.x, mesh->boundsMax.y, mesh->boundsMin.z },
                { mesh->boundsMin.x, mesh->boundsMin.y, mesh->boundsMax.z },
                { mesh->boundsMax.x, mesh->boundsMin.y, mesh->boundsMax.z },
                { mesh->boundsMin.x, mesh->boundsMax.y, mesh->boundsMax.z },
                { mesh->boundsMax.x, mesh->boundsMax.y, mesh->boundsMax.z },
            };
            glm::vec3 wMin(std::numeric_limits<float>::max());
            glm::vec3 wMax(std::numeric_limits<float>::lowest());
            for (const glm::vec3& c : corners) {
                const glm::vec3 w = glm::vec3(proxy.worldMatrix * glm::vec4(c, 1.0f));
                wMin = glm::min(wMin, w);
                wMax = glm::max(wMax, w);
            }
            proxy.worldBoundsMin = wMin;
            proxy.worldBoundsMax = wMax;
        } else {
            // Fallback: point bound at entity position
            const glm::vec3 pos(proxy.worldMatrix[3]);
            proxy.worldBoundsMin = pos;
            proxy.worldBoundsMax = pos;
            ctx.AddWarning(e.name, "Entity template '" + e.templateName + "' not found in meshDescs");
        }

        if (e.isAlpha || e.isDecal) {
            ctx.alphaDraws.push_back(std::move(proxy));
        } else {
            ctx.solidDraws.push_back(std::move(proxy));
        }
    }

    // Build ExportVisCell grid from map info
    const MapInfo& info = scene.mapInfo;
    ctx.visGridW = info.map_size_x > 0 ? info.map_size_x : 32;
    ctx.visGridH = info.map_size_z > 0 ? info.map_size_z : 32;
    ctx.visCellSizeX = info.grid_size.x > 0.0f ? info.grid_size.x : 32.0f;
    ctx.visCellSizeZ = info.grid_size.z > 0.0f ? info.grid_size.z : 32.0f;
    ctx.visGridOriginXZ = glm::vec2(info.center.x - info.extents.x,
                                    info.center.z - info.extents.z);

    const int totalCells = ctx.visGridW * ctx.visGridH;
    ctx.visCells.resize(totalCells);
    for (int z = 0; z < ctx.visGridH; ++z) {
        for (int x = 0; x < ctx.visGridW; ++x) {
            ExportVisCell& cell = ctx.visCells[z * ctx.visGridW + x];
            cell.cellIndex = z * ctx.visGridW + x;
            cell.minXZ = ctx.visGridOriginXZ + glm::vec2(x * ctx.visCellSizeX, z * ctx.visCellSizeZ);
            cell.maxXZ = cell.minXZ + glm::vec2(ctx.visCellSizeX, ctx.visCellSizeZ);
        }
    }

    // Assign draws to cells
    for (uint32_t i = 0; i < static_cast<uint32_t>(ctx.solidDraws.size()); ++i) {
        const ExportDrawProxy& proxy = ctx.solidDraws[i];
        if (!proxy.isStatic) continue;

        const glm::vec3 wMin = proxy.worldBoundsMin;
        const glm::vec3 wMax = proxy.worldBoundsMax;

        int cxMin = static_cast<int>(std::floor((wMin.x - ctx.visGridOriginXZ.x) / ctx.visCellSizeX));
        int cxMax = static_cast<int>(std::floor((wMax.x - ctx.visGridOriginXZ.x) / ctx.visCellSizeX));
        int czMin = static_cast<int>(std::floor((wMin.z - ctx.visGridOriginXZ.y) / ctx.visCellSizeZ));
        int czMax = static_cast<int>(std::floor((wMax.z - ctx.visGridOriginXZ.y) / ctx.visCellSizeZ));

        cxMin = std::max(0, cxMin); cxMax = std::min(ctx.visGridW - 1, cxMax);
        czMin = std::max(0, czMin); czMax = std::min(ctx.visGridH - 1, czMax);

        for (int cz = czMin; cz <= czMax; ++cz) {
            for (int cx = cxMin; cx <= cxMax; ++cx) {
                ctx.visCells[cz * ctx.visGridW + cx].drawIndices.push_back(i);
            }
        }
    }

    ctx.report.totalDrawCalls = static_cast<int>(ctx.solidDraws.size() + ctx.alphaDraws.size());

    NC::LOGGING::Log("[COOK][SCENE] ", scene.mapName,
                     " entities=", scene.entities.size(),
                     " solidDraws=", ctx.solidDraws.size(),
                     " cells=", totalCells,
                     " gridW=", ctx.visGridW, " gridH=", ctx.visGridH);
}

// ---------------------------------------------------------------------------
// ReduceFloatKeys — greedy forward-pass keyframe elimination
// ---------------------------------------------------------------------------
std::vector<AnimKey<float>> Cooker::ReduceFloatKeys(
    const std::vector<AnimKey<float>>& keys, float epsilon)
{
    if (keys.size() <= 2) return keys;

    std::vector<AnimKey<float>> result;
    result.reserve(keys.size());
    result.push_back(keys.front());

    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        const float t0 = result.back().time;
        const float t1 = keys[i + 1].time;
        if (t1 <= t0) continue; // degenerate span
        const float t = (keys[i].time - t0) / (t1 - t0);
        const float interp = result.back().value + t * (keys[i + 1].value - result.back().value);
        if (std::abs(keys[i].value - interp) > epsilon) {
            result.push_back(keys[i]);
        }
    }

    result.push_back(keys.back());
    return result;
}

// ---------------------------------------------------------------------------
// ReduceVec4Keys — greedy forward-pass keyframe elimination for vec4 channels
// ---------------------------------------------------------------------------
std::vector<AnimKey<glm::vec4>> Cooker::ReduceVec4Keys(
    const std::vector<AnimKey<glm::vec4>>& keys, float epsilon)
{
    if (keys.size() <= 2) return keys;

    std::vector<AnimKey<glm::vec4>> result;
    result.reserve(keys.size());
    result.push_back(keys.front());

    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        const float t0 = result.back().time;
        const float t1 = keys[i + 1].time;
        if (t1 <= t0) continue;
        const float t = (keys[i].time - t0) / (t1 - t0);
        const glm::vec4 interp = glm::mix(result.back().value, keys[i + 1].value, t);
        if (glm::length(keys[i].value - interp) > epsilon) {
            result.push_back(keys[i]);
        }
    }

    result.push_back(keys.back());
    return result;
}

// ---------------------------------------------------------------------------
// CookAnimation — apply keyframe reduction to an AnimSection
// ---------------------------------------------------------------------------
void Cooker::CookAnimation(const AnimSection& src, const CookSettings& s, AnimSection& out) {
    out = src; // copy all metadata and int/string fields

    if (!s.reduceAnimKeyframes) return;

    const float eps = s.animKeyEpsilon;
    const int beforePos   = static_cast<int>(src.posArray.size());
    const int beforeEuler = static_cast<int>(src.eulerArray.size());
    const int beforeScale = static_cast<int>(src.scaleArray.size());
    const int beforeFloat = static_cast<int>(src.floatKeyArray.size());
    const int beforeF4    = static_cast<int>(src.float4KeyArray.size());

    out.posArray       = ReduceVec4Keys(src.posArray,       eps);
    out.eulerArray     = ReduceVec4Keys(src.eulerArray,     eps);
    out.scaleArray     = ReduceVec4Keys(src.scaleArray,     eps);
    out.floatKeyArray  = ReduceFloatKeys(src.floatKeyArray, eps);
    out.float4KeyArray = ReduceVec4Keys(src.float4KeyArray, eps);
    // int keys: no reduction (discrete values)
    out.intKeyArray    = src.intKeyArray;

    const int saved = (beforePos   - static_cast<int>(out.posArray.size())) +
                      (beforeEuler - static_cast<int>(out.eulerArray.size())) +
                      (beforeScale - static_cast<int>(out.scaleArray.size())) +
                      (beforeFloat - static_cast<int>(out.floatKeyArray.size())) +
                      (beforeF4    - static_cast<int>(out.float4KeyArray.size()));

    if (saved > 0) {
        NC::LOGGING::Log("[COOK][ANIM] '", src.animationName, "' reduced by ", saved, " keyframes");
    }
}

} // namespace NDEVC::Export
