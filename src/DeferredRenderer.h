// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DEFERREDRENDERER_H
#define NDEVC_DEFERREDRENDERER_H

#include "visibility/VisibilityGrid.h"
#include "rendering/abstract/IRenderer.h"
#include "Camera.h"
#include "DrawCmd.h"
#include "Mesh.h"
#include "NDEVcHeaders.h"
#include "Shader.h"
#include "rendering/abstract/IShaderManager.h"
#include "Model/ModelInstance.h"
#include "Map/MapLoader.h"
#include "ParticleData/ParticleSystemNode.h"
#include "DrawBatchSystem.h"
#include "FrameGraph.h"
#include "rendering/abstract/IGraphicsDevice.h"
#include "rendering/abstract/IPlatform.h"
#include "rendering/abstract/IWindow.h"
#include "rendering/abstract/IInputSystem.h"
#include "rendering/abstract/ITexture.h"
#include "rendering/abstract/IFramebuffer.h"
#include "rendering/abstract/ISampler.h"
#include "rendering/abstract/IBuffer.h"
#include "AnimatorNodeInstance.h"
#include <unordered_set>

class DeferredRenderer : public NDEVC::Graphics::IRenderer {
	std::unique_ptr<NDEVC::Graphics::IGraphicsDevice> device_;
	std::unique_ptr<NDEVC::Platform::IPlatform> platform_;
	std::shared_ptr<NDEVC::Platform::IWindow> window_;
	std::shared_ptr<NDEVC::Platform::IInputSystem> inputSystem_;
	int width = 1280, height = 800;
	std::shared_ptr<NDEVC::Graphics::ITexture> whiteTex_abstracted;
	std::shared_ptr<NDEVC::Graphics::ITexture> blackTex_abstracted;
	std::shared_ptr<NDEVC::Graphics::ITexture> normalTex_abstracted;
	std::shared_ptr<NDEVC::Graphics::ITexture> blackCubeTex_abstracted;
	std::shared_ptr<NDEVC::Graphics::ISampler> samplerRepeat_abstracted;
	std::shared_ptr<NDEVC::Graphics::ISampler> samplerClamp_abstracted;
	std::shared_ptr<NDEVC::Graphics::ISampler> samplerShadow_abstracted;
	std::shared_ptr<NDEVC::Graphics::IBuffer> sphereVBO_abstracted;
	std::shared_ptr<NDEVC::Graphics::IBuffer> sphereEBO_abstracted;
	glm::mat4 cachedViewMatrix;
	glm::mat4 cachedProjectionMatrix;
	GLuint gPositionVS = 0, gPositionWS = 0, gPositionWSRead = 0;
	GLuint gBuffer = 0, gPosition = 0, gNormalDepthPacked = 0, gAlbedoSpec = 0, gDepth = 0;
	GLuint gEmissive = 0;
	GLuint lightFBO = 0, lightBuffer = 0;
	GLuint gDepthDecal = 0, gDepthDecalFBO = 0;
	GLuint gDepthCopyReadFBO = 0;
	GLuint sceneDepthStencil = 0;
	GLuint gSamplerRepeat = 0;
	GLuint gSamplerClamp = 0;
	GLuint sceneFBO = 0, sceneColor = 0;
	GLuint sceneFBO2 = 0, sceneColor2 = 0;
	GLuint screenVAO = 0;
	GLuint blackCubeTex = 0;

	std::shared_ptr<NDEVC::Graphics::ITexture> lightBufferTex;
	std::shared_ptr<NDEVC::Graphics::ITexture> sceneColorTex;
	std::shared_ptr<NDEVC::Graphics::ITexture> sceneDepthStencilTex;
	std::shared_ptr<NDEVC::Graphics::ITexture> sceneColor2Tex;

	struct ParticleAttach {
		std::shared_ptr<Particles::ParticleSystemNode> node;
		std::string nodeName;
		const Node* sourceNode = nullptr;
		glm::mat4 local{1.0f};
		void* instance = nullptr;
		bool dynamicTransform = false;
	};
	std::vector<ParticleAttach> particleNodes;
	std::unique_ptr<NDEVC::Graphics::IShaderManager> shaderManager;
	std::unordered_map<std::string, Node*> nodeMap;

	std::vector<std::shared_ptr<ModelInstance>> instances;
	std::unordered_map<void*, double> instanceSpawnTimes;
	std::unordered_map<void*, std::string> instanceModelPathByOwner_;
	std::unordered_map<void*, std::string> instanceMeshResourceByOwner_;
	std::unordered_map<std::string, int> loadedModelRefCountByPath_;
	std::unordered_map<std::string, std::string> loadedMeshByModelPath_;
	std::vector<std::unique_ptr<AnimatorNodeInstance>> animatorInstances;
	GLuint whiteTex = 0, blackTex = 0, normalTex = 0;

	struct StreamWindowState {
		const MapData* map = nullptr;
		int gridW = 0;
		int gridH = 0;
		float cellSizeX = 32.0f;
		float cellSizeZ = 32.0f;
		glm::vec2 originXZ{0.0f, 0.0f};
		std::vector<std::vector<size_t>> cellToInstances;
		std::unordered_map<size_t, void*> loadedOwnersByMapIndex;
		double lastTickTime = 0.0;
		bool initialized = false;
	};
	StreamWindowState streamState_;
	bool enableIncrementalStreaming_ = true;
	int streamLoadBudgetPerTick_ = 96;
	int streamUnloadBudgetPerTick_ = 128;
	double streamTickIntervalSec_ = 0.05;

	std::unique_ptr<MapData> ownedCurrentMap_;
	MapData* currentMap = nullptr;
	std::string currentMapSourcePath_;
	bool shutdownComplete_ = false;

	static constexpr int NUM_CASCADES = 2;
	static constexpr int SHADOW_WIDTH = 1024;
	static constexpr int SHADOW_HEIGHT = 1024;
	std::shared_ptr<NDEVC::Graphics::ITexture> shadowMapTextures[NUM_CASCADES];
	GLuint shadowMapCascades[NUM_CASCADES];
	GLuint shadowFBOCascades[NUM_CASCADES];
	glm::mat4 lightSpaceMatrices[NUM_CASCADES];
	// Default tuned for NUM_CASCADES == 2:
	// cascade0: 0.1-45m, cascade1: 45-300m.
	float cascadeSplits[5] = { 0.1f, 45.0f, 300.0f, 300.0f, 300.0f };
	// Shadow pass indirect draw resources (megabuffer path)
	GLuint shadowMatrixSSBO_ = 0;
	size_t shadowMatrixSSBOCapacity_ = 0;
	GLuint shadowIndirectBuffer_ = 0;
	size_t shadowIndirectBufferCapacity_ = 0;

	struct PointLight {
		glm::vec3 position;
		float range;
		glm::vec4 color;
	};
	std::vector<PointLight> pointLights;
	GLuint sphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
	GLuint pointLightInstanceVBO = 0;
	size_t pointLightInstanceCapacity = 0;
	int sphereIndexCount = 0;
	void generateSphereMesh();
	bool debugShadowView = false;
	int debugShadowCascade = 0;

	std::unique_ptr<NDEVC::Graphics::FrameGraph> shadowGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> geometryGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> decalGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> lightingGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> particleGraph;

	bool filterEventsOnly = false;
	int activeEventFilterIndex = 0;
	bool enableInstanceFrustumCulling = true;
	float frustumMargin = 150.0f;
	int maxInstancesPerFrame = 18000;
	bool enableDrawCulling = true;
	bool deferredShaderUsesPackedGBuffer_ = false;
	bool deferredShaderHasNormalMatrixUniform_ = false;
	bool deferredShaderCompatibilityLogged_ = false;
	bool deferredPackedCompatReady_ = false;

	// Visibility system (Diablo 2 style spatial culling)
	VisibilityGrid solidVisGrid_;
	VisibilityGrid alphaTestVisGrid_;
	VisibilityGrid envVisGrid_;
	VisibilityGrid envAlphaVisGrid_;
	VisibilityGrid waterVisGrid_;
	VisibilityGrid postAlphaVisGrid_;
	VisibilityGrid simpleLayerVisGrid_;

	bool enableVisibilityGrid_ = true;
	float visibleRange_ = 300.0f;       // world units; 0 = no range limit (frustum-only)
	std::vector<int> visibleCells_;     // scratch buffer, reused per frame
	std::vector<int> lastVisibleCells_; // previous frame's visible set (change detection)

	// Top-down isometric camera (Diablo 2 style): ~55° pitch, facing -Z, elevated above origin.
	// Position is recentered above the map after load.
	Camera camera_{"MainCamera",
		glm::vec3(0.0f, 200.0f, 120.0f),    // elevated + offset back
		glm::vec3(0.0f, -0.819f, -0.574f),  // looking down at ~55° facing -Z
		glm::vec3(0.0f, 1.0f, 0.0f),        // world up
		270.0f, -55.0f,                      // yaw, pitch
		200.0f, 0.1f,                        // pan speed, mouse sensitivity
		45.0f, 0.1f, 3000.0f                 // fov, near, far
	};

	DrawCmd* selectedObject = nullptr;
	int selectedIndex = -1;

	std::vector<DrawCmd> solidDraws;
	std::vector<DrawCmd> decalDraws;
	std::vector<DrawCmd> particleDraws;
	std::vector<DrawCmd> environmentDraws;
	std::vector<DrawCmd> environmentAlphaDraws;
	std::vector<DrawCmd> simpleLayerDraws;
	std::vector<DrawCmd> alphaTestDraws;
	std::vector<DrawCmd> refractionDraws;
	std::vector<DrawCmd> postAlphaUnlitDraws;
	std::vector<DrawCmd> waterDraws;

	int lastFrameDrawCalls_ = 0;
	int frameDrawCalls_ = 0;
	bool decalDrawsSorted = false;
	bool decalBatchDirty = true;
	std::vector<DrawCmd*> animatedDraws;
	std::vector<size_t> solidShaderVarAnimatedIndices;

	struct TextureBindingCache {
		GLuint boundTextures[16] = {0};
		GLuint boundSamplers[16] = {0};

		void reset() {
			std::fill(std::begin(boundTextures), std::end(boundTextures), 0);
			std::fill(std::begin(boundSamplers), std::end(boundSamplers), 0);
		}
	} textureCache;

	// Debug: wireframe overlay showing which visibility cells are currently visible
	bool debugShowVisibilityCells_ = false;
	GLuint debugCellVAO_ = 0;
	GLuint debugCellVBO_ = 0;
	GLuint debugLineProgram_ = 0;

	void BuildVisibilityGrids();
	void UpdateVisibilityThisFrame(const Camera::Frustum& frustum);
	void DrawVisibilityCellsDebug();

	void initGLFW();
	void initDeferred();
	void initCascadedShadowMaps();
	void renderCascadedShadows(const glm::vec3& camPos, const glm::vec3& camForward);
	void setupRenderPasses();
	void renderSingleFrame();
	void releaseOwnedGLResources();
	void bindDrawTextures(const DrawCmd& dc);
	void renderMeshDraw(const DrawCmd& dc);
	ModelInstance* appendN3WTransform(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);
	void BuildDrawsWithTransform(const Model& model, const glm::mat4& instanceTransform, void* instance, std::vector<DrawCmd>& out);
	void LoadMapInstances(const MapData* map);
	void ResetStreamingState();
	void BuildStreamingIndex(const MapData* map);
	int ComputeStreamingCellIndex(const glm::vec3& worldPos) const;
	ModelInstance* LoadMapInstanceByIndex(const MapData* map, size_t mapIndex);
	void UnloadInstanceByOwner(void* owner);
	void RebuildNodeMapFromInstances();
	void ApplySceneRebuildAfterStreamingChanges(bool meshLayoutChanged);
	void UpdateIncrementalStreaming(bool forceFullSync = false);
	void UpdateLookAtSelection(bool force = false);
	bool InitializeImGui();
	void ShutdownImGui();
	void RenderImGui();
	void RenderLookAtPanel();
	struct DisabledDrawKey {
		void* instance = nullptr;
		std::string nodeName;
		std::string shdr;
		int group = -1;

		bool operator==(const DisabledDrawKey& other) const {
			return instance == other.instance &&
				group == other.group &&
				nodeName == other.nodeName &&
				shdr == other.shdr;
		}
	};
	struct DisabledDrawKeyHash {
		size_t operator()(const DisabledDrawKey& k) const {
			size_t h = std::hash<void*>{}(k.instance);
			h ^= std::hash<int>{}(k.group) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<std::string>{}(k.nodeName) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<std::string>{}(k.shdr) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};
	DisabledDrawKey MakeDisabledDrawKey(const DrawCmd& dc) const;
	bool IsDrawDisabled(const DrawCmd& dc) const;
	void SetDrawDisabled(const DrawCmd& dc, bool disabled);
	void ApplyDisabledDrawFlags();
	void ClearDisabledDraws();

	bool IsInstanceVisible(const Instance& inst, const Template& tmpl, const Camera::Frustum& frustum, const glm::vec3& camPos);
	void rebuildAnimatedDrawLists();
	void WriteWebSnapshot(const char* reason);
	void NotifyWebModelLoaded(const std::string& modelPath, const std::string& meshResourceId);
	void NotifyWebModelUnloaded(const std::string& modelPath, const std::string& meshResourceId);

	GLuint quadVAO = 0;
	GLuint quadVBO = 0;
	bool optRenderLOG = false;
	bool optCheckGLErrors = false;
	bool scenePrepared = false;

	double lastFrame = 0.0;
	double deltaTime = 0.0;


	void InitScreenQuad() {
		if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
		if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }

		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);

		const float v[] = {
			-1.f,-1.f,  0.f,0.f,
			 1.f,-1.f,  1.f,0.f,
			-1.f, 1.f,  0.f,1.f,
			 1.f, 1.f,  1.f,1.f
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

		screenVAO = quadVAO;

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void InitShadowSampler() {
		if (device_) {
			NDEVC::Graphics::SamplerDesc shadowDesc;
			shadowDesc.minFilter = NDEVC::Graphics::SamplerFilter::Linear;
			shadowDesc.magFilter = NDEVC::Graphics::SamplerFilter::Linear;
			shadowDesc.wrapS = NDEVC::Graphics::SamplerWrap::ClampToBorder;
			shadowDesc.wrapT = NDEVC::Graphics::SamplerWrap::ClampToBorder;
			shadowDesc.useCompare = true;
			shadowDesc.compareFunc = NDEVC::Graphics::CompareFunc::LessEqual;
			samplerShadow_abstracted = device_->CreateSampler(shadowDesc);
			if (samplerShadow_abstracted) {
				gSamplerShadow = *(GLuint*)samplerShadow_abstracted->GetNativeHandle();
				const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				glSamplerParameterfv(gSamplerShadow, GL_TEXTURE_BORDER_COLOR, borderColor);
				glSamplerParameteri(gSamplerShadow, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
				glSamplerParameteri(gSamplerShadow, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
			}
		} else {
			glGenSamplers(1, &gSamplerShadow);
			glSamplerParameteri(gSamplerShadow, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glSamplerParameteri(gSamplerShadow, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glSamplerParameteri(gSamplerShadow, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
			glSamplerParameteri(gSamplerShadow, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
			const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			glSamplerParameterfv(gSamplerShadow, GL_TEXTURE_BORDER_COLOR, borderColor);
			glSamplerParameteri(gSamplerShadow, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(gSamplerShadow, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		}
	}

	DrawCmd cachedObj;
	int cachedIndex = -1;
	bool imguiInitialized = false;
	bool clickPickEnabled = true;
	bool pickIncludeTransparent = false;
	bool pickIncludeDecals = false;
	double pickLastUpdateMs = 0.0;
	int pickCandidateCount = 0;
	std::unordered_set<DisabledDrawKey, DisabledDrawKeyHash> disabledDrawSet;
	std::vector<DisabledDrawKey> disabledDrawOrder;
	int disabledSelectionIndex = -1;

	inline void bindTexture(uint32_t slot, GLuint texture) {
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, texture);
	}

	inline void bindSampler(uint32_t slot, GLuint sampler) {
		if (device_) {
			if (sampler == 0) {
				device_->BindSampler(nullptr, slot);
				return;
			}
			if (sampler == gSamplerRepeat && samplerRepeat_abstracted) {
				device_->BindSampler(samplerRepeat_abstracted.get(), slot);
				return;
			}
			if (sampler == gSamplerClamp && samplerClamp_abstracted) {
				device_->BindSampler(samplerClamp_abstracted.get(), slot);
				return;
			}
			if (sampler == gSamplerShadow && samplerShadow_abstracted) {
				device_->BindSampler(samplerShadow_abstracted.get(), slot);
				return;
			}
		}
		glBindSampler(slot, sampler);
	}

	inline void clearError(const char* label = nullptr) {
		if (device_) {
			GLenum err = GL_NO_ERROR;
			while ((err = glGetError()) != GL_NO_ERROR) {
				if (label && *label) {
					std::cerr << "[GL] Error at " << label << ": 0x" << std::hex << err << std::dec << "\n";
				} else {
					std::cerr << "[GL] Error: 0x" << std::hex << err << std::dec << "\n";
				}
			}
		}
	}

	GLuint gSamplerShadow = 0;

public:
	DeferredRenderer();
	~DeferredRenderer() override;

	void resizeFramebuffers(int newWidth, int newHeight);

	void Initialize() override;
	void Shutdown() override;
	void RenderFrame() override;
	void Resize(int width, int height) override;

	void AppendModel(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) override;
	void LoadMap(const MapData* map) override;
	void ReloadMapWithCurrentMode() override;

	void SetCheckGLErrors(bool enabled) override;
	bool GetCheckGLErrors() const override;
	void SetRenderLog(bool enabled) override;

	DrawCmd* GetSelectedObject() override;
	int GetSelectedIndex() const override;

	Camera& GetCamera() override;
	const Camera& GetCamera() const override;
};
#endif