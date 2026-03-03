// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EXPORT_TYPES_H
#define NDEVC_EXPORT_TYPES_H

#include "glm.hpp"
#include "Assets/Map/MapHeader.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// Export profiles — Work / Playtest / Shipping
// ---------------------------------------------------------------------------
enum class ExportProfile : uint8_t { Work = 0, Playtest = 1, Shipping = 2 };

// ---------------------------------------------------------------------------
// Cook settings — derived from profile, overridable per-asset
// ---------------------------------------------------------------------------
struct CookSettings {
    // Texture
    bool  compressTextures    = false;
    bool  generateMips        = true;
    int   maxTextureDim       = 0;      // 0 = unlimited

    // Mesh
    float lodReductionRatio[3] = { 1.0f, 0.5f, 0.25f };
    bool  reorderForGPUCache  = false;
    bool  quantizePositions   = false;

    // Shader
    bool  pruneUnusedVariants = false;
    bool  validateUniforms    = true;

    // Content strip
    bool  stripEditorData     = false;
    bool  stripDebugSymbols   = false;

    // Package
    bool  compressPackage     = false;
    bool  encryptPackage      = false;

    // Optimization passes
    bool  bakePVS             = false;
    bool  buildHLOD           = false;
    bool  reduceAnimKeyframes = false;
    float animKeyEpsilon      = 0.001f;

    // Debug
    bool  includeDebugNames   = true;
};

// ---------------------------------------------------------------------------
// Budget limits — enforced by ValidationGates
// ---------------------------------------------------------------------------
struct ExportBudgets {
    uint64_t maxVRAMBytes          = 0; // 0 = unlimited
    int      maxDrawCalls          = 0; // 0 = unlimited
    float    maxFrameCostEstimate  = 0.0f;
    uint64_t maxPackageSizeBytes   = 0; // 0 = unlimited
};

// ---------------------------------------------------------------------------
// Issue + report
// ---------------------------------------------------------------------------
struct ExportIssue {
    enum class Severity : uint8_t { Warning = 0, Error = 1 };
    Severity    severity = Severity::Warning;
    std::string assetPath;
    std::string message;
};

struct ExportReport {
    bool     success                 = true;
    uint64_t totalVRAMEstimateBytes  = 0;
    int      totalDrawCalls          = 0;
    float    frameCostEstimate       = 0.0f;
    uint64_t totalPackageSizeBytes   = 0;
    std::vector<ExportIssue>                       issues;
    std::vector<std::pair<std::string, uint64_t>>  topOffenders; // name, vram
};

// ---------------------------------------------------------------------------
// Stable asset descriptors — game data enters through these only, never raw ptrs
// ---------------------------------------------------------------------------
struct MeshDesc {
    std::string sourcePath;
    std::string name;
    bool        isStatic      = true;
    glm::vec3   boundsMin     { 0.0f };
    glm::vec3   boundsMax     { 0.0f };
    uint32_t    vertexCount   = 0;
    uint32_t    triangleCount = 0;
};

struct TextureDesc {
    std::string sourcePath;
    std::string name;
    int         width        = 0;
    int         height       = 0;
    bool        isSRGB       = true;
    bool        generateMips = true;
    bool        isNormalMap  = false;
};

struct MaterialDesc {
    std::string name;
    std::string shaderName;
    std::unordered_map<std::string, std::string> textureSlots;
    std::unordered_map<std::string, float>       floatParams;
    std::unordered_map<std::string, int>         intParams;
};

struct EntityDesc {
    std::string name;
    std::string templateName; // maps to a MeshDesc by name
    glm::vec3   position { 0.0f };
    glm::vec4   rotation { 0.0f, 0.0f, 0.0f, 1.0f }; // xyzw quaternion
    glm::vec3   scale    { 1.0f };
    bool        isStatic = true;
    bool        isAlpha  = false; // true for alpha-test draws
    bool        isDecal  = false; // true for decal draws
};

struct SceneDesc {
    std::string sceneGuid;
    std::string sceneName;
    std::string mapPath;
    std::string mapName;
    MapInfo     mapInfo {};
    std::vector<EntityDesc>  entities;
    std::vector<std::string> requiredModelPaths;
    std::vector<std::string> requiredMeshPaths;
    std::vector<std::string> requiredAnimPaths;
    std::vector<std::string> requiredTexturePaths;
    std::vector<std::string> requiredShaderNames;
};

// ---------------------------------------------------------------------------
// Lightweight draw proxy — no GL dependency, used by export optimizer
// ---------------------------------------------------------------------------
struct ExportDrawProxy {
    glm::mat4   worldMatrix   { 1.0f };
    glm::vec3   worldBoundsMin{ 0.0f };
    glm::vec3   worldBoundsMax{ 0.0f };
    std::string shaderName;
    bool        isStatic = true;
    bool        isAlpha  = false;
    bool        isDecal  = false;
};

// ---------------------------------------------------------------------------
// Visibility cell proxy — mirrors VisibilityCell without GL dependency
// ---------------------------------------------------------------------------
struct ExportVisCell {
    glm::vec2            minXZ     { 0.0f };
    glm::vec2            maxXZ     { 0.0f };
    std::vector<uint32_t> drawIndices; // indices into solidDraws array
    int                  cellIndex = 0;

    int drawCount() const { return static_cast<int>(drawIndices.size()); }
};

// ---------------------------------------------------------------------------
// Optimization outputs
// ---------------------------------------------------------------------------
struct PVSData {
    int  cellCount = 0;
    // cellVisibility[i] = sorted list of cell indices visible from cell i
    std::vector<std::vector<int>> cellVisibility;
    bool valid = false;
};

struct HLODCell {
    int       cellIndex = 0;
    glm::vec3 boundsMin { 0.0f };
    glm::vec3 boundsMax { 0.0f };
    int       drawCount = 0;
};

struct HLODData {
    std::vector<HLODCell> cells;
    int highDetailRadiusChunks = 2;
    bool valid = false;
};

struct PrunedVariants {
    // shaderName -> set of variant-define strings that were actually used
    // Each entry is a sorted, space-separated define string, e.g. "ALPHA_TEST INSTANCING"
    std::unordered_map<std::string, std::vector<std::string>> activeVariants;
    int totalPossibleCount = 0;
    int prunedCount        = 0;
    bool valid = false;
};

// ---------------------------------------------------------------------------
// Package manifest — written into the archive, used by runtime
// ---------------------------------------------------------------------------
struct PackageManifest {
    static constexpr uint32_t kCurrentVersion = 1;

    uint32_t      version     = kCurrentVersion;
    uint64_t      buildTime   = 0;   // unix epoch seconds
    uint64_t      contentHash = 0;   // XOR of all asset hashes
    ExportProfile profile     = ExportProfile::Work;

    struct ChunkInfo {
        std::string name;
        uint64_t    hash        = 0;
        uint64_t    sizeBytes   = 0;
        uint64_t    offsetBytes = 0;
        bool        preload     = false;
        bool        compressed  = false;
    };

    struct AssetInfo {
        std::string assetPath;
        uint32_t    chunkId   = 0;
        uint64_t    assetHash = 0;
    };

    std::vector<ChunkInfo> chunks;
    std::vector<AssetInfo> assets;
};

// ---------------------------------------------------------------------------
// ExportContext — mutable state shared by all pipeline nodes
// ---------------------------------------------------------------------------
struct ExportContext {
    ExportProfile  profile  = ExportProfile::Work;
    CookSettings   settings {};
    ExportBudgets  budgets  {};
    ExportReport   report   {};
    std::string    outputDir;

    // Asset descriptors (populated by ImportNode)
    SceneDesc                 sourceScene;
    std::vector<MeshDesc>     meshDescs;
    std::vector<TextureDesc>  textureDescs;
    std::vector<MaterialDesc> materialDescs;

    // Draw proxies (populated by CookNode)
    std::vector<ExportDrawProxy> solidDraws;
    std::vector<ExportDrawProxy> alphaDraws;

    // Visibility grid (populated by CookNode)
    std::vector<ExportVisCell> visCells;
    int       visGridW         = 0;
    int       visGridH         = 0;
    glm::vec2 visGridOriginXZ  { 0.0f };
    float     visCellSizeX     = 32.0f;
    float     visCellSizeZ     = 32.0f;

    // Optimizer outputs (populated by OptimizeNode)
    PVSData        pvsData        {};
    HLODData       hlodData       {};
    PrunedVariants prunedVariants {};

    // Package output (populated by PackageNode)
    std::string     outputPackagePath;
    PackageManifest manifest        {};

    // Helpers
    void AddError(const std::string& assetPath, const std::string& msg) {
        report.success = false;
        report.issues.push_back({ ExportIssue::Severity::Error, assetPath, msg });
    }
    void AddWarning(const std::string& assetPath, const std::string& msg) {
        report.issues.push_back({ ExportIssue::Severity::Warning, assetPath, msg });
    }
};

} // namespace NDEVC::Export

#endif // NDEVC_EXPORT_TYPES_H
