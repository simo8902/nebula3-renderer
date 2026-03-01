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
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>

namespace {
glm::mat4 BuildLocalTransform(const Node* node) {
    if (!node) return glm::mat4(1.0f);
    const glm::vec3 pos(node->position.x, node->position.y, node->position.z);
    const glm::quat rot(node->rotation.w, node->rotation.x, node->rotation.y, node->rotation.z);
    const glm::vec3 scl(node->scale.x, node->scale.y, node->scale.z);
    return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scl);
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

    if (!map) {
        ownedCurrentMap_.reset();
        currentMap = nullptr;
        currentMapSourcePath_.clear();
        NC::LOGGING::Warning("[SCENE] LoadMap cleared current map (null input)");
        return;
    }
    ownedCurrentMap_ = std::make_unique<MapData>(*map);
    currentMap = ownedCurrentMap_.get();
    LoadMapInstances(currentMap);

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
    NC::LOGGING::Log("[SCENE] Clear end");
}

void SceneManager::Tick(double dt, const Camera& camera) {
    (void)dt;
    (void)camera;
    NC::LOGGING::Log("[SCENE] Tick dt=", dt, " instances=", instances.size());
    UpdateIncrementalStreaming(false);
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
        dc.isStatic = true;
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
    // TODO: move from DeferredRenderer
}

void SceneManager::BuildStreamingIndex(const MapData* map) {
    (void)map;
    // TODO: move from DeferredRenderer
}

int SceneManager::ComputeStreamingCellIndex(const glm::vec3& worldPos) const {
    (void)worldPos;
    // TODO: move from DeferredRenderer
    return -1;
}

ModelInstance* SceneManager::LoadMapInstanceByIndex(const MapData* map, size_t mapIndex) {
    (void)map;
    (void)mapIndex;
    // TODO: move from DeferredRenderer
    return nullptr;
}

void SceneManager::UnloadInstanceByOwner(void* owner) {
    (void)owner;
    // TODO: move from DeferredRenderer
}

void SceneManager::RebuildNodeMapFromInstances() {
    // TODO: move from DeferredRenderer
}

void SceneManager::ApplySceneRebuildAfterStreamingChanges(bool meshLayoutChanged) {
    (void)meshLayoutChanged;
    // TODO: move from DeferredRenderer
}

void SceneManager::UpdateIncrementalStreaming(bool forceFullSync) {
    (void)forceFullSync;
    // TODO: move from DeferredRenderer
}

void SceneManager::NotifyWebModelLoaded(const std::string& modelPath,
    const std::string& meshResourceId) {
    (void)modelPath;
    (void)meshResourceId;
    // TODO: move from DeferredRenderer
}

void SceneManager::NotifyWebModelUnloaded(const std::string& modelPath,
    const std::string& meshResourceId) {
    (void)modelPath;
    (void)meshResourceId;
    // TODO: move from DeferredRenderer
}
