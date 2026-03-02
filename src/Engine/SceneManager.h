#ifndef NDEVC_SCENEMANAGER_H
#define NDEVC_SCENEMANAGER_H

#include "Rendering/DrawCmd.h"
#include "Rendering/Camera.h"
#include "Assets/Model/ModelInstance.h"
#include "Assets/Map/MapLoader.h"
#include "Assets/Particles/ParticleSystemNode.h"
#include "Animation/AnimatorNodeInstance.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Rendering/Interfaces/IShaderManager.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class SceneManager {
public:
    // Called once after the GL context and MegaBuffer exist
    void Initialize(NDEVC::Graphics::IGraphicsDevice* device,
                    NDEVC::Graphics::IShaderManager* shaderMgr);

    // Scene mutation (called by game code or the renderer facade)
    void AppendModel(const std::string& path, const glm::vec3& pos,
                     const glm::quat& rot, const glm::vec3& scale);
    void LoadMap(const MapData* map);
    void ReloadMap();                        // reloads currentMapSourcePath_
    void Clear();                            // unloads everything

    // Per-frame update: streaming tick, animation, particle attachment
    void Tick(double dt, const Camera& camera);

    // Frame preparation: clears and repopulates all draw lists
    // The draw lists are owned by DeferredRenderer and passed in by pointer.
    void PrepareDrawLists(
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
        std::vector<DrawCmd*>& animatedDraws);

    // Returns true if scene draw lists changed since last PrepareDrawLists call.
    bool IsDrawListsDirty() const { return drawListsDirty_; }

    // Read-only accessors used by DeferredRenderer internals
    const std::string& GetCurrentMapPath() const { return currentMapSourcePath_; }
    const std::unordered_map<std::string, int>& GetLoadedModelRefCounts() const
        { return loadedModelRefCountByPath_; }
    const std::unordered_map<std::string, std::string>& GetLoadedMeshPaths() const
        { return loadedMeshByModelPath_; }
    friend class DeferredRenderer;

private:
    NDEVC::Graphics::IGraphicsDevice* device_ = nullptr;
    NDEVC::Graphics::IShaderManager* shaderMgr_ = nullptr;

    // --- Instance / model tracking ---
    struct ParticleAttach {
        std::shared_ptr<Particles::ParticleSystemNode> node;
        std::string nodeName;
        const Node* sourceNode = nullptr;
        glm::mat4 local{1.0f};
        void* instance = nullptr;
        bool dynamicTransform = false;
    };
    std::vector<ParticleAttach> particleNodes;
    std::unordered_map<std::string, Node*> nodeMap;
    std::vector<std::shared_ptr<ModelInstance>> instances;
    std::unordered_map<void*, double> instanceSpawnTimes;
    std::unordered_map<void*, std::string> instanceModelPathByOwner_;
    std::unordered_map<void*, std::string> instanceMeshResourceByOwner_;
    std::unordered_map<std::string, int> loadedModelRefCountByPath_;
    std::unordered_map<std::string, std::string> loadedMeshByModelPath_;
    std::vector<std::unique_ptr<AnimatorNodeInstance>> animatorInstances;

    // --- Map + streaming ---
    struct StreamWindowState {
        const MapData* map = nullptr;
        int gridW = 0, gridH = 0;
        float cellSizeX = 32.0f, cellSizeZ = 32.0f;
        glm::vec2 originXZ{0.0f, 0.0f};
        std::vector<std::vector<size_t>> cellToInstances;
        std::unordered_map<size_t, void*> loadedOwnersByMapIndex;
        double lastTickTime = 0.0;
        bool initialized = false;
    };
    StreamWindowState streamState_;
    glm::vec3 streamCameraPos_{0.0f, 0.0f, 0.0f};
    bool streamCameraValid_ = false;
    bool enableIncrementalStreaming_ = false;
    int streamLoadBudgetPerTick_ = 96;
    int streamUnloadBudgetPerTick_ = 128;
    double streamTickIntervalSec_ = 0.05;
    std::unique_ptr<MapData> ownedCurrentMap_;
    MapData* currentMap = nullptr;
    std::string currentMapSourcePath_;
    std::unordered_map<std::string, std::shared_ptr<NDEVC::Graphics::ITexture>> textureRefsByResourceId_;
    std::vector<DrawCmd> sceneSolidDraws_;
    std::vector<DrawCmd> sceneAlphaTestDraws_;
    std::vector<DrawCmd> sceneDecalDraws_;
    std::vector<DrawCmd> sceneEnvironmentDraws_;
    std::vector<DrawCmd> sceneEnvironmentAlphaDraws_;
    std::vector<DrawCmd> sceneSimpleLayerDraws_;
    std::vector<DrawCmd> sceneRefractionDraws_;
    std::vector<DrawCmd> scenePostAlphaUnlitDraws_;
    std::vector<DrawCmd> sceneWaterDraws_;
    std::vector<DrawCmd> sceneParticleDraws_;
    bool drawListsDirty_ = true;
    std::unordered_map<void*, glm::mat4> instanceTransformCache_;
    std::unordered_set<void*> movedInstanceOwners_;

    ModelInstance* appendN3WTransform(const std::string& path,
        const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);
    void BuildDrawsWithTransform(const Model& model,
        const glm::mat4& instanceTransform, void* instance,
        std::vector<DrawCmd>& out);
    NDEVC::Graphics::ITexture* GetOrCreateTextureRef(const std::string& textureResourceId,
        NDEVC::Graphics::TextureType type);
    void LoadMapInstances(const MapData* map);
    void ResetStreamingState();
    void BuildStreamingIndex(const MapData* map);
    int ComputeStreamingCellIndex(const glm::vec3& worldPos) const;
    ModelInstance* LoadMapInstanceByIndex(const MapData* map, size_t mapIndex);
    void UnloadInstanceByOwner(void* owner);
    void RebuildNodeMapFromInstances();
    void ApplySceneRebuildAfterStreamingChanges(bool meshLayoutChanged);
    void UpdateIncrementalStreaming(bool forceFullSync = false);
    void PropagateMovedInstanceTransforms(std::vector<DrawCmd>& draws);
    void PropagateMovedInstanceTransformsToAllSceneDraws();
    void NotifyWebModelLoaded(const std::string& modelPath,
        const std::string& meshResourceId);
    void NotifyWebModelUnloaded(const std::string& modelPath,
        const std::string& meshResourceId);
};

#endif
