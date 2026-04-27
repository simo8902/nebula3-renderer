#ifndef NDEVC_SCENEMANAGER_H
#define NDEVC_SCENEMANAGER_H

#include "Rendering/DrawCmd.h"
#include "Rendering/Camera.h"
#include "Assets/Model/ModelInstance.h"
#include "Assets/Map/MapLoader.h"
#include "Core/FrameArena.h"
#include "Engine/SceneSnapshot.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class SceneManager {
public:
    enum class ParityStage : int {
        Idle            = 0,  // No load in progress
        LevelLoad       = 1,  // loaderserver::LoadLevel
        CategoryLoad    = 2,  // categorymanager::LoadInstances
        EntityCreate    = 3,  // entityloader row iteration + factorymanager
        EntityActivate  = 4,  // entitymanager::AttachEntity -> OnActivate
        ProxyCreate     = 5,  // graphicsproperty::SetupGraphicsEntities
        InternalAttach  = 6,  // internalmodelentityhandler -> stage attach
        PendingValid    = 7,  // internalmodelentity async validity gate
        Valid           = 8,  // render-ready
        Count
    };
    static const char* ParityStageName(ParityStage s);

    // ── Per-load/frame parity counters ─────────────────────────────────
    struct ParityCounters {
        int categoryRowsLoaded     = 0;  // map instance rows processed
        int runtimeEntitiesCreated = 0;  // ModelInstance objects created
        int entitiesActivated      = 0;  // instances with transform set
        int graphicsProxiesCreated = 0;  // BuildDrawsWithTransform calls
        int internalAttached       = 0;  // draws classified into buckets
        int validRenderables       = 0;  // draws with valid mesh+megabuffer
        int submittedDrawCommands  = 0;  // draws emitted to renderer lists
        void reset() { *this = ParityCounters{}; }
    };
    const ParityCounters& GetParityCounters() const { return parityCounters_; }
    ParityStage GetCurrentParityStage() const { return parityStage_; }

    // ── Per-instance runtime lifecycle state (Nebula entity lifecycle) ──
    // Tracks each loaded instance through a deterministic state progression
    // matching Nebula's entity -> graphicsproperty -> internalmodelentity flow.
    enum class EntityLifecycleState : int {
        Created          = 0,  // ModelInstance allocated (≡ entityloader row)
        Activated        = 1,  // Transform applied        (≡ entitymanager::AttachEntity)
        ProxyCreated     = 2,  // Draws built              (≡ graphicsproperty::SetupGraphicsEntities)
        InternalAttached = 3,  // Draws classified         (≡ internalmodelentityhandler stage attach)
        PendingValid     = 4,  // Waiting for model ready  (≡ internalmodelentity async gate)
        Valid            = 5,  // Render-ready
        Count
    };
    static const char* EntityLifecycleStateName(EntityLifecycleState s);

    struct RuntimeEntityRecord {
        void*                owner       = nullptr;
        std::string          modelPath;
        EntityLifecycleState state       = EntityLifecycleState::Created;
        int                  drawCount   = 0;  // draws classified for this entity
    };
    const std::unordered_map<void*, RuntimeEntityRecord>& GetRuntimeEntities() const
        { return runtimeEntities_; }
    int CountRuntimeEntitiesInState(EntityLifecycleState s) const;

    // Scene mutation (called by game code or the renderer facade)
    void AppendModel(const std::string& path, const glm::vec3& pos,
                     const glm::quat& rot, const glm::vec3& scale);
    void LoadMap(const MapData* map);
    void LoadMap(const MapData* map, const std::string& sourcePath);
    void ReloadMap();                        // reloads currentMapSourcePath_
    void Clear();                            // unloads everything

    using SceneEntityId = uint64_t;
    struct AuthoredEntity {
        SceneEntityId id = 0;
        SceneEntityId parentId = 0;  // 0 = root
        std::string name;
        std::string modelPath;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        bool enabled = true;
        void* runtimeOwner = nullptr;
    };
    struct SceneAssetInfo {
        std::string guid;
        std::string name;
        std::string sourcePath;
        std::string mapPath;
        bool dirty = false;
        size_t entityCount = 0;
    };
    void CreateScene(const std::string& sceneName = "Untitled Scene");
    bool OpenScene(const std::string& path);
    bool SetActiveScene(const std::string& path);
    bool SaveScene();
    bool SaveScene(const std::string& path);
    bool SaveSceneAs(const std::string& path);
    void ImportMapAsEditableScene(const MapData* map, const std::string& sourcePath);
    SceneEntityId CreateEntity(const std::string& name,
                               const glm::vec3& pos = glm::vec3(0.0f),
                               const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                               const glm::vec3& scale = glm::vec3(1.0f));
    SceneEntityId CreateModelEntity(const std::string& modelPath,
                                    const glm::vec3& pos,
                                    const glm::quat& rot,
                                    const glm::vec3& scale,
                                    const std::string& name = "");
    bool DestroyEntity(SceneEntityId id);
    bool SetEntityTransform(SceneEntityId id,
                            const glm::vec3& pos,
                            const glm::quat& rot,
                            const glm::vec3& scale);
    bool SetEntityName(SceneEntityId id, const std::string& name);
    SceneAssetInfo GetActiveSceneInfo() const;
    bool HasActiveScenePath() const { return !activeScenePath_.empty(); }
    bool IsSceneDirty() const { return activeSceneDirty_; }
    const std::vector<AuthoredEntity>& GetAuthoredEntities() const { return authoredEntities_; }
    SceneEntityId FindEntityIdByRuntimeOwner(void* owner) const;

    void Tick(double dt, const Camera& camera);

    // Converts the current draw lists into arena-allocated RenderProxy PODs.
    // Call once per frame after Tick(). The returned snapshot is valid until
    // the next FrameArena::Reset() on the supplied arena.
    SceneSnapshot BuildSnapshot(FrameArena& arena, const Camera& camera) const;

    // Frame preparation: clears and repopulates all draw lists
    // The draw lists are owned by DeferredRenderer and passed in by pointer.
    void PrepareDrawLists(
        const Camera& camera,
        std::vector<DrawCmd>& solidDraws,
        std::vector<DrawCmd>& alphaTestDraws,
        std::vector<DrawCmd>& decalDraws,
        std::vector<DrawCmd>& environmentDraws,
        std::vector<DrawCmd>& environmentAlphaDraws,
        std::vector<DrawCmd>& simpleLayerDraws,
        std::vector<DrawCmd>& refractionDraws,
        std::vector<DrawCmd>& postAlphaUnlitDraws,
        std::vector<DrawCmd>& waterDraws);

    // Returns true if scene draw lists changed since last PrepareDrawLists call.
    bool IsDrawListsDirty() const { return drawListsDirty_; }

    // Returns true if buildMegaBuffer() was already called during this tick's
    // streaming rebuild, so the renderer can skip the redundant Phase 4 call.
    bool WasMegaBufferRebuiltThisTick() const { return megaBufferRebuiltThisTick_; }

    // Read-only accessors used by DeferredRenderer internals
    const std::string& GetCurrentMapPath() const { return currentMapSourcePath_; }
    const std::unordered_map<std::string, int>& GetLoadedModelRefCounts() const
        { return loadedModelRefCountByPath_; }
    const std::unordered_map<std::string, std::string>& GetLoadedMeshPaths() const
        { return loadedMeshByModelPath_; }

    struct ExportSceneEntity {
        std::string name;
        std::string templateName;
        glm::vec3   position{0.0f};
        glm::vec4   rotation{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec3   scale{1.0f};
        bool        isStatic = true;
        bool        isAlpha  = false;
        bool        isDecal  = false;
    };

    struct ExportSceneSnapshot {
        std::string sceneGuid;
        std::string sceneName;
        std::string mapPath;
        std::string mapName;
        MapInfo mapInfo{};
        std::vector<ExportSceneEntity> entities;
        std::vector<std::string> loadedModelPaths;
        std::vector<std::string> loadedMeshPaths;
        std::vector<std::string> loadedTexturePaths;
        std::vector<std::string> loadedShaderNames;
    };

    ExportSceneSnapshot BuildExportSceneSnapshot(bool includeUnloadedDependencies = false) const;
    friend class DeferredRenderer;
    friend class ParticlePass;

private:
    std::unordered_map<std::string, Node*> nodeMap;
    std::vector<std::unique_ptr<ModelInstance>> instances;
    std::unordered_map<void*, double> instanceSpawnTimes;
    std::unordered_map<void*, std::string> instanceModelPathByOwner_;
    std::unordered_map<void*, std::string> instanceMeshResourceByOwner_;
    std::unordered_map<std::string, int> loadedModelRefCountByPath_;
    std::unordered_map<std::string, std::string> loadedMeshByModelPath_;

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
        bool stable = false;
        int  lastStableCenterCell = -1;
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
    bool drawListsDirty_ = true;
    bool megaBufferRebuiltThisTick_ = false;
    int pendingEntityCount_ = 0;
    std::unordered_map<void*, glm::mat4> instanceTransformCache_;
    std::unordered_set<void*> movedInstanceOwners_;
    bool transformsStable_ = false;

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
    AuthoredEntity* FindAuthoredEntity(SceneEntityId id);
    const AuthoredEntity* FindAuthoredEntity(SceneEntityId id) const;
    std::string BuildDefaultEntityName(const std::string& preferredBase) const;
    std::string EnsureScenePathHasExtension(const std::string& path) const;
    bool SaveSceneToPath(const std::string& path);
    void MarkSceneDirty();
    void RebindAuthoredRuntimeOwners();
    void ClearAuthoredRuntimeOwners();
    bool suppressSceneDirty_ = false;
    uint64_t nextSceneEntityId_ = 1;
    std::vector<AuthoredEntity> authoredEntities_;
    std::string activeSceneGuid_;
    std::string activeSceneName_ = "Untitled Scene";
    std::string activeScenePath_;
    bool activeSceneDirty_ = false;

    // ── Parity diagnostics state ───────────────────────────────────────
    ParityStage    parityStage_ = ParityStage::Idle;
    ParityCounters parityCounters_;
    int            parityMapLoadSeq_ = 0;   // monotonic map load counter
    int            parityFrameSeq_   = 0;   // monotonic frame counter
    void ParityLogStageTransition(ParityStage next);
    void ParityLogCounters(const char* context);

    // ── Runtime entity lifecycle tracking ───────────────────────────────
    std::unordered_map<void*, RuntimeEntityRecord> runtimeEntities_;
    void TransitionEntity(void* owner, EntityLifecycleState newState);
    void TickPendingEntities();
    void MaterializeEntityDraws(void* owner, const std::string& modelPath,
        ModelInstance& instance, const Model& model);

public:
    // Parity validation / debug dump (callable from editor or diagnostics)
    void ParityDumpRuntimeEntities() const;
};

#endif
