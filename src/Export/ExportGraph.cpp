// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/ExportGraph.h"
#include "Export/Cooker.h"
#include "Export/SceneExportOptimizer.h"
#include "Export/Package.h"
#include "Export/ValidationGates.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// ExportGraph — node management and topological execution
// ---------------------------------------------------------------------------
void ExportGraph::AddNode(std::unique_ptr<IExportNode> node)
{
    nodes_.push_back(std::move(node));
}

// ---------------------------------------------------------------------------
// TopoSort — Kahn's algorithm on output-key → input-key edges
//
// An edge A→B exists if A produces a key that B consumes.
// Returns nodes in valid execution order, or empty if a cycle is detected.
// ---------------------------------------------------------------------------
std::vector<IExportNode*> ExportGraph::TopoSort() const
{
    const int N = static_cast<int>(nodes_.size());

    // Build: key → producer node index
    std::unordered_map<std::string, int> keyProducer;
    for (int i = 0; i < N; ++i) {
        for (const std::string& k : nodes_[i]->GetOutputKeys()) {
            keyProducer[k] = i;
        }
    }

    // Build in-degree and adjacency list
    std::vector<int>              inDegree(N, 0);
    std::vector<std::vector<int>> adj(N);

    for (int i = 0; i < N; ++i) {
        std::unordered_set<int> addedDeps;
        for (const std::string& k : nodes_[i]->GetInputKeys()) {
            auto it = keyProducer.find(k);
            if (it == keyProducer.end()) continue;
            const int prod = it->second;
            if (prod == i) continue;
            if (addedDeps.insert(prod).second) {
                adj[prod].push_back(i);
                ++inDegree[i];
            }
        }
    }

    // Kahn's BFS
    std::vector<int> queue;
    for (int i = 0; i < N; ++i) {
        if (inDegree[i] == 0) queue.push_back(i);
    }

    std::vector<IExportNode*> sorted;
    sorted.reserve(N);
    size_t head = 0;
    while (head < queue.size()) {
        const int cur = queue[head++];
        sorted.push_back(nodes_[cur].get());
        for (int next : adj[cur]) {
            if (--inDegree[next] == 0) queue.push_back(next);
        }
    }

    if (static_cast<int>(sorted.size()) != N) {
        NC::LOGGING::Error("[GRAPH] Cycle detected in export graph — aborting");
        return {};
    }
    return sorted;
}

// ---------------------------------------------------------------------------
// Execute — run nodes in topological order
// ---------------------------------------------------------------------------
bool ExportGraph::Execute(ExportContext& ctx)
{
    const std::vector<IExportNode*> order = TopoSort();
    if (order.empty() && !nodes_.empty()) return false;

    NC::LOGGING::Log("[GRAPH] Starting export pipeline (", order.size(), " nodes)");

    for (IExportNode* node : order) {
        NC::LOGGING::Log("[GRAPH] >> ", node->GetName());
        if (!node->Execute(ctx)) {
            NC::LOGGING::Error("[GRAPH] Node '", node->GetName(), "' failed — pipeline aborted");
            ctx.report.success = false;
            return false;
        }
    }

    NC::LOGGING::Log("[GRAPH] Pipeline complete. success=", ctx.report.success,
                     " issues=", ctx.report.issues.size());
    return ctx.report.success;
}

// ---------------------------------------------------------------------------
// LogTopology
// ---------------------------------------------------------------------------
void ExportGraph::LogTopology() const
{
    NC::LOGGING::Log("[GRAPH] Topology (", nodes_.size(), " nodes):");
    for (const auto& n : nodes_) {
        const auto ins  = n->GetInputKeys();
        const auto outs = n->GetOutputKeys();
        std::string inStr, outStr;
        for (const auto& k : ins)  { if (!inStr.empty()) inStr  += ","; inStr  += k; }
        for (const auto& k : outs) { if (!outStr.empty()) outStr += ","; outStr += k; }
        NC::LOGGING::Log("  [", n->GetName(), "] in=[", inStr, "] out=[", outStr, "]");
    }
}

// ===========================================================================
// ImportNode::Execute
// Validates sourceScene and populates mesh/texture/material desc lists.
// ===========================================================================
bool ImportNode::Execute(ExportContext& ctx)
{
    if (ctx.sourceScene.mapPath.empty()) {
        ctx.AddWarning("scene", "sourceScene.mapPath is empty — exporting loaded assets without startup map");
    }

    // Build MeshDesc list from requiredMeshPaths (name = filename without extension)
    ctx.meshDescs.clear();
    for (const std::string& path : ctx.sourceScene.requiredMeshPaths) {
        MeshDesc m;
        m.sourcePath = path;
        std::filesystem::path fp(path);
        m.name = fp.stem().string();
        ctx.meshDescs.push_back(std::move(m));
    }

    // Build TextureDesc list from requiredTexturePaths
    ctx.textureDescs.clear();
    for (const std::string& path : ctx.sourceScene.requiredTexturePaths) {
        TextureDesc t;
        t.sourcePath = path;
        std::filesystem::path fp(path);
        t.name = fp.stem().string();
        // Guess sRGB from name convention (not "_n" = normal map, not "_r" = roughness)
        const std::string stem = fp.stem().string();
        t.isNormalMap = (stem.size() >= 2 && stem.substr(stem.size() - 2) == "_n");
        t.isSRGB      = !t.isNormalMap;
        ctx.textureDescs.push_back(std::move(t));
    }

    // Build MaterialDesc stubs from shaders referenced
    ctx.materialDescs.clear();
    for (const std::string& shaderName : ctx.sourceScene.requiredShaderNames) {
        MaterialDesc mat;
        mat.name       = shaderName;
        mat.shaderName = shaderName;
        ctx.materialDescs.push_back(std::move(mat));
    }

    NC::LOGGING::Log("[IMPORT] scene=", ctx.sourceScene.mapName,
                     " models=", ctx.sourceScene.requiredModelPaths.size(),
                     " meshes=", ctx.meshDescs.size(),
                     " anims=", ctx.sourceScene.requiredAnimPaths.size(),
                     " textures=", ctx.textureDescs.size(),
                     " materials=", ctx.materialDescs.size(),
                     " entities=", ctx.sourceScene.entities.size());
    return true;
}

// ===========================================================================
// CookNode::Execute
// Applies profile settings, builds draw proxies and visibility cells.
// ===========================================================================
bool CookNode::Execute(ExportContext& ctx)
{
    // Cook each asset class
    for (const MeshDesc& m : ctx.meshDescs) {
        Cooker::CookMesh(m, ctx.settings, ctx);
    }
    for (const TextureDesc& t : ctx.textureDescs) {
        Cooker::CookTexture(t, ctx.settings, ctx);
    }
    for (const MaterialDesc& mat : ctx.materialDescs) {
        Cooker::CookMaterial(mat, ctx.settings, ctx);
    }

    // Cook scene — builds draw proxies + vis cells
    Cooker::CookScene(ctx.sourceScene, ctx.meshDescs, ctx.settings, ctx);

    NC::LOGGING::Log("[COOK] solidDraws=", ctx.solidDraws.size(),
                     " alphaDraws=", ctx.alphaDraws.size(),
                     " visCells=", ctx.visCells.size(),
                     " vramEst=",
                     static_cast<double>(ctx.report.totalVRAMEstimateBytes) / (1024.0 * 1024.0),
                     "MB");
    return true;
}

// ===========================================================================
// OptimizeNode::Execute
// PVS bake, HLOD generation, shader variant pruning.
// ===========================================================================
bool OptimizeNode::Execute(ExportContext& ctx)
{
    if (ctx.settings.bakePVS && !ctx.visCells.empty()) {
        ctx.pvsData = SceneExportOptimizer::BakePVS(
            ctx.visCells,
            ctx.visGridW, ctx.visGridH,
            ctx.visGridOriginXZ,
            ctx.visCellSizeX, ctx.visCellSizeZ);
    } else if (ctx.settings.bakePVS) {
        NC::LOGGING::Warning("[OPTIMIZE] PVS bake requested but visCells is empty — skipped");
    }

    if (ctx.settings.buildHLOD && !ctx.visCells.empty()) {
        // World center from map info
        const glm::vec3 worldCenter(
            ctx.sourceScene.mapInfo.center.x,
            0.0f,
            ctx.sourceScene.mapInfo.center.z);

        ctx.hlodData = SceneExportOptimizer::BuildHLOD(
            ctx.visCells,
            ctx.solidDraws,
            ctx.visGridW, ctx.visGridH,
            ctx.visGridOriginXZ,
            ctx.visCellSizeX, ctx.visCellSizeZ,
            2, // highDetailRadiusChunks (default; callers may set in settings later)
            worldCenter);
    }

    if (ctx.settings.pruneUnusedVariants) {
        ctx.prunedVariants = SceneExportOptimizer::PruneShaderVariants(
            ctx.solidDraws, ctx.alphaDraws);
    }

    NC::LOGGING::Log("[OPTIMIZE] pvs=", ctx.pvsData.valid,
                     " hlodCells=", ctx.hlodData.cells.size(),
                     " variantsPruned=", ctx.prunedVariants.prunedCount);
    return true;
}

namespace {
bool ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto endPos = f.tellg();
    if (endPos <= 0) {
        out.clear();
        return true;
    }
    const size_t fileSize = static_cast<size_t>(endPos);
    out.resize(fileSize);
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(fileSize));
    return f.good() || f.eof();
}

std::filesystem::path ResolvePathByRoots(const std::filesystem::path& path,
                                         const std::vector<std::filesystem::path>& roots) {
    namespace fs = std::filesystem;
    if (path.empty()) return {};
    std::string token = path.string();
    enum class AssetKind { Unknown, Mesh, Texture, Model, Anim, Shader, Map };
    AssetKind kind = AssetKind::Unknown;
    auto stripPrefix = [&](const char* prefix, AssetKind k) {
        if (token.rfind(prefix, 0) == 0) {
            token = token.substr(std::strlen(prefix));
            kind = k;
        }
    };
    stripPrefix("msh:", AssetKind::Mesh);
    stripPrefix("tex:", AssetKind::Texture);
    stripPrefix("mdl:", AssetKind::Model);
    stripPrefix("mod:", AssetKind::Model);
    stripPrefix("anim:", AssetKind::Anim);
    stripPrefix("ani:", AssetKind::Anim);

    fs::path normalized(token);
    const std::string lowered = normalized.generic_string();
    if (kind == AssetKind::Unknown) {
        if (lowered.find("/shaders/") != std::string::npos) kind = AssetKind::Shader;
        else if (lowered.find("/meshes/") != std::string::npos || normalized.extension() == ".nvx2") kind = AssetKind::Mesh;
        else if (lowered.find("/textures/") != std::string::npos || normalized.extension() == ".dds") kind = AssetKind::Texture;
        else if (lowered.find("/models/") != std::string::npos || normalized.extension() == ".n3") kind = AssetKind::Model;
        else if (lowered.find("/anims/") != std::string::npos || normalized.extension() == ".nax3" || normalized.extension() == ".nac") kind = AssetKind::Anim;
        else if (normalized.extension() == ".map" || lowered.find("/maps/") != std::string::npos) kind = AssetKind::Map;
    }

    std::vector<fs::path> candidates;
    candidates.push_back(normalized);
    if (!normalized.has_extension()) {
        switch (kind) {
        case AssetKind::Mesh:
            candidates.push_back(normalized.string() + ".nvx2");
            break;
        case AssetKind::Texture:
            candidates.push_back(normalized.string() + ".dds");
            candidates.push_back(normalized.string() + ".tga");
            candidates.push_back(normalized.string() + ".png");
            break;
        case AssetKind::Model:
            candidates.push_back(normalized.string() + ".n3");
            break;
        case AssetKind::Anim:
            candidates.push_back(normalized.string() + ".nax3");
            candidates.push_back(normalized.string() + ".nac");
            break;
        case AssetKind::Map:
            candidates.push_back(normalized.string() + ".map");
            break;
        case AssetKind::Shader:
            candidates.push_back(normalized.string() + ".vert");
            candidates.push_back(normalized.string() + ".frag");
            break;
        case AssetKind::Unknown:
            candidates.push_back(normalized.string() + ".nvx2");
            candidates.push_back(normalized.string() + ".dds");
            candidates.push_back(normalized.string() + ".n3");
            candidates.push_back(normalized.string() + ".nax3");
            candidates.push_back(normalized.string() + ".map");
            break;
        }
    }

    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (candidate.is_absolute() && fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return fs::absolute(candidate, ec);
        }
    }
    for (const fs::path& root : roots) {
        if (root.empty()) continue;
        for (const fs::path& relative : candidates) {
            const fs::path candidate = root / relative;
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

std::string MakeAssetChunkName(uint32_t assetChunkIndex) {
    char name[16] = {};
    std::snprintf(name, sizeof(name), "asset%06u", assetChunkIndex);
    return std::string(name);
}
} // namespace

// ===========================================================================
// PackageNode::Execute
// Writes NDPK archive and manifest.
// ===========================================================================
bool PackageNode::Execute(ExportContext& ctx)
{
    if (ctx.outputDir.empty()) {
        ctx.AddError("package", "outputDir is empty — cannot write package");
        return false;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(ctx.outputDir, ec);
    if (ec) {
        ctx.AddError("package", "Cannot create output directory: " + ec.message());
        return false;
    }

    const std::string mapName = ctx.sourceScene.mapName.empty()
        ? "scene"
        : ctx.sourceScene.mapName;
    ctx.outputPackagePath = (fs::path(ctx.outputDir) / (mapName + ".ndpk")).string();

    PackageWriter writer(ctx.profile);
    ctx.manifest.assets.clear();
    uint32_t chunkId = 0;
    uint32_t assetChunkIndex = 0;

    auto pushChunk = [&](const std::string& name, std::vector<uint8_t> data, bool preload) {
        writer.AddChunk(name, std::move(data), preload);
        ++chunkId;
    };

    // Startup chunk: always present.
    {
        std::vector<uint8_t> startupData;
        const std::string tag = "NDPK_STARTUP:" + mapName;
        startupData.insert(startupData.end(), tag.begin(), tag.end());
        pushChunk("startup", std::move(startupData), /*preload=*/true);
    }

    if (ctx.pvsData.valid) {
        pushChunk("pvs", PackageWriter::SerializePVS(ctx.pvsData), /*preload=*/true);
    }

    if (ctx.hlodData.valid) {
        pushChunk("hlod", PackageWriter::SerializeHLOD(ctx.hlodData), /*preload=*/false);
    }

    // Scene metadata chunk (scene identity + map reference + authored entities).
    {
        std::vector<uint8_t> sceneData;
        sceneData.reserve(256 + ctx.sourceScene.entities.size() * 96);
        auto appendU16 = [&sceneData](uint16_t v) {
            sceneData.push_back(static_cast<uint8_t>(v & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        };
        auto appendU32 = [&sceneData](uint32_t v) {
            sceneData.push_back(static_cast<uint8_t>(v & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
        };
        auto appendF32 = [&sceneData](float v) {
            uint32_t raw = 0;
            static_assert(sizeof(uint32_t) == sizeof(float), "f32 packing assumption");
            std::memcpy(&raw, &v, sizeof(uint32_t));
            sceneData.push_back(static_cast<uint8_t>(raw & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((raw >> 8) & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((raw >> 16) & 0xFFu));
            sceneData.push_back(static_cast<uint8_t>((raw >> 24) & 0xFFu));
        };
        auto appendStr = [&sceneData, &appendU16](const std::string& s) {
            const uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 65535));
            appendU16(len);
            sceneData.insert(sceneData.end(), s.begin(), s.begin() + len);
        };

        sceneData.insert(sceneData.end(), {'N','D','S','C','E','N','E','2'});
        appendU32(2u);
        appendStr(ctx.sourceScene.sceneGuid);
        appendStr(ctx.sourceScene.sceneName.empty() ? ctx.sourceScene.mapName : ctx.sourceScene.sceneName);
        appendStr(ctx.sourceScene.mapName);
        appendStr(ctx.sourceScene.mapPath);
        appendU32(static_cast<uint32_t>(ctx.sourceScene.entities.size()));

        for (const EntityDesc& entity : ctx.sourceScene.entities) {
            appendStr(entity.name);
            appendStr(entity.templateName);
            appendF32(entity.position.x);
            appendF32(entity.position.y);
            appendF32(entity.position.z);
            appendF32(entity.rotation.x);
            appendF32(entity.rotation.y);
            appendF32(entity.rotation.z);
            appendF32(entity.rotation.w);
            appendF32(entity.scale.x);
            appendF32(entity.scale.y);
            appendF32(entity.scale.z);
            uint8_t flags = 0;
            if (entity.isStatic) flags |= 0x1u;
            if (entity.isAlpha)  flags |= 0x2u;
            if (entity.isDecal)  flags |= 0x4u;
            sceneData.push_back(flags);
        }
        pushChunk("scene", std::move(sceneData), /*preload=*/true);
    }

    // Resolve roots used when source paths are relative.
    std::vector<fs::path> roots;
    const char* modelsRoot   = std::getenv("NDEVC_MODELS_ROOT");
    const char* meshesRoot   = std::getenv("NDEVC_MESHES_ROOT");
    const char* animsRoot    = std::getenv("NDEVC_ANIMS_ROOT");
    const char* texturesRoot = std::getenv("NDEVC_TEXTURES_ROOT");
    const char* mapsRoot     = std::getenv("NDEVC_MAPS_ROOT");
    const char* sourceDir    = std::getenv("NDEVC_SOURCE_DIR");
    if (modelsRoot && modelsRoot[0]) roots.emplace_back(modelsRoot);
    if (meshesRoot && meshesRoot[0]) roots.emplace_back(meshesRoot);
    if (animsRoot && animsRoot[0]) roots.emplace_back(animsRoot);
    if (texturesRoot && texturesRoot[0]) roots.emplace_back(texturesRoot);
    if (mapsRoot && mapsRoot[0]) roots.emplace_back(mapsRoot);
    if (sourceDir && sourceDir[0]) roots.emplace_back(fs::path(sourceDir));
    roots.emplace_back(fs::path(SOURCE_DIR));

    std::unordered_set<std::string> packedAbsolutePaths;

    auto packFileAsset = [&](const std::string& logicalPath, bool preload, bool required = true) {
        if (logicalPath.empty()) return;
        const fs::path resolved = ResolvePathByRoots(fs::path(logicalPath), roots);
        if (resolved.empty()) {
            if (required) {
                ctx.AddWarning(logicalPath, "Asset file not found during packaging");
            }
            return;
        }
        const std::string resolvedKey = fs::weakly_canonical(resolved).string();
        if (!packedAbsolutePaths.insert(resolvedKey).second) {
            return;
        }
        std::vector<uint8_t> bytes;
        if (!ReadWholeFile(resolved, bytes)) {
            ctx.AddWarning(logicalPath, "Failed to read asset bytes during packaging");
            return;
        }
        auto makeAssetId = [&](const fs::path& p) -> std::string {
            const std::string s = p.generic_string();
            static const char* markers[] = {
                "/maps/", "/meshes/", "/models/", "/textures/", "/anims/", "/shaders/"
            };
            for (const char* m : markers) {
                const size_t pos = s.find(m);
                if (pos != std::string::npos) {
                    return s.substr(pos + 1);
                }
            }
            std::string logical = fs::path(logicalPath).generic_string();
            if (!logical.empty()) {
                const char* prefixes[] = {"msh:", "tex:", "mdl:", "mod:", "anim:", "ani:"};
                for (const char* prefix : prefixes) {
                    if (logical.rfind(prefix, 0) == 0) {
                        logical = logical.substr(std::strlen(prefix));
                        break;
                    }
                }
                while (!logical.empty() && (logical[0] == '/' || logical[0] == '\\')) {
                    logical.erase(logical.begin());
                }
                if (!logical.empty()) return logical;
            }
            return p.generic_string();
        };
        const std::string assetId = makeAssetId(resolved);
        const uint64_t hash = ExportCache::HashBytes(bytes.data(), bytes.size());
        const std::string chunkName = MakeAssetChunkName(assetChunkIndex++);
        writer.AddChunk(chunkName, std::move(bytes), preload);
        ctx.manifest.assets.push_back({
            assetId,
            chunkId,
            hash
        });
        ++chunkId;
    };

    // Pack the selected startup map and all required content bytes into the NDPK.
    packFileAsset(ctx.sourceScene.mapPath, /*preload=*/true);
    for (const std::string& modelPath : ctx.sourceScene.requiredModelPaths) {
        packFileAsset(modelPath, /*preload=*/false);
    }
    for (const MeshDesc& m : ctx.meshDescs) {
        packFileAsset(m.sourcePath, /*preload=*/false);
    }
    for (const std::string& animPath : ctx.sourceScene.requiredAnimPaths) {
        packFileAsset(animPath, /*preload=*/false);
    }
    for (const TextureDesc& t : ctx.textureDescs) {
        packFileAsset(t.sourcePath, /*preload=*/false);
    }
    // Pack ALL shaders from the shaders/ directory.
    // OpenGLShaderManager hardcodes every pipeline shader at init; they must all
    // be present.  Scene-referenced shader names are a subset — pack the whole dir.
    {
        const fs::path srcShaders = fs::path(SOURCE_DIR) / "shaders";
        std::error_code iterEc;
        for (const auto& entry : fs::recursive_directory_iterator(srcShaders, iterEc)) {
            if (!entry.is_regular_file()) continue;
            const fs::path& p = entry.path();
            const auto ext = p.extension().string();
            if (ext == ".vert" || ext == ".frag" || ext == ".geom" ||
                ext == ".comp" || ext == ".tesc" || ext == ".tese") {
                const fs::path rel = fs::relative(p, fs::path(SOURCE_DIR), iterEc);
                if (!iterEc && !rel.empty())
                    packFileAsset(rel.generic_string(), /*preload=*/true, /*required=*/true);
                iterEc.clear();
            }
        }
    }

    const bool ok = writer.Finalize(ctx.outputPackagePath, ctx.manifest);
    if (!ok) {
        ctx.AddError("package", "PackageWriter::Finalize failed for " + ctx.outputPackagePath);
        return false;
    }

    // Report package size
    std::error_code ec2;
    const auto fileSize = fs::file_size(ctx.outputPackagePath, ec2);
    if (!ec2) ctx.report.totalPackageSizeBytes = fileSize;

    return true;
}

// ===========================================================================
// ValidateNode::Execute
// Checks budgets and produces top-offender report.
// ===========================================================================
bool ValidateNode::Execute(ExportContext& ctx)
{
    return ValidationGates::Validate(ctx);
}

// ===========================================================================
// BuildStandardGraph — creates a complete pipeline with all 5 nodes
// ===========================================================================
ExportGraph BuildStandardGraph()
{
    ExportGraph graph;
    graph.AddNode(std::make_unique<ImportNode>());
    graph.AddNode(std::make_unique<CookNode>());
    graph.AddNode(std::make_unique<OptimizeNode>());
    graph.AddNode(std::make_unique<PackageNode>());
    graph.AddNode(std::make_unique<ValidateNode>());
    return graph;
}

} // namespace NDEVC::Export
