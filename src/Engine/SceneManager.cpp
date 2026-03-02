#include "Engine/SceneManager.h"
#include "Rendering/Mesh.h"
#include "Rendering/MegaBuffer.h"
#include "Assets/Model/Model.h"
#include "Assets/Model/ModelServer.h"
#include "Assets/Servers/MeshServer.h"
#include "Assets/Servers/TextureServer.h"
#include "Core/Logger.h"
#include "Rendering/Interfaces/ITexture.h"
#include "glad/glad.h"
#include "gtc/quaternion.hpp"
#include "gtc/matrix_transform.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <unordered_set>

namespace {
glm::mat4 BuildLocalTransform(const Node* node) {
    if (!node) return glm::mat4(1.0f);
    const glm::vec3 pos(node->position.x, node->position.y, node->position.z);
    const glm::quat rot(node->rotation.w, node->rotation.x, node->rotation.y, node->rotation.z);
    const glm::vec3 scl(node->scale.x, node->scale.y, node->scale.z);
    return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scl);
}

bool MatrixIsFinite(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(matrix[c][r])) {
                return false;
            }
        }
    }
    return true;
}

bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b) {
    return std::memcmp(&a, &b, sizeof(glm::mat4)) == 0;
}

bool TryComputeInverse(const glm::mat4& matrix, glm::mat4& outInverse) {
    outInverse = glm::inverse(matrix);
    return MatrixIsFinite(outInverse);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

float GetFloatParam(const std::unordered_map<std::string, float>& values,
                    const char* key,
                    float fallback) {
    if (!key || !*key) return fallback;
    auto it = values.find(key);
    if (it != values.end()) return it->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return value;
        }
    }
    return fallback;
}

int GetIntParam(const std::unordered_map<std::string, int32_t>& values,
                const char* key,
                int fallback) {
    if (!key || !*key) return fallback;
    auto it = values.find(key);
    if (it != values.end()) return it->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return value;
        }
    }
    return fallback;
}

const std::string* FindTextureParamNoCase(const std::unordered_map<std::string, std::string>& values,
                                          const char* key) {
    if (!key || !*key) return nullptr;
    auto exact = values.find(key);
    if (exact != values.end()) return &exact->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return &value;
        }
    }
    return nullptr;
}

const glm::vec4* FindVec4ParamNoCase(const std::unordered_map<std::string, glm::vec4>& values,
                                     const char* key) {
    if (!key || !*key) return nullptr;
    auto exact = values.find(key);
    if (exact != values.end()) return &exact->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return &value;
        }
    }
    return nullptr;
}

bool HasTextureSemanticNoCase(const std::unordered_map<std::string, std::string>& values,
                              std::initializer_list<const char*> semantics) {
    for (const char* semantic : semantics) {
        const std::string* value = FindTextureParamNoCase(values, semantic);
        if (value != nullptr && !value->empty()) {
            return true;
        }
    }
    return false;
}

bool HasNonEmptyTexture(const Node* node, const char* key) {
    if (!node || !key || !*key) return false;
    const std::string* value = FindTextureParamNoCase(node->shader_params_texture, key);
    return value != nullptr && !value->empty();
}

const int32_t* FindIntParamNoCase(const std::unordered_map<std::string, int32_t>& values,
                                  const char* key) {
    if (!key || !*key) return nullptr;
    auto exact = values.find(key);
    if (exact != values.end()) return &exact->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return &value;
        }
    }
    return nullptr;
}

const float* FindFloatParamNoCase(const std::unordered_map<std::string, float>& values,
                                  const char* key) {
    if (!key || !*key) return nullptr;
    auto exact = values.find(key);
    if (exact != values.end()) return &exact->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return &value;
        }
    }
    return nullptr;
}

const std::string* FindStringParamNoCase(const std::unordered_map<std::string, std::string>& values,
                                         const char* key) {
    if (!key || !*key) return nullptr;
    auto exact = values.find(key);
    if (exact != values.end()) return &exact->second;

    const std::string keyLower = ToLower(key);
    for (const auto& [name, value] : values) {
        if (ToLower(name) == keyLower) {
            return &value;
        }
    }
    return nullptr;
}

bool StringIsTruthy(const std::string& value) {
    const std::string lowered = ToLower(value);
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

bool StringIsFalsey(const std::string& value) {
    const std::string lowered = ToLower(value);
    return lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off";
}

bool TryReadNodeBoolParam(const Node* node, const char* key, bool& outValue) {
    if (!node || !key || !*key) return false;

    if (const int32_t* value = FindIntParamNoCase(node->shader_params_int, key)) {
        outValue = (*value != 0);
        return true;
    }
    if (const float* value = FindFloatParamNoCase(node->shader_params_float, key)) {
        outValue = (*value > 0.5f);
        return true;
    }
    if (const std::string* value = FindStringParamNoCase(node->string_attrs, key)) {
        if (StringIsTruthy(*value)) {
            outValue = true;
            return true;
        }
        if (StringIsFalsey(*value)) {
            outValue = false;
            return true;
        }
    }
    return false;
}

enum class NodeMobilityHint {
    None,
    Static,
    Dynamic
};

NodeMobilityHint ResolveNodeMobilityHint(const Node* node) {
    if (!node) {
        return NodeMobilityHint::None;
    }

    bool value = false;
    const char* dynamicKeys[] = {
        "IsDynamic", "Dynamic", "Movable", "IsMovable", "NonStatic"
    };
    for (const char* key : dynamicKeys) {
        if (TryReadNodeBoolParam(node, key, value) && value) {
            return NodeMobilityHint::Dynamic;
        }
    }

    const char* staticKeys[] = {
        "IsStatic", "Static", "StaticNode", "NodeStatic", "RenderStatic"
    };
    for (const char* key : staticKeys) {
        if (TryReadNodeBoolParam(node, key, value)) {
            return value ? NodeMobilityHint::Static : NodeMobilityHint::Dynamic;
        }
    }

    return NodeMobilityHint::None;
}

bool ResolveDecalReceiveSolid(const Node* node) {
    if (!node) return false;

    if (const int32_t* value = FindIntParamNoCase(node->shader_params_int, "DecalReceiveSolid")) {
        return *value != 0;
    }
    if (const float* value = FindFloatParamNoCase(node->shader_params_float, "DecalReceiveSolid")) {
        return *value > 0.5f;
    }
    if (const std::string* value = FindStringParamNoCase(node->string_attrs, "DecalReceiveSolid")) {
        return StringIsTruthy(*value);
    }

    if (ToLower(node->model_node_type) == "decalreceivesolid") {
        return true;
    }

    return false;
}

enum class DrawBucket {
    Solid,
    AlphaTest,
    Decal,
    Water,
    Refraction,
    PostAlphaUnlit,
    SimpleLayer,
    Environment,
    EnvironmentAlpha
};

DrawBucket DetermineDrawBucket(const DrawCmd& dc) {
    const std::string shaderLower = ToLower(dc.shdr);
    const std::string nodeTypeLower = ToLower(dc.modelNodeType);
    const std::string nodeNameLower = ToLower(dc.nodeName);

    auto hasTag = [&](std::initializer_list<const char*> tags) {
        for (const char* tag : tags) {
            if (ContainsNoCase(shaderLower, tag) ||
                ContainsNoCase(nodeTypeLower, tag) ||
                ContainsNoCase(nodeNameLower, tag)) {
                return true;
            }
        }
        return false;
    };

    const bool simpleLayerBySemantics =
        HasTextureSemanticNoCase(dc.shaderParamsTexture, {"MaskMap"}) &&
        HasTextureSemanticNoCase(dc.shaderParamsTexture, {"DiffMap2", "DiffMap1", "BumpMap1", "SpecMap1"});
    const bool environmentBySemantics =
        dc.tex[9] != nullptr || HasTextureSemanticNoCase(dc.shaderParamsTexture, {"CubeMap0", "EnvironmentMap"});
    const bool decalReceiverByTag =
        ContainsNoCase(shaderLower, "decalreceive") ||
        ContainsNoCase(shaderLower, "decal_receive") ||
        ContainsNoCase(nodeTypeLower, "decalreceive") ||
        ContainsNoCase(nodeTypeLower, "decal_receive") ||
        ContainsNoCase(nodeNameLower, "decalreceive") ||
        ContainsNoCase(nodeNameLower, "decal_receive");

    if (!decalReceiverByTag &&
        (dc.isDecal ||
        ContainsNoCase(shaderLower, "decal") ||
        ContainsNoCase(nodeTypeLower, "decal"))) {
        return DrawBucket::Decal;
    }
    if (hasTag({"water"})) {
        return DrawBucket::Water;
    }
    if (hasTag({"refract", "refraction"})) {
        return DrawBucket::Refraction;
    }
    if (hasTag({"postalphaunlit", "post_alpha_unlit"})) {
        return DrawBucket::PostAlphaUnlit;
    }
    if (hasTag({"simplelayer", "simple_layer"}) || simpleLayerBySemantics) {
        return DrawBucket::SimpleLayer;
    }
    if (hasTag({"environmentalpha", "environment_alpha", "envalpha", "env_alpha"})) {
        return DrawBucket::EnvironmentAlpha;
    }
    if (hasTag({"environment", "envmap"}) || environmentBySemantics) {
        return DrawBucket::Environment;
    }
    if (dc.alphaTest || hasTag({"alphatest", "alpha_test", "cutout"})) {
        return DrawBucket::AlphaTest;
    }
    return DrawBucket::Solid;
}

std::string ResolveModelPath(const std::string& rawPathOrResource) {
    namespace fs = std::filesystem;
    if (rawPathOrResource.empty()) return {};

    std::string token = rawPathOrResource;
    if (token.starts_with("mdl:")) token = token.substr(4);
    std::replace(token.begin(), token.end(), '\\', '/');
    if (token.empty()) return {};

    fs::path candidate(token);
    if (!candidate.has_extension()) {
        candidate += ".n3";
    }

    auto isFile = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_regular_file(p, ec);
    };

    if (isFile(candidate)) {
        return candidate.string();
    }

    if (!candidate.is_absolute()) {
        fs::path rooted = fs::path(MODELS_ROOT) / candidate;
        if (isFile(rooted)) {
            return rooted.string();
        }

        const std::string normalized = candidate.generic_string();
        constexpr const char* kModelsPrefix = "models/";
        if (normalized.rfind(kModelsPrefix, 0) == 0) {
            fs::path trimmed = fs::path(MODELS_ROOT) / normalized.substr(std::strlen(kModelsPrefix));
            if (isFile(trimmed)) {
                return trimmed.string();
            }
        }

        return rooted.string();
    }

    return candidate.string();
}

void RefreshMegaOffsets(std::vector<DrawCmd>& draws) {
    for (auto& dc : draws) {
        if (!dc.mesh) continue;
        dc.megaVertexOffset = dc.mesh->megaVertexOffset;
        dc.megaIndexOffset = dc.mesh->megaIndexOffset;
    }
}

class ExternalTextureRef final : public NDEVC::Graphics::ITexture {
public:
    ExternalTextureRef(GLuint handle, NDEVC::Graphics::TextureType type)
        : handle_(handle), type_(type) {}

    uint32_t GetWidth() const override { return 1; }
    uint32_t GetHeight() const override { return 1; }
    NDEVC::Graphics::Format GetFormat() const override { return NDEVC::Graphics::Format::RGBA8_UNORM; }
    NDEVC::Graphics::TextureType GetType() const override { return type_; }
    void* GetNativeHandle() const override { return static_cast<void*>(const_cast<GLuint*>(&handle_)); }

private:
    GLuint handle_ = 0;
    NDEVC::Graphics::TextureType type_ = NDEVC::Graphics::TextureType::Texture2D;
};

double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
}

void SceneManager::Initialize(NDEVC::Graphics::IGraphicsDevice* device,
                              NDEVC::Graphics::IShaderManager* shaderMgr) {
    device_ = device;
    shaderMgr_ = shaderMgr;
    NC::LOGGING::Log("[SCENE] Initialize device=", (device_ ? 1 : 0), " shaderMgr=", (shaderMgr_ ? 1 : 0));
}

void SceneManager::AppendModel(const std::string& path, const glm::vec3& pos,
                               const glm::quat& rot, const glm::vec3& scale) {
    NC::LOGGING::Log("[SCENE] AppendModel path=", path,
                     " pos=(", pos.x, ",", pos.y, ",", pos.z, ")",
                     " rot=(", rot.x, ",", rot.y, ",", rot.z, ",", rot.w, ")",
                     " scale=(", scale.x, ",", scale.y, ",", scale.z, ")");
    appendN3WTransform(path, pos, rot, scale);
}

void SceneManager::LoadMap(const MapData* map) {
    NC::LOGGING::Log("[SCENE] LoadMap begin ptr=", (map ? 1 : 0));
    ResetStreamingState();
    instances.clear();
    particleNodes.clear();
    animatorInstances.clear();
    nodeMap.clear();
    instanceSpawnTimes.clear();
    instanceModelPathByOwner_.clear();
    instanceMeshResourceByOwner_.clear();
    loadedModelRefCountByPath_.clear();
    loadedMeshByModelPath_.clear();
    textureRefsByResourceId_.clear();
    sceneSolidDraws_.clear();
    sceneAlphaTestDraws_.clear();
    sceneDecalDraws_.clear();
    sceneEnvironmentDraws_.clear();
    sceneEnvironmentAlphaDraws_.clear();
    sceneSimpleLayerDraws_.clear();
    sceneRefractionDraws_.clear();
    scenePostAlphaUnlitDraws_.clear();
    sceneWaterDraws_.clear();
    sceneParticleDraws_.clear();
    instanceTransformCache_.clear();
    movedInstanceOwners_.clear();

    if (!map) {
        ownedCurrentMap_.reset();
        currentMap = nullptr;
        currentMapSourcePath_.clear();
        drawListsDirty_ = true;
        NC::LOGGING::Warning("[SCENE] LoadMap cleared current map (null input)");
        return;
    }
    ownedCurrentMap_ = std::make_unique<MapData>(*map);
    currentMap = ownedCurrentMap_.get();
    BuildStreamingIndex(currentMap);
    UpdateIncrementalStreaming(true);

    {
        size_t totalDraws = 0;
        size_t receivers = 0;
        size_t decalDraws = 0;
        size_t exactInt = 0;
        size_t exactFloat = 0;
        size_t exactString = 0;
        size_t modelTypeExact = 0;
        size_t modelTypeTagged = 0;
        size_t shaderTagged = 0;
        size_t nameTagged = 0;
        std::unordered_map<std::string, size_t> decalIntKeys;
        std::unordered_map<std::string, size_t> decalFloatKeys;
        std::unordered_map<std::string, size_t> decalStringKeys;

        auto scanDraw = [&](const DrawCmd& dc) {
            ++totalDraws;
            if (dc.receivesDecals) ++receivers;
            if (dc.isDecal) ++decalDraws;

            const Node* node = dc.sourceNode;
            if (!node) return;

            if (FindIntParamNoCase(node->shader_params_int, "DecalReceiveSolid") != nullptr) ++exactInt;
            if (FindFloatParamNoCase(node->shader_params_float, "DecalReceiveSolid") != nullptr) ++exactFloat;
            if (FindStringParamNoCase(node->string_attrs, "DecalReceiveSolid") != nullptr) ++exactString;
            if (ToLower(node->model_node_type) == "decalreceivesolid") ++modelTypeExact;

            if (ContainsNoCase(node->model_node_type, "decalreceive")) ++modelTypeTagged;
            if (ContainsNoCase(node->shader, "decalreceive")) ++shaderTagged;
            if (ContainsNoCase(node->node_name, "decalreceive")) ++nameTagged;

            for (const auto& [key, value] : node->shader_params_int) {
                (void)value;
                if (ContainsNoCase(key, "decal")) {
                    ++decalIntKeys[ToLower(key)];
                }
            }
            for (const auto& [key, value] : node->shader_params_float) {
                (void)value;
                if (ContainsNoCase(key, "decal")) {
                    ++decalFloatKeys[ToLower(key)];
                }
            }
            for (const auto& [key, value] : node->string_attrs) {
                (void)value;
                if (ContainsNoCase(key, "decal")) {
                    ++decalStringKeys[ToLower(key)];
                }
            }
        };

        for (const auto& dc : sceneSolidDraws_) scanDraw(dc);
        for (const auto& dc : sceneAlphaTestDraws_) scanDraw(dc);
        for (const auto& dc : sceneDecalDraws_) scanDraw(dc);
        for (const auto& dc : sceneEnvironmentDraws_) scanDraw(dc);
        for (const auto& dc : sceneEnvironmentAlphaDraws_) scanDraw(dc);
        for (const auto& dc : sceneSimpleLayerDraws_) scanDraw(dc);
        for (const auto& dc : sceneRefractionDraws_) scanDraw(dc);
        for (const auto& dc : scenePostAlphaUnlitDraws_) scanDraw(dc);
        for (const auto& dc : sceneWaterDraws_) scanDraw(dc);

        NC::LOGGING::Log("[SCENE][DECAL_RECEIVE] draws=", totalDraws,
                         " receivers=", receivers,
                         " decalDraws=", decalDraws,
                         " exactInt=", exactInt,
                         " exactFloat=", exactFloat,
                         " exactString=", exactString,
                         " modelTypeExact=", modelTypeExact,
                         " modelTypeTag=", modelTypeTagged,
                         " shaderTag=", shaderTagged,
                         " nameTag=", nameTagged,
                         " decalIntKeys=", decalIntKeys.size(),
                         " decalFloatKeys=", decalFloatKeys.size(),
                         " decalStringKeys=", decalStringKeys.size());

        if (exactInt == 0 && exactFloat == 0 && exactString == 0 && modelTypeExact == 0) {
            NC::LOGGING::Warning("[SCENE][DECAL_RECEIVE] DecalReceiveSolid key was not found in parsed node params.");
        }

        int printed = 0;
        for (const auto& [key, hits] : decalIntKeys) {
            if (printed >= 8) break;
            NC::LOGGING::Log("[SCENE][DECAL_RECEIVE] intKey=", key, " hits=", hits);
            ++printed;
        }
        printed = 0;
        for (const auto& [key, hits] : decalFloatKeys) {
            if (printed >= 8) break;
            NC::LOGGING::Log("[SCENE][DECAL_RECEIVE] floatKey=", key, " hits=", hits);
            ++printed;
        }
        printed = 0;
        for (const auto& [key, hits] : decalStringKeys) {
            if (printed >= 8) break;
            NC::LOGGING::Log("[SCENE][DECAL_RECEIVE] stringKey=", key, " hits=", hits);
            ++printed;
        }
    }

    drawListsDirty_ = true;
    NC::LOGGING::Log("[SCENE] LoadMap end instances=", instances.size(),
                     " solid=", sceneSolidDraws_.size(),
                     " alpha=", sceneAlphaTestDraws_.size(),
                     " decals=", sceneDecalDraws_.size(),
                     " env=", sceneEnvironmentDraws_.size(),
                     " envAlpha=", sceneEnvironmentAlphaDraws_.size(),
                     " simpleLayer=", sceneSimpleLayerDraws_.size(),
                     " refraction=", sceneRefractionDraws_.size(),
                     " postAlpha=", scenePostAlphaUnlitDraws_.size(),
                     " water=", sceneWaterDraws_.size(),
                     " particles=", sceneParticleDraws_.size());
}

void SceneManager::ReloadMap() {
    if (currentMapSourcePath_.empty()) {
        NC::LOGGING::Warning("[SCENE] ReloadMap requested with empty source path");
        return;
    }
    NC::LOGGING::Log("[SCENE] ReloadMap begin path=", currentMapSourcePath_);
    MapLoader loader;
    auto reloaded = loader.load_map(currentMapSourcePath_);
    if (!reloaded) {
        NC::LOGGING::Error("[SCENE] ReloadMap failed path=", currentMapSourcePath_);
        return;
    }
    LoadMap(reloaded.get());
    NC::LOGGING::Log("[SCENE] ReloadMap end path=", currentMapSourcePath_);
}

void SceneManager::Clear() {
    NC::LOGGING::Log("[SCENE] Clear begin");
    instances.clear();
    particleNodes.clear();
    animatorInstances.clear();
    nodeMap.clear();
    ResetStreamingState();
    ownedCurrentMap_.reset();
    currentMap = nullptr;
    currentMapSourcePath_.clear();
    instanceSpawnTimes.clear();
    instanceModelPathByOwner_.clear();
    instanceMeshResourceByOwner_.clear();
    loadedModelRefCountByPath_.clear();
    loadedMeshByModelPath_.clear();
    textureRefsByResourceId_.clear();
    sceneSolidDraws_.clear();
    sceneAlphaTestDraws_.clear();
    sceneDecalDraws_.clear();
    sceneEnvironmentDraws_.clear();
    sceneEnvironmentAlphaDraws_.clear();
    sceneSimpleLayerDraws_.clear();
    sceneRefractionDraws_.clear();
    scenePostAlphaUnlitDraws_.clear();
    sceneWaterDraws_.clear();
    sceneParticleDraws_.clear();
    instanceTransformCache_.clear();
    movedInstanceOwners_.clear();
    drawListsDirty_ = true;
    NC::LOGGING::Log("[SCENE] Clear end");
}

void SceneManager::Tick(double dt, const Camera& camera) {
    (void)dt;
    streamCameraPos_ = camera.getPosition();
    streamCameraValid_ = true;
    UpdateIncrementalStreaming(false);

    movedInstanceOwners_.clear();
    std::unordered_set<void*> liveOwners;
    liveOwners.reserve(instances.size());

    for (const auto& instance : instances) {
        if (!instance) continue;
        void* owner = static_cast<void*>(instance.get());
        liveOwners.insert(owner);

        const glm::mat4 currentTransform = instance->getTransform();
        auto cacheIt = instanceTransformCache_.find(owner);
        if (cacheIt == instanceTransformCache_.end()) {
            instanceTransformCache_.emplace(owner, currentTransform);
            movedInstanceOwners_.insert(owner);
            continue;
        }

        if (!MatricesNearlyEqual(cacheIt->second, currentTransform)) {
            cacheIt->second = currentTransform;
            movedInstanceOwners_.insert(owner);
        }
    }

    for (auto it = instanceTransformCache_.begin(); it != instanceTransformCache_.end();) {
        if (liveOwners.find(it->first) == liveOwners.end()) {
            it = instanceTransformCache_.erase(it);
        } else {
            ++it;
        }
    }

    if (!movedInstanceOwners_.empty()) {
        PropagateMovedInstanceTransformsToAllSceneDraws();
    }
    // animation tick will go here
}

void SceneManager::PrepareDrawLists(
    const Camera& camera,
    std::vector<DrawCmd>& solidDraws,
    std::vector<DrawCmd>& alphaTestDraws,
    std::vector<DrawCmd>& decalDraws,
    std::vector<DrawCmd>& particleDraws,
    std::vector<DrawCmd>& environmentDraws,
    std::vector<DrawCmd>& environmentAlphaDraws,
    std::vector<DrawCmd>& simpleLayerDraws,
    std::vector<DrawCmd>& refractionDraws,
    std::vector<DrawCmd>& postAlphaUnlitDraws,
    std::vector<DrawCmd>& waterDraws,
    std::vector<DrawCmd*>& animatedDraws) {
    (void)camera;

    if (!drawListsDirty_) {
        if (!movedInstanceOwners_.empty()) {
            PropagateMovedInstanceTransforms(solidDraws);
            PropagateMovedInstanceTransforms(alphaTestDraws);
            PropagateMovedInstanceTransforms(decalDraws);
            PropagateMovedInstanceTransforms(particleDraws);
            PropagateMovedInstanceTransforms(environmentDraws);
            PropagateMovedInstanceTransforms(environmentAlphaDraws);
            PropagateMovedInstanceTransforms(simpleLayerDraws);
            PropagateMovedInstanceTransforms(refractionDraws);
            PropagateMovedInstanceTransforms(postAlphaUnlitDraws);
            PropagateMovedInstanceTransforms(waterDraws);
        }
        return;
    }
    drawListsDirty_ = false;

    solidDraws = sceneSolidDraws_;
    alphaTestDraws = sceneAlphaTestDraws_;
    decalDraws = sceneDecalDraws_;
    particleDraws = sceneParticleDraws_;
    environmentDraws = sceneEnvironmentDraws_;
    environmentAlphaDraws = sceneEnvironmentAlphaDraws_;
    simpleLayerDraws = sceneSimpleLayerDraws_;
    refractionDraws = sceneRefractionDraws_;
    postAlphaUnlitDraws = scenePostAlphaUnlitDraws_;
    waterDraws = sceneWaterDraws_;

    RefreshMegaOffsets(solidDraws);
    RefreshMegaOffsets(alphaTestDraws);
    RefreshMegaOffsets(decalDraws);
    RefreshMegaOffsets(environmentDraws);
    RefreshMegaOffsets(environmentAlphaDraws);
    RefreshMegaOffsets(simpleLayerDraws);
    RefreshMegaOffsets(refractionDraws);
    RefreshMegaOffsets(postAlphaUnlitDraws);
    RefreshMegaOffsets(waterDraws);
    RefreshMegaOffsets(particleDraws);

    if (!movedInstanceOwners_.empty()) {
        PropagateMovedInstanceTransforms(solidDraws);
        PropagateMovedInstanceTransforms(alphaTestDraws);
        PropagateMovedInstanceTransforms(decalDraws);
        PropagateMovedInstanceTransforms(particleDraws);
        PropagateMovedInstanceTransforms(environmentDraws);
        PropagateMovedInstanceTransforms(environmentAlphaDraws);
        PropagateMovedInstanceTransforms(simpleLayerDraws);
        PropagateMovedInstanceTransforms(refractionDraws);
        PropagateMovedInstanceTransforms(postAlphaUnlitDraws);
        PropagateMovedInstanceTransforms(waterDraws);
    }

    animatedDraws.clear();
    auto collectAnimated = [&animatedDraws](std::vector<DrawCmd>& draws) {
        for (auto& dc : draws) {
            if (dc.hasPotentialTransformAnimation || dc.hasShaderVarAnimations) {
                animatedDraws.push_back(&dc);
            }
        }
    };
    collectAnimated(solidDraws);
    collectAnimated(alphaTestDraws);
    collectAnimated(decalDraws);
    collectAnimated(environmentDraws);
    collectAnimated(environmentAlphaDraws);
    collectAnimated(simpleLayerDraws);
    collectAnimated(refractionDraws);
    collectAnimated(postAlphaUnlitDraws);
    collectAnimated(waterDraws);
    collectAnimated(particleDraws);
    NC::LOGGING::Log("[SCENE] PrepareDrawLists solid=", solidDraws.size(),
                     " alpha=", alphaTestDraws.size(),
                     " decals=", decalDraws.size(),
                     " particles=", particleDraws.size(),
                     " env=", environmentDraws.size(),
                     " envAlpha=", environmentAlphaDraws.size(),
                     " simpleLayer=", simpleLayerDraws.size(),
                     " refraction=", refractionDraws.size(),
                     " postAlpha=", postAlphaUnlitDraws.size(),
                     " water=", waterDraws.size(),
                     " animated=", animatedDraws.size());
}

void SceneManager::PropagateMovedInstanceTransforms(std::vector<DrawCmd>& draws) {
    if (draws.empty() || movedInstanceOwners_.empty()) {
        return;
    }

    std::unordered_map<const Node*, glm::mat4> localToModelCache;
    std::function<glm::mat4(const Node*)> modelSpaceTransform = [&](const Node* node) -> glm::mat4 {
        if (!node) return glm::mat4(1.0f);
        auto it = localToModelCache.find(node);
        if (it != localToModelCache.end()) {
            return it->second;
        }
        const glm::mat4 local = BuildLocalTransform(node);
        const glm::mat4 parent = node->node_parent ? modelSpaceTransform(node->node_parent) : glm::mat4(1.0f);
        const glm::mat4 result = parent * local;
        localToModelCache.emplace(node, result);
        return result;
    };

    for (DrawCmd& dc : draws) {
        if (!dc.instance) continue;
        if (movedInstanceOwners_.find(dc.instance) == movedInstanceOwners_.end()) continue;

        const auto transformIt = instanceTransformCache_.find(dc.instance);
        if (transformIt == instanceTransformCache_.end()) continue;
        const glm::mat4& instanceTransform = transformIt->second;
        if (MatricesNearlyEqual(dc.rootMatrix, instanceTransform)) continue;

        glm::mat4 localTransform(1.0f);
        bool hasLocalTransform = false;
        if (dc.sourceNode) {
            localTransform = modelSpaceTransform(dc.sourceNode);
            hasLocalTransform = true;
        } else {
            glm::mat4 inverseRoot(1.0f);
            if (TryComputeInverse(dc.rootMatrix, inverseRoot)) {
                localTransform = inverseRoot * dc.worldMatrix;
                hasLocalTransform = MatrixIsFinite(localTransform);
            }
        }

        dc.rootMatrix = instanceTransform;
        if (hasLocalTransform) {
            dc.worldMatrix = instanceTransform * localTransform;
        }
        dc.cullBoundsValid = false;
        dc.cullTransformHash = 0ull;
        dc.frustumCulled = false;
        if (dc.isStatic) {
            // Avoid stale static-batch matrices after runtime transform changes.
            dc.isStatic = false;
        }
    }
}

void SceneManager::PropagateMovedInstanceTransformsToAllSceneDraws() {
    PropagateMovedInstanceTransforms(sceneSolidDraws_);
    PropagateMovedInstanceTransforms(sceneAlphaTestDraws_);
    PropagateMovedInstanceTransforms(sceneDecalDraws_);
    PropagateMovedInstanceTransforms(sceneParticleDraws_);
    PropagateMovedInstanceTransforms(sceneEnvironmentDraws_);
    PropagateMovedInstanceTransforms(sceneEnvironmentAlphaDraws_);
    PropagateMovedInstanceTransforms(sceneSimpleLayerDraws_);
    PropagateMovedInstanceTransforms(sceneRefractionDraws_);
    PropagateMovedInstanceTransforms(scenePostAlphaUnlitDraws_);
    PropagateMovedInstanceTransforms(sceneWaterDraws_);
}

ModelInstance* SceneManager::appendN3WTransform(const std::string& path,
    const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
    const std::string modelPath = ResolveModelPath(path);
    if (modelPath.empty()) {
        NC::LOGGING::Error("[SCENE] appendN3WTransform empty model path input=", path);
        return nullptr;
    }

    Reporter rep;
    rep.currentFile = modelPath;
    Options opt;
    opt.n3filepath = modelPath;
    auto model = ModelServer::instance().loadModel(modelPath, rep, opt);
    if (!model) {
        NC::LOGGING::Error("[SCENE] appendN3WTransform failed to load model path=", modelPath);
        return nullptr;
    }

    auto instance = ModelServer::instance().createInstance(model);
    if (!instance) {
        NC::LOGGING::Error("[SCENE] appendN3WTransform failed to create instance path=", modelPath);
        return nullptr;
    }

    instance->setTransform(pos, rot, scale);
    ModelInstance* rawInstance = instance.get();
    void* owner = static_cast<void*>(rawInstance);
    instances.push_back(instance);
    instanceSpawnTimes[owner] = 0.0;
    instanceModelPathByOwner_[owner] = modelPath;
    ++loadedModelRefCountByPath_[modelPath];

    std::vector<DrawCmd> newDraws;
    BuildDrawsWithTransform(*model, rawInstance->getTransform(), owner, newDraws);

    auto classifyDraw = [this](DrawCmd&& dc) {
        switch (DetermineDrawBucket(dc)) {
        case DrawBucket::Decal:
            sceneDecalDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Water:
            sceneWaterDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Refraction:
            sceneRefractionDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::PostAlphaUnlit:
            scenePostAlphaUnlitDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::SimpleLayer:
            sceneSimpleLayerDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::EnvironmentAlpha:
            sceneEnvironmentAlphaDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Environment:
            sceneEnvironmentDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::AlphaTest:
            sceneAlphaTestDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Solid:
        default:
            sceneSolidDraws_.push_back(std::move(dc));
            break;
        }
    };

        for (auto& dc : newDraws) {
            if (dc.sourceNode && !dc.sourceNode->mesh_ressource_id.empty()) {
                instanceMeshResourceByOwner_[owner] = dc.sourceNode->mesh_ressource_id;
                loadedMeshByModelPath_[modelPath] = dc.sourceNode->mesh_ressource_id;
            }
            classifyDraw(std::move(dc));
        }
    drawListsDirty_ = true;
    NC::LOGGING::Log("[SCENE] appendN3WTransform success path=", modelPath,
                     " draws=", newDraws.size(),
                     " totalInstances=", instances.size());

    return rawInstance;
}

NDEVC::Graphics::ITexture* SceneManager::GetOrCreateTextureRef(
    const std::string& textureResourceId,
    NDEVC::Graphics::TextureType type) {
    if (textureResourceId.empty()) {
        return nullptr;
    }

    const std::string cacheKey = std::to_string(static_cast<int>(type)) + ":" + textureResourceId;
    auto found = textureRefsByResourceId_.find(cacheKey);
    if (found != textureRefsByResourceId_.end()) {
        return found->second.get();
    }

    const GLuint handle = TextureServer::instance().loadTexture(textureResourceId);
    if (handle == 0) {
        NC::LOGGING::Error("[SCENE] TextureRef load failed id=", textureResourceId, " type=", static_cast<int>(type));
        return nullptr;
    }

    auto wrapped = std::make_shared<ExternalTextureRef>(handle, type);
    NDEVC::Graphics::ITexture* raw = wrapped.get();
    textureRefsByResourceId_.emplace(cacheKey, std::move(wrapped));
    return raw;
}

void SceneManager::BuildDrawsWithTransform(const Model& model,
    const glm::mat4& instanceTransform, void* instance,
    std::vector<DrawCmd>& out) {
    std::unordered_map<const Node*, glm::mat4> localToModelCache;
    std::function<glm::mat4(const Node*)> modelSpaceTransform = [&](const Node* node) -> glm::mat4 {
        if (!node) return glm::mat4(1.0f);
        auto it = localToModelCache.find(node);
        if (it != localToModelCache.end()) {
            return it->second;
        }
        const glm::mat4 local = BuildLocalTransform(node);
        const glm::mat4 parent = node->node_parent ? modelSpaceTransform(node->node_parent) : glm::mat4(1.0f);
        const glm::mat4 result = parent * local;
        localToModelCache.emplace(node, result);
        return result;
    };

    for (const Node* node : model.getNodes()) {
        if (!node || node->mesh_ressource_id.empty()) continue;

        Mesh* mesh = MeshServer::instance().loadMesh(node->mesh_ressource_id);
        if (!mesh) continue;

        DrawCmd dc;
        dc.mesh = mesh;
        dc.shdr = node->shader;
        dc.nodeName = node->node_name;
        dc.modelNodeType = node->model_node_type;
        dc.rootMatrix = instanceTransform;
        dc.worldMatrix = instanceTransform * modelSpaceTransform(node);
        dc.localBoxMin = node->local_box_min;
        dc.localBoxMax = node->local_box_max;
        dc.position = node->position;
        dc.rotation = node->rotation;
        dc.scale = node->scale;
        dc.group = node->primitive_group_idx;
        dc.instance = instance;
        dc.sourceNode = node;
        dc.receivesDecals = ResolveDecalReceiveSolid(node);

        dc.shaderParamsTexture = node->shader_params_texture;
        auto assignTextureSemantic = [&](int slot,
                                         std::initializer_list<const char*> semantics,
                                         NDEVC::Graphics::TextureType type) {
            for (const char* semantic : semantics) {
                const std::string* texResId = FindTextureParamNoCase(node->shader_params_texture, semantic);
                if (!texResId || texResId->empty()) {
                    continue;
                }
                dc.tex[slot] = GetOrCreateTextureRef(*texResId, type);
                if (dc.tex[slot]) {
                    return;
                }
            }
        };
        assignTextureSemantic(0, {"DiffMap0", "DiffMap", "DiffuseMap0", "DiffuseMap", "ColorMap", "AlbedoMap"},
                              NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(1, {"SpecMap0", "SpecMap", "SpecularMap0", "SpecularMap"},
                              NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(2, {"BumpMap0", "BumpMap", "NormalMap0", "NormalMap"},
                              NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(3, {"EmsvMap0", "EmsvMap", "EmissiveMap0", "EmissiveMap"},
                              NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(4, {"DiffMap2", "DiffMap1"}, NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(5, {"SpecMap1"}, NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(6, {"BumpMap1"}, NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(7, {"MaskMap"}, NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(8, {"DiffMap1", "AlphaMap", "OpacityMap", "AOMap"},
                              NDEVC::Graphics::TextureType::Texture2D);
        assignTextureSemantic(9, {"CubeMap0", "EnvironmentMap"},
                              NDEVC::Graphics::TextureType::TextureCube);

        if (!dc.tex[0]) {
            assignTextureSemantic(0, {"DistortMap", "DiffMap1", "AlphaMap", "OpacityMap"},
                                  NDEVC::Graphics::TextureType::Texture2D);
        }

        for (const auto& [k, v] : node->shader_params_int) {
            dc.shaderParamsInt[k] = static_cast<int>(v);
        }
        for (const auto& [k, v] : node->shader_params_float) {
            dc.shaderParamsFloat[k] = v;
        }
        dc.shaderParamsVec4 = node->shader_params_vec4;

        dc.cachedHasSpecMap = dc.tex[1] != nullptr || HasNonEmptyTexture(node, "SpecMap0");
        dc.cachedIsAdditive = GetIntParam(node->shader_params_int, "isAdditive",
                            GetIntParam(node->shader_params_int, "additive", 0)) != 0;
        dc.cachedHasCustomDiffMap = dc.tex[0] != nullptr;
        dc.cachedHasVelocity = false;
        if (const glm::vec4* velocity = FindVec4ParamNoCase(node->shader_params_vec4, "Velocity")) {
            dc.cachedVelocity = glm::vec2(velocity->x, velocity->y);
            dc.cachedHasVelocity = glm::length(dc.cachedVelocity) > 0.00001f;
        }
        dc.cachedWaterCullMode = GetIntParam(node->shader_params_int, "cullMode", 2);
        dc.cachedIntensity0 = GetFloatParam(node->shader_params_float, "Intensity0", 0.25f);
        dc.cachedMatEmissiveIntensity = GetFloatParam(node->shader_params_float, "MatEmissiveIntensity", 1.0f);
        dc.cachedMatSpecularIntensity = GetFloatParam(node->shader_params_float, "MatSpecularIntensity", 0.0f);
        dc.cachedMatSpecularPower = GetFloatParam(node->shader_params_float, "MatSpecularPower", 32.0f);
        dc.cachedBumpScale = GetFloatParam(node->shader_params_float, "BumpScale", 1.0f);
        dc.cachedAlphaBlendFactor = GetFloatParam(node->shader_params_float, "alphaBlendFactor", 1.0f);
        dc.cachedTwoSided = GetIntParam(node->shader_params_int, "twoSided", GetIntParam(node->shader_params_int, "TwoSided", 0));
        dc.cachedIsFlatNormal = GetIntParam(node->shader_params_int, "isFlatNormal", 0);
        dc.cullMode = GetIntParam(node->shader_params_int, "cullMode", 0);

        const float floatAlphaCutoff = GetFloatParam(node->shader_params_float, "alphaCutoff", -1.0f);
        float alphaCutoff;
        if (floatAlphaCutoff >= 0.0f) {
            alphaCutoff = floatAlphaCutoff;
        } else {
            const int intAlphaRef = GetIntParam(node->shader_params_int, "AlphaRef", -1);
            if (intAlphaRef >= 0) {
                alphaCutoff = static_cast<float>(intAlphaRef) / 255.0f;
            } else {
                alphaCutoff = GetFloatParam(node->shader_params_float, "AlphaRef", 0.5f);
            }
        }
        dc.alphaCutoff = alphaCutoff;
        const int alphaTestInt = GetIntParam(node->shader_params_int, "alphaTest", 0);
        const float alphaTestFloat = GetFloatParam(node->shader_params_float, "alphaTest", 0.0f);
        const bool alphaCutByName =
            ContainsNoCase(dc.shdr, "alphatest") || ContainsNoCase(dc.shdr, "alpha_test") ||
            ContainsNoCase(dc.modelNodeType, "alphatest") || ContainsNoCase(dc.modelNodeType, "alpha_test") ||
            ContainsNoCase(dc.nodeName, "alphatest") || ContainsNoCase(dc.nodeName, "alpha_test");
        const bool isAlphaBlendPass =
            ContainsNoCase(dc.shdr, "postalphaunlit") ||
            ContainsNoCase(dc.shdr, "environmentalpha") ||
            ContainsNoCase(dc.modelNodeType, "postalphaunlit") ||
            ContainsNoCase(dc.modelNodeType, "environmentalpha");
        dc.alphaTest = alphaTestInt != 0 || alphaTestFloat > 0.5f ||
                       (!isAlphaBlendPass && alphaCutByName);

        const bool isDecalReceiver =
            ContainsNoCase(dc.shdr, "decalreceive") ||
            ContainsNoCase(dc.shdr, "decal_receive") ||
            ContainsNoCase(dc.modelNodeType, "decalreceive") ||
            ContainsNoCase(dc.modelNodeType, "decal_receive") ||
            ContainsNoCase(dc.nodeName, "decalreceive") ||
            ContainsNoCase(dc.nodeName, "decal_receive");
        dc.isDecal = !isDecalReceiver &&
                     (ContainsNoCase(dc.shdr, "decal") ||
                      ContainsNoCase(dc.modelNodeType, "decal"));
        if (dc.isDecal) {
            if (!dc.tex[0]) {
                assignTextureSemantic(0, {"DiffMap1", "DecalMap"}, NDEVC::Graphics::TextureType::Texture2D);
            }
            if (!dc.tex[3]) {
                assignTextureSemantic(3, {"MaskMap", "DecalMask", "OpacityMap", "AlphaMap"},
                                      NDEVC::Graphics::TextureType::Texture2D);
            }
        }
        dc.hasPotentialTransformAnimation = !node->animSections.empty();
        dc.hasShaderVarAnimations = std::any_of(
            node->animSections.begin(), node->animSections.end(),
            [](const AnimSection& section) { return !section.shaderVarSemantic.empty(); });
        const NodeMobilityHint mobilityHint = ResolveNodeMobilityHint(node);
        if (mobilityHint == NodeMobilityHint::Dynamic) {
            dc.isStatic = false;
        } else if (dc.hasPotentialTransformAnimation || dc.hasShaderVarAnimations) {
            dc.isStatic = false;
        } else if (mobilityHint == NodeMobilityHint::Static) {
            dc.isStatic = true;
        } else {
            dc.isStatic = true;
        }

        out.push_back(std::move(dc));
    }
}

void SceneManager::LoadMapInstances(const MapData* map) {
    if (!map) {
        NC::LOGGING::Warning("[SCENE] LoadMapInstances skipped (null map)");
        return;
    }
    size_t skippedInvalidTemplate = 0;
    size_t skippedInvalidResource = 0;
    size_t skippedEmptyPath = 0;
    size_t skippedModelLoad = 0;
    size_t skippedInstanceCreate = 0;
    size_t loadedInstances = 0;
    NC::LOGGING::Log("[SCENE] LoadMapInstances begin mapInstances=", map->instances.size());

    for (const auto& inst : map->instances) {
        if (inst.templ_index < 0 || static_cast<size_t>(inst.templ_index) >= map->templates.size()) {
            ++skippedInvalidTemplate;
            continue;
        }

        const Template& tmpl = map->templates[inst.templ_index];
        if (tmpl.gfx_res_id >= map->string_table.size()) {
            ++skippedInvalidResource;
            continue;
        }

        const std::string& mapModelId = map->string_table[tmpl.gfx_res_id];
        const std::string modelPath = ResolveModelPath(mapModelId);
        if (modelPath.empty()) {
            ++skippedEmptyPath;
            continue;
        }

        Reporter rep;
        rep.currentFile = modelPath;
        Options opt;
        opt.n3filepath = modelPath;
        auto model = ModelServer::instance().loadModel(modelPath, rep, opt);
        if (!model) {
            ++skippedModelLoad;
            continue;
        }

        auto modelInstance = ModelServer::instance().createInstance(model);
        if (!modelInstance) {
            ++skippedInstanceCreate;
            continue;
        }

        const glm::vec3 pos(inst.pos.x, inst.pos.y, inst.pos.z);
        const glm::quat rot(inst.rot.w, inst.rot.x, inst.rot.y, inst.rot.z);
        const glm::vec3 scale = inst.use_scaling
            ? glm::vec3(inst.scale.x, inst.scale.y, inst.scale.z)
            : glm::vec3(1.0f);
        modelInstance->setTransform(pos, rot, scale);

        void* owner = static_cast<void*>(modelInstance.get());
        instances.push_back(modelInstance);
        instanceSpawnTimes[owner] = 0.0;
        instanceModelPathByOwner_[owner] = modelPath;
        ++loadedModelRefCountByPath_[modelPath];

        std::vector<DrawCmd> draws;
        BuildDrawsWithTransform(*model, modelInstance->getTransform(), owner, draws);

        auto classifyDraw = [this](DrawCmd&& dc) {
            switch (DetermineDrawBucket(dc)) {
            case DrawBucket::Decal:
                sceneDecalDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::Water:
                sceneWaterDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::Refraction:
                sceneRefractionDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::PostAlphaUnlit:
                scenePostAlphaUnlitDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::SimpleLayer:
                sceneSimpleLayerDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::EnvironmentAlpha:
                sceneEnvironmentAlphaDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::Environment:
                sceneEnvironmentDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::AlphaTest:
                sceneAlphaTestDraws_.push_back(std::move(dc));
                break;
            case DrawBucket::Solid:
            default:
                sceneSolidDraws_.push_back(std::move(dc));
                break;
            }
        };

        for (auto& dc : draws) {
            if (dc.sourceNode && !dc.sourceNode->mesh_ressource_id.empty()) {
                instanceMeshResourceByOwner_[owner] = dc.sourceNode->mesh_ressource_id;
                loadedMeshByModelPath_[modelPath] = dc.sourceNode->mesh_ressource_id;
            }
            classifyDraw(std::move(dc));
        }
        ++loadedInstances;
        if ((loadedInstances % 256) == 0) {
            NC::LOGGING::Log("[SCENE] LoadMapInstances progress loaded=", loadedInstances);
        }
    }
    NC::LOGGING::Log("[SCENE] LoadMapInstances end loaded=", loadedInstances,
                     " skippedInvalidTemplate=", skippedInvalidTemplate,
                     " skippedInvalidResource=", skippedInvalidResource,
                     " skippedEmptyPath=", skippedEmptyPath,
                     " skippedModelLoad=", skippedModelLoad,
                     " skippedInstanceCreate=", skippedInstanceCreate,
                     " totalInstances=", instances.size());
}

void SceneManager::ResetStreamingState() {
    streamState_ = StreamWindowState{};
    streamCameraValid_ = false;
}

void SceneManager::BuildStreamingIndex(const MapData* map) {
    ResetStreamingState();
    if (!map) {
        return;
    }

    streamState_.map = map;
    streamState_.gridW = map->info.map_size_x > 0 ? map->info.map_size_x : 32;
    streamState_.gridH = map->info.map_size_z > 0 ? map->info.map_size_z : 32;
    streamState_.cellSizeX = map->info.grid_size.x > 0.0f ? map->info.grid_size.x : 32.0f;
    streamState_.cellSizeZ = map->info.grid_size.z > 0.0f ? map->info.grid_size.z : 32.0f;
    streamState_.originXZ = glm::vec2(map->info.center.x - map->info.extents.x,
                                      map->info.center.z - map->info.extents.z);
    if (streamState_.gridW <= 0 || streamState_.gridH <= 0) {
        streamState_.gridW = 32;
        streamState_.gridH = 32;
    }
    if (streamState_.cellSizeX <= 0.0f) streamState_.cellSizeX = 32.0f;
    if (streamState_.cellSizeZ <= 0.0f) streamState_.cellSizeZ = 32.0f;

    streamState_.cellToInstances.clear();
    streamState_.cellToInstances.resize(static_cast<size_t>(streamState_.gridW) * streamState_.gridH);
    for (size_t mapIndex = 0; mapIndex < map->instances.size(); ++mapIndex) {
        const Instance& inst = map->instances[mapIndex];
        int cx = static_cast<int>(std::floor((inst.pos.x - streamState_.originXZ.x) / streamState_.cellSizeX));
        int cz = static_cast<int>(std::floor((inst.pos.z - streamState_.originXZ.y) / streamState_.cellSizeZ));
        cx = std::clamp(cx, 0, streamState_.gridW - 1);
        cz = std::clamp(cz, 0, streamState_.gridH - 1);
        const size_t cellIndex = static_cast<size_t>(cz * streamState_.gridW + cx);
        streamState_.cellToInstances[cellIndex].push_back(mapIndex);
    }
    streamState_.lastTickTime = 0.0;
    streamState_.initialized = true;
    NC::LOGGING::Log("[SCENE][STREAM] BuildStreamingIndex cells=", streamState_.cellToInstances.size(),
                     " mapInstances=", map->instances.size(),
                     " grid=", streamState_.gridW, "x", streamState_.gridH);
}

int SceneManager::ComputeStreamingCellIndex(const glm::vec3& worldPos) const {
    if (!streamState_.initialized || streamState_.gridW <= 0 || streamState_.gridH <= 0) {
        return -1;
    }
    const float fx = (worldPos.x - streamState_.originXZ.x) / streamState_.cellSizeX;
    const float fz = (worldPos.z - streamState_.originXZ.y) / streamState_.cellSizeZ;
    if (!std::isfinite(fx) || !std::isfinite(fz)) {
        return -1;
    }
    if (fx < 0.0f || fz < 0.0f ||
        fx >= static_cast<float>(streamState_.gridW) ||
        fz >= static_cast<float>(streamState_.gridH)) {
        return -1;
    }
    const int cx = static_cast<int>(std::floor(fx));
    const int cz = static_cast<int>(std::floor(fz));
    return cz * streamState_.gridW + cx;
}

ModelInstance* SceneManager::LoadMapInstanceByIndex(const MapData* map, size_t mapIndex) {
    if (!map || mapIndex >= map->instances.size()) {
        return nullptr;
    }
    auto loadedIt = streamState_.loadedOwnersByMapIndex.find(mapIndex);
    if (loadedIt != streamState_.loadedOwnersByMapIndex.end()) {
        void* owner = loadedIt->second;
        for (const auto& instance : instances) {
            if (instance && static_cast<void*>(instance.get()) == owner) {
                return instance.get();
            }
        }
        streamState_.loadedOwnersByMapIndex.erase(loadedIt);
    }

    const Instance& inst = map->instances[mapIndex];
    if (inst.templ_index < 0 || static_cast<size_t>(inst.templ_index) >= map->templates.size()) {
        return nullptr;
    }
    const Template& tmpl = map->templates[inst.templ_index];
    if (tmpl.gfx_res_id >= map->string_table.size()) {
        return nullptr;
    }

    const std::string& mapModelId = map->string_table[tmpl.gfx_res_id];
    const std::string modelPath = ResolveModelPath(mapModelId);
    if (modelPath.empty()) {
        return nullptr;
    }

    Reporter rep;
    rep.currentFile = modelPath;
    Options opt;
    opt.n3filepath = modelPath;
    auto model = ModelServer::instance().loadModel(modelPath, rep, opt);
    if (!model) {
        return nullptr;
    }

    auto modelInstance = ModelServer::instance().createInstance(model);
    if (!modelInstance) {
        return nullptr;
    }

    const glm::vec3 pos(inst.pos.x, inst.pos.y, inst.pos.z);
    const glm::quat rot(inst.rot.w, inst.rot.x, inst.rot.y, inst.rot.z);
    const glm::vec3 scale = inst.use_scaling
        ? glm::vec3(inst.scale.x, inst.scale.y, inst.scale.z)
        : glm::vec3(1.0f);
    modelInstance->setTransform(pos, rot, scale);

    ModelInstance* rawInstance = modelInstance.get();
    void* owner = static_cast<void*>(rawInstance);
    instances.push_back(modelInstance);
    instanceSpawnTimes[owner] = 0.0;
    instanceModelPathByOwner_[owner] = modelPath;
    ++loadedModelRefCountByPath_[modelPath];

    std::vector<DrawCmd> draws;
    BuildDrawsWithTransform(*model, modelInstance->getTransform(), owner, draws);

    auto classifyDraw = [this](DrawCmd&& dc) {
        switch (DetermineDrawBucket(dc)) {
        case DrawBucket::Decal:
            sceneDecalDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Water:
            sceneWaterDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Refraction:
            sceneRefractionDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::PostAlphaUnlit:
            scenePostAlphaUnlitDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::SimpleLayer:
            sceneSimpleLayerDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::EnvironmentAlpha:
            sceneEnvironmentAlphaDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Environment:
            sceneEnvironmentDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::AlphaTest:
            sceneAlphaTestDraws_.push_back(std::move(dc));
            break;
        case DrawBucket::Solid:
        default:
            sceneSolidDraws_.push_back(std::move(dc));
            break;
        }
    };

    std::string meshResourceId;
    for (auto& dc : draws) {
        if (dc.sourceNode && !dc.sourceNode->mesh_ressource_id.empty()) {
            instanceMeshResourceByOwner_[owner] = dc.sourceNode->mesh_ressource_id;
            loadedMeshByModelPath_[modelPath] = dc.sourceNode->mesh_ressource_id;
            if (meshResourceId.empty()) {
                meshResourceId = dc.sourceNode->mesh_ressource_id;
            }
        }
        classifyDraw(std::move(dc));
    }

    streamState_.loadedOwnersByMapIndex[mapIndex] = owner;
    drawListsDirty_ = true;
    NotifyWebModelLoaded(modelPath, meshResourceId);
    return rawInstance;
}

void SceneManager::UnloadInstanceByOwner(void* owner) {
    if (!owner) {
        return;
    }

    for (auto it = streamState_.loadedOwnersByMapIndex.begin();
         it != streamState_.loadedOwnersByMapIndex.end();) {
        if (it->second == owner) {
            it = streamState_.loadedOwnersByMapIndex.erase(it);
        } else {
            ++it;
        }
    }

    auto eraseDrawsByOwner = [owner](std::vector<DrawCmd>& draws) {
        const size_t before = draws.size();
        draws.erase(std::remove_if(draws.begin(), draws.end(),
            [owner](const DrawCmd& dc) { return dc.instance == owner; }),
            draws.end());
        return draws.size() != before;
    };

    bool removed = false;
    removed |= eraseDrawsByOwner(sceneSolidDraws_);
    removed |= eraseDrawsByOwner(sceneAlphaTestDraws_);
    removed |= eraseDrawsByOwner(sceneDecalDraws_);
    removed |= eraseDrawsByOwner(sceneEnvironmentDraws_);
    removed |= eraseDrawsByOwner(sceneEnvironmentAlphaDraws_);
    removed |= eraseDrawsByOwner(sceneSimpleLayerDraws_);
    removed |= eraseDrawsByOwner(sceneRefractionDraws_);
    removed |= eraseDrawsByOwner(scenePostAlphaUnlitDraws_);
    removed |= eraseDrawsByOwner(sceneWaterDraws_);
    removed |= eraseDrawsByOwner(sceneParticleDraws_);

    const size_t particleBefore = particleNodes.size();
    particleNodes.erase(std::remove_if(particleNodes.begin(), particleNodes.end(),
        [owner](const ParticleAttach& entry) { return entry.instance == owner; }),
        particleNodes.end());
    removed |= particleNodes.size() != particleBefore;

    const size_t animatorBefore = animatorInstances.size();
    animatorInstances.erase(std::remove_if(animatorInstances.begin(), animatorInstances.end(),
        [owner](const std::unique_ptr<AnimatorNodeInstance>& anim) {
            return anim && anim->GetOwner() == owner;
        }), animatorInstances.end());
    removed |= animatorInstances.size() != animatorBefore;

    const size_t instancesBefore = instances.size();
    instances.erase(std::remove_if(instances.begin(), instances.end(),
        [owner](const std::shared_ptr<ModelInstance>& instance) {
            return instance && static_cast<void*>(instance.get()) == owner;
        }), instances.end());
    removed |= instances.size() != instancesBefore;

    std::string modelPath;
    auto modelPathIt = instanceModelPathByOwner_.find(owner);
    if (modelPathIt != instanceModelPathByOwner_.end()) {
        modelPath = modelPathIt->second;
        instanceModelPathByOwner_.erase(modelPathIt);
    }

    instanceSpawnTimes.erase(owner);
    instanceMeshResourceByOwner_.erase(owner);
    instanceTransformCache_.erase(owner);
    movedInstanceOwners_.erase(owner);

    if (!modelPath.empty()) {
        auto refIt = loadedModelRefCountByPath_.find(modelPath);
        if (refIt != loadedModelRefCountByPath_.end()) {
            if (refIt->second > 1) {
                --refIt->second;
            } else {
                loadedModelRefCountByPath_.erase(refIt);
                std::string meshResourceId;
                auto meshIt = loadedMeshByModelPath_.find(modelPath);
                if (meshIt != loadedMeshByModelPath_.end()) {
                    meshResourceId = meshIt->second;
                    loadedMeshByModelPath_.erase(meshIt);
                }
                NotifyWebModelUnloaded(modelPath, meshResourceId);
            }
        }
    }

    if (removed) {
        drawListsDirty_ = true;
    }
}

void SceneManager::RebuildNodeMapFromInstances() {
    nodeMap.clear();
    for (const auto& instance : instances) {
        if (!instance) continue;
        const auto model = instance->getModel();
        if (!model) continue;
        for (Node* node : model->getNodes()) {
            if (!node || node->node_name.empty()) continue;
            nodeMap[node->node_name] = node;
        }
    }
}

void SceneManager::ApplySceneRebuildAfterStreamingChanges(bool meshLayoutChanged) {
    if (meshLayoutChanged) {
        MeshServer::instance().buildMegaBuffer();
    }
    RebuildNodeMapFromInstances();
    drawListsDirty_ = true;
}

void SceneManager::UpdateIncrementalStreaming(bool forceFullSync) {
    if (!currentMap) {
        ResetStreamingState();
        return;
    }

    if (!streamState_.initialized || streamState_.map != currentMap) {
        BuildStreamingIndex(currentMap);
    }
    if (!streamState_.initialized || streamState_.map == nullptr) {
        return;
    }

    const double nowSec = NowSeconds();
    if (!forceFullSync &&
        streamState_.lastTickTime > 0.0 &&
        (nowSec - streamState_.lastTickTime) < streamTickIntervalSec_) {
        return;
    }
    streamState_.lastTickTime = nowSec;

    std::vector<size_t> targetMapIndices;
    if (forceFullSync || !enableIncrementalStreaming_ || !streamCameraValid_) {
        targetMapIndices.reserve(currentMap->instances.size());
        for (size_t i = 0; i < currentMap->instances.size(); ++i) {
            targetMapIndices.push_back(i);
        }
    } else {
        const int centerCell = ComputeStreamingCellIndex(streamCameraPos_);
        if (centerCell < 0) {
            for (size_t i = 0; i < currentMap->instances.size(); ++i) {
                targetMapIndices.push_back(i);
            }
        } else {
            constexpr int kCellRadius = 3;
            const int centerX = centerCell % streamState_.gridW;
            const int centerZ = centerCell / streamState_.gridW;
            const int minX = std::max(0, centerX - kCellRadius);
            const int maxX = std::min(streamState_.gridW - 1, centerX + kCellRadius);
            const int minZ = std::max(0, centerZ - kCellRadius);
            const int maxZ = std::min(streamState_.gridH - 1, centerZ + kCellRadius);
            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    const size_t cellIndex = static_cast<size_t>(z * streamState_.gridW + x);
                    if (cellIndex >= streamState_.cellToInstances.size()) continue;
                    const auto& instancesInCell = streamState_.cellToInstances[cellIndex];
                    targetMapIndices.insert(targetMapIndices.end(),
                                            instancesInCell.begin(),
                                            instancesInCell.end());
                }
            }
        }
    }

    std::unordered_set<size_t> targetSet;
    targetSet.reserve(targetMapIndices.size() * 2u + 1u);
    for (size_t mapIndex : targetMapIndices) {
        if (mapIndex < currentMap->instances.size()) {
            targetSet.insert(mapIndex);
        }
    }

    std::vector<size_t> toLoad;
    toLoad.reserve(targetSet.size());
    for (size_t mapIndex : targetSet) {
        if (streamState_.loadedOwnersByMapIndex.find(mapIndex) == streamState_.loadedOwnersByMapIndex.end()) {
            toLoad.push_back(mapIndex);
        }
    }
    std::sort(toLoad.begin(), toLoad.end());

    std::vector<void*> toUnloadOwners;
    toUnloadOwners.reserve(streamState_.loadedOwnersByMapIndex.size());
    for (const auto& [mapIndex, owner] : streamState_.loadedOwnersByMapIndex) {
        if (targetSet.find(mapIndex) == targetSet.end()) {
            toUnloadOwners.push_back(owner);
        }
    }

    int loadBudget = streamLoadBudgetPerTick_;
    int unloadBudget = streamUnloadBudgetPerTick_;
    if (forceFullSync) {
        loadBudget = std::numeric_limits<int>::max();
        unloadBudget = std::numeric_limits<int>::max();
    }
    loadBudget = std::max(loadBudget, 0);
    unloadBudget = std::max(unloadBudget, 0);

    int loadedCount = 0;
    int unloadedCount = 0;
    bool meshLayoutChanged = false;

    for (size_t mapIndex : toLoad) {
        if (loadedCount >= loadBudget) break;
        if (LoadMapInstanceByIndex(currentMap, mapIndex) != nullptr) {
            ++loadedCount;
            meshLayoutChanged = true;
        }
    }

    for (void* owner : toUnloadOwners) {
        if (unloadedCount >= unloadBudget) break;
        if (!owner) continue;
        UnloadInstanceByOwner(owner);
        ++unloadedCount;
        meshLayoutChanged = true;
    }

    if (loadedCount > 0 || unloadedCount > 0) {
        ApplySceneRebuildAfterStreamingChanges(meshLayoutChanged);
        NC::LOGGING::Log("[SCENE][STREAM] Update loaded=", loadedCount,
                         " unloaded=", unloadedCount,
                         " loadedNow=", streamState_.loadedOwnersByMapIndex.size(),
                         " target=", targetSet.size(),
                         " force=", forceFullSync ? 1 : 0);
    }
}

void SceneManager::NotifyWebModelLoaded(const std::string& modelPath,
    const std::string& meshResourceId) {
    NC::LOGGING::Log("[SCENE][WEB] model loaded path=", modelPath, " mesh=", meshResourceId);
}

void SceneManager::NotifyWebModelUnloaded(const std::string& modelPath,
    const std::string& meshResourceId) {
    NC::LOGGING::Log("[SCENE][WEB] model unloaded path=", modelPath, " mesh=", meshResourceId);
}
