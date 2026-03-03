// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DEFERREDRENDERER_H
#define NDEVC_DEFERREDRENDERER_H

#include "Rendering/Visibility/VisibilityGrid.h"
#include "Rendering/Visibility/InternalVisibilityStage.h"
#include "Rendering/Interfaces/IRenderer.h"
#include "Rendering/Camera.h"
#include "Rendering/DrawCmd.h"
#include "Rendering/MaterialGPU.h"
#include "Rendering/Mesh.h"
#include "Platform/NDEVcHeaders.h"
#include "Rendering/Shader.h"
#include "Rendering/Interfaces/IShaderManager.h"
#include "Assets/Model/ModelInstance.h"
#include "Assets/Map/MapLoader.h"
#include "Assets/Particles/ParticleSystemNode.h"
#include "Rendering/DrawBatchSystem.h"
#include "Rendering/FrameGraph.h"
#include "Rendering/OpenGL/GLHandles.h"
#include "Rendering/GLHandles.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Platform/IPlatform.h"
#include "Platform/IWindow.h"
#include "Input/IInputSystem.h"
#include "Rendering/Interfaces/ITexture.h"
#include "Rendering/Interfaces/IFramebuffer.h"
#include "Rendering/Interfaces/ISampler.h"
#include "Rendering/Interfaces/IBuffer.h"
#include "Animation/AnimatorNodeInstance.h"
#include "Engine/SceneManager.h"
#include <array>
#include <cstdint>
#include <unordered_map>
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
	NDEVC::GL::GLTexHandle gPositionVS, gPositionWS;
	NDEVC::GL::GLTexHandle gPositionWSRead;
	NDEVC::GL::GLFBOHandle gBuffer;
	NDEVC::GL::GLTexHandle gPosition, gNormalDepthPacked, gAlbedoSpec, gDepth;
	NDEVC::GL::GLTexHandle gEmissive;
	NDEVC::GL::GLFBOHandle lightFBO;
	NDEVC::GL::GLTexHandle lightBuffer;
	NDEVC::GL::GLTexHandle gDepthDecal;
	NDEVC::GL::GLFBOHandle gDepthDecalFBO;
	NDEVC::GL::GLFBOHandle gDepthCopyReadFBO;
	NDEVC::GL::GLTexHandle sceneDepthStencil;
	NDEVC::GL::GLSamplerHandle gSamplerRepeat;
	NDEVC::GL::GLSamplerHandle gSamplerClamp;
	NDEVC::GL::GLFBOHandle sceneFBO;
	NDEVC::GL::GLTexHandle sceneColor;
	NDEVC::GL::GLFBOHandle sceneFBO2;
	NDEVC::GL::GLTexHandle sceneColor2;
	NDEVC::GL::GLVAOHandle screenVAO;
	NDEVC::GL::GLTexHandle blackCubeTex;

	std::shared_ptr<NDEVC::Graphics::ITexture> lightBufferTex;
	std::shared_ptr<NDEVC::Graphics::ITexture> sceneColorTex;
	std::shared_ptr<NDEVC::Graphics::ITexture> sceneDepthStencilTex;
	std::shared_ptr<NDEVC::Graphics::ITexture> sceneColor2Tex;

	std::unique_ptr<NDEVC::Graphics::IShaderManager> shaderManager;
	SceneManager scene_;
	NDEVC::GL::GLTexHandle whiteTex, blackTex, normalTex;
	bool shutdownComplete_ = false;

	static constexpr int NUM_CASCADES = 2;
	static constexpr int SHADOW_WIDTH = 1024;
	static constexpr int SHADOW_HEIGHT = 1024;
	std::shared_ptr<NDEVC::Graphics::ITexture> shadowMapTextures[NUM_CASCADES];
	NDEVC::GL::GLTexHandle shadowMapCascades[NUM_CASCADES];
	std::array<NDEVC::GL::GLFBOHandle, NUM_CASCADES> shadowFBOCascades;
	glm::mat4 lightSpaceMatrices[NUM_CASCADES];
	// Default tuned for NUM_CASCADES == 2:
	// cascade0: 0.1-45m, cascade1: 45-300m.
	float cascadeSplits[5] = { 0.1f, 45.0f, 300.0f, 300.0f, 300.0f };
	// Shadow pass indirect draw resources (megabuffer path)
	NDEVC::Graphics::GL::UniqueBuffer shadowMatrixSSBO_;
	size_t shadowMatrixSSBOCapacity_ = 0;
	NDEVC::Graphics::GL::UniqueBuffer shadowIndirectBuffer_;
	size_t shadowIndirectBufferCapacity_ = 0;

	// Shadow pass scratch buffers – reused each frame to eliminate per-frame heap allocs (fix #3)
	struct ShadowCaster {
		const DrawCmd* draw = nullptr;
		float radius = 0.0f;
		float viewDepth = 0.0f;
		glm::vec3 worldCenter{0.0f};
	};
	struct ShadowInstancedGroup {
		uint32_t count = 0;
		uint32_t firstIndex = 0;
		std::vector<glm::mat4> matrices;
	};
	std::vector<ShadowCaster> shadowCasters_;
	std::vector<ShadowCaster> simpleLayerShadowCasters_;
	bool shadowCastersDirty_ = true;
	bool shadowCasterCacheHit_ = false;
	std::vector<glm::mat4>    shadowMatrices_;
	std::vector<DrawCommand>  shadowCommands_;
	struct ShadowGeomGroup {
		uint32_t indexCount = 0;
		uint32_t firstIndex = 0;
		std::vector<uint32_t> casterIndices;
	};
	std::vector<ShadowGeomGroup> solidShadowGeomGroups_;

	// SimpleLayer shadow instancing GPU resources (fix #4)
	NDEVC::Graphics::GL::UniqueBuffer slShadowMatrixSSBO_;
	size_t slShadowMatrixSSBOCapacity_ = 0;
	NDEVC::Graphics::GL::UniqueBuffer slShadowIndirectBuffer_;
	size_t slShadowIndirectBufferCapacity_ = 0;

	// SimpleLayer GBuffer instancing GPU resources (fix #4)
	NDEVC::Graphics::GL::UniqueBuffer slGBufWorldMatSSBO_;
	size_t slGBufWorldMatSSBOCapacity_ = 0;
	NDEVC::Graphics::GL::UniqueBuffer slGBufInvWorldSSBO_;
	size_t slGBufInvWorldSSBOCapacity_ = 0;
	NDEVC::Graphics::GL::UniqueBuffer slGBufTilingSSBO_;
	size_t slGBufTilingSSBOCapacity_ = 0;
	NDEVC::Graphics::GL::UniqueBuffer slGBufIndirectBuffer_;
	size_t slGBufIndirectBufferCapacity_ = 0;

	// GBuffer instancing per-frame scratch vectors (reused to avoid heap allocs)
	struct SlGBufGroup {
		NDEVC::Graphics::ITexture* texSlot[4]{};
		bool  alphaTest      = false;
		float alphaClipRef   = 0.0f;
		int   cullMode       = 0;
		bool  receivesDecals = false;
		uint32_t cmdOffset   = 0;
		uint32_t cmdCount    = 0;
	};
	std::vector<SlGBufGroup>  slGBufGroups_;
	std::vector<DrawCommand>  slGBufCmds_;
	std::vector<glm::mat4>    slGBufWorldMats_;
	std::vector<glm::mat4>    slGBufInvWorldMats_;
	std::vector<float>        slGBufTilings_;
	bool slGBufCacheValid_ = false;
	uint64_t slGBufVisHash_ = 0;

	struct PointLight {
		glm::vec3 position;
		float range;
		glm::vec4 color;
	};
	std::vector<PointLight> pointLights;
	NDEVC::Graphics::GL::UniqueVertexArray sphereVAO;
	NDEVC::GL::GLBufHandle sphereVBO, sphereEBO;
	NDEVC::Graphics::GL::UniqueBuffer pointLightInstanceVBO;
	size_t pointLightInstanceCapacity = 0;
	int sphereIndexCount = 0;
	void generateSphereMesh();
	bool viewportDisabled_ = false;
	bool debugShadowView = false;
	int debugShadowCascade = 0;

	std::unique_ptr<NDEVC::Graphics::FrameGraph> shadowGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> geometryGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> decalGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> lightingGraph;
	std::unique_ptr<NDEVC::Graphics::FrameGraph> particleGraph;

	std::shared_ptr<NDEVC::Graphics::IRenderState> cachedBlitState_;
	std::shared_ptr<NDEVC::Graphics::IRenderState> cachedEnvState_;
	std::shared_ptr<NDEVC::Graphics::IRenderState> cachedEnvStateNoCull_;
	std::shared_ptr<NDEVC::Graphics::IRenderState> cachedGeomState_;

	bool filterEventsOnly = false;
	int activeEventFilterIndex = 0;
	bool enableInstanceFrustumCulling = true;
	float frustumMargin = 150.0f;
	int maxInstancesPerFrame = 18000;
	bool enableDrawCulling = true;
	bool useLegacyDeferredInit_ = false;
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
	VisibilityGrid refractionVisGrid_;
	VisibilityGrid decalVisGrid_;

	bool enableVisibilityGrid_ = true;
	bool visGridRevealedAll_ = false;
	bool visResolveSkipped_ = false;
	float visibleRange_ = 0.0f;         // world units; 0 = no range limit (frustum-only)
	std::vector<int> visibleCells_;     // scratch buffer, reused per frame
	std::vector<int> lastVisibleCells_; // previous frame's visible set (change detection)
	InternalVisibilityStage visibilityStage_;
	std::uint64_t visibilityStageFrameIndex_ = 0;

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

	DrawBatchSystem solidBatchSystem_;
	DrawBatchSystem alphaTestBatchSystem_;
	DrawBatchSystem environmentBatchSystem_;
	DrawBatchSystem environmentAlphaBatchSystem_;
	DrawBatchSystem decalBatchSystem_;

	int lastFrameDrawCalls_ = 0;
	int frameDrawCalls_ = 0;
	bool decalDrawsSorted = false;
	bool decalBatchDirty = true;
	std::vector<DrawCmd*> animatedDraws;
	std::vector<size_t> solidShaderVarAnimatedIndices;
	bool solidShaderVarAnimatedIndicesDirty_ = true;
	Camera::Frustum frameFrustum_;

	struct TextureBindingCache {
		uint32_t boundTextures[16] = {0};
		uint32_t boundSamplers[16] = {0};

		void reset() {
			std::fill(std::begin(boundTextures), std::end(boundTextures), 0);
			std::fill(std::begin(boundSamplers), std::end(boundSamplers), 0);
		}
	} textureCache;

	// Debug: wireframe overlay showing which visibility cells are currently visible
	bool debugShowVisibilityCells_ = false;
	NDEVC::Graphics::GL::UniqueVertexArray debugCellVAO_;
	NDEVC::Graphics::GL::UniqueBuffer debugCellVBO_;
	NDEVC::Graphics::GL::UniqueProgram debugLineProgram_;

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
	void UpdateLookAtSelection(bool force = false);
	void InvalidateSelection();
	void ValidateSelectionPointer();
	bool InitializeImGui();
	void ShutdownImGui();
	void RenderImGui();
	void RenderEditorShell();
	void BuildEditorDockLayout(unsigned int dockspaceId);
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

	NDEVC::Graphics::GL::UniqueVertexArray quadVAO;
	NDEVC::Graphics::GL::UniqueBuffer quadVBO;
	bool optRenderLOG = false;
	bool optCheckGLErrors = false;
	bool scenePrepared = false;

	double lastFrame = 0.0;
	double deltaTime = 0.0;


	void InitScreenQuad() {
		quadVAO.reset();
		quadVBO.reset();

		glGenVertexArrays(1, quadVAO.put());
		glGenBuffers(1, quadVBO.put());
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

		screenVAO.reset();
		screenVAO.id = quadVAO.id;
		quadVAO.id = 0;

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
				gSamplerShadow.reset();
				gSamplerShadow.id = *(GLuint*)samplerShadow_abstracted->GetNativeHandle();
				const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				glSamplerParameterfv(gSamplerShadow, GL_TEXTURE_BORDER_COLOR, borderColor);
				glSamplerParameteri(gSamplerShadow, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
				glSamplerParameteri(gSamplerShadow, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
			}
		} else {
			glGenSamplers(1, gSamplerShadow.put());
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

	// ── Frame profiler (Patch #5, expanded Patch #8) ─────────────────
	struct FrameProfile {
		// Phase totals (chrono, ms)
		double sceneTick     = 0.0;
		double prepareDraws  = 0.0;
		double rebuild       = 0.0;
		double shadowPass    = 0.0;
		double geometryPass  = 0.0;
		double decalPass     = 0.0;
		double lightingPass  = 0.0;
		double forwardPass   = 0.0;
		double editorClear   = 0.0;
		double imguiRender   = 0.0;
		double swapBuffers   = 0.0;
		double frameTotal    = 0.0;
		// Shadow CPU breakdown
		double shadowCasterBuild = 0.0;
		double shadowGroup   = 0.0;
		double shadowUpload  = 0.0;
		double shadowDraw    = 0.0;
		double shadowSL      = 0.0;
		// Geometry CPU breakdown
		double geomSetup      = 0.0;
		double geomSolidCull  = 0.0;
		double geomSolidFlush = 0.0;
		double geomAlphaCull  = 0.0;
		double geomAlphaFlush = 0.0;
		double geomSL         = 0.0;
		double geomEnv        = 0.0;
		// SL sub-phases
		double geomSLVis      = 0.0;
		double geomSLSort     = 0.0;
		double geomSLGroup    = 0.0;
		double geomSLUpload   = 0.0;
		double geomSLRender   = 0.0;
		int    geomSLGroupCount   = 0;
		int    geomSLVisibleCount = 0;
		int    slCacheHit         = 0;
		double fenceWait          = 0.0;
		// GPU timer query results (ms)
		double gpuShadow    = 0.0;
		double gpuGeometry  = 0.0;
		double gpuDecal     = 0.0;
		double gpuLighting  = 0.0;
		double gpuForward   = 0.0;
		int    fwdFlushCount = 0;
	};
	FrameProfile frameProfile_;
	int profileFrameCounter_ = 0;
	static constexpr int kProfileLogInterval = 300;
	static constexpr int kGpuQueryCount = 5;
	GLuint gpuQueries_[kGpuQueryCount] = {};
	bool gpuQueriesInit_ = false;

	// Frame-level fence sync for GPU pacing (Patch #11)
	static constexpr int kMaxFramesInFlight = 2;
	GLsync frameFences_[kMaxFramesInFlight + 1] = {};
	int frameFenceIdx_ = 0;

	// GPU-driven material SSBO (Phase 2A)
	NDEVC::GL::GLBufHandle materialSSBO_;
	size_t materialSSBOCapacity_ = 0;
	size_t materialSSBOCount_ = 0;
	bool materialSSBODirty_ = true;
	uint64_t fallbackWhiteHandle_ = 0;
	uint64_t fallbackBlackHandle_ = 0;
	uint64_t fallbackNormalHandle_ = 0;
	uint64_t fallbackBlackCubeHandle_ = 0;
	void buildMaterialSSBO();

	// GPU-driven decal material SSBO (Phase 4)
	NDEVC::GL::GLBufHandle decalMaterialSSBO_;
	size_t decalMaterialSSBOCapacity_ = 0;
	size_t decalMaterialSSBOCount_ = 0;
	void buildDecalMaterialSSBO();

	// GPU-driven water material SSBO (Phase 5)
	NDEVC::GL::GLBufHandle waterMaterialSSBO_;
	size_t waterMaterialSSBOCapacity_ = 0;
	size_t waterMaterialSSBOCount_ = 0;
	void buildWaterMaterialSSBO();

	// GPU-driven refraction material SSBO (Phase 5)
	NDEVC::GL::GLBufHandle refractionMaterialSSBO_;
	size_t refractionMaterialSSBOCapacity_ = 0;
	size_t refractionMaterialSSBOCount_ = 0;
	void buildRefractionMaterialSSBO();

	NDEVC::GL::GLBufHandle envAlphaMaterialSSBO_;
	size_t envAlphaMaterialSSBOCapacity_ = 0;
	size_t envAlphaMaterialSSBOCount_ = 0;
	void buildEnvAlphaMaterialSSBO();

	DrawCmd cachedObj;
	int cachedIndex = -1;
	bool imguiInitialized = false;
	bool editorModeEnabled_ = false;
	bool editorDockInitialized_ = false;
	float editorToolbarHeight_ = 44.0f;
	float sceneViewportX_ = 0.0f;
	float sceneViewportY_ = 0.0f;
	float sceneViewportW_ = 0.0f;
	float sceneViewportH_ = 0.0f;
	bool sceneViewportValid_ = false;
	bool sceneViewportHovered_ = false;
	bool sceneViewportFocused_ = false;
	bool editorViewportInputRouting_ = true;
	bool clickPickEnabled = true;
	bool pickIncludeTransparent = false;
	bool pickIncludeDecals = false;
	double pickLastUpdateMs = 0.0;
	int pickCandidateCount = 0;
	std::unordered_set<DisabledDrawKey, DisabledDrawKeyHash> disabledDrawSet;
	std::vector<DisabledDrawKey> disabledDrawOrder;
	int disabledSelectionIndex = -1;

	inline bool IsSceneViewportPointerInside(const double x, const double y) const {
		if (!sceneViewportValid_) return false;
		return x >= static_cast<double>(sceneViewportX_) &&
			   y >= static_cast<double>(sceneViewportY_) &&
			   x <= static_cast<double>(sceneViewportX_ + sceneViewportW_) &&
			   y <= static_cast<double>(sceneViewportY_ + sceneViewportH_);
	}

	inline bool IsSceneViewportInputActive() const {
		if (!editorModeEnabled_ || !editorViewportInputRouting_) return false;
		if (!sceneViewportValid_) return false;
		return sceneViewportHovered_ || sceneViewportFocused_;
	}

	inline GLuint toTextureHandle(NDEVC::Graphics::ITexture* texture) const {
		return texture ? *(GLuint*)texture->GetNativeHandle() : 0;
	}

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
		extern bool gEnableGLErrorChecking;
		if (!gEnableGLErrorChecking) return;
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

	NDEVC::GL::GLSamplerHandle gSamplerShadow;

public:
	DeferredRenderer();
	~DeferredRenderer() override;

	void resizeFramebuffers(int newWidth, int newHeight);

	void Initialize() override;
	void Shutdown() override;
	void PollEvents() override;
	void RenderFrame() override;
	void RenderSingleFrame() override;
	bool ShouldClose() const override;
	void Resize(int width, int height) override;

	void AppendModel(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);
	void LoadMap(const MapData* map);
	void ReloadMapWithCurrentMode();

	void SetCheckGLErrors(bool enabled) override;
	bool GetCheckGLErrors() const override;
	void SetRenderLog(bool enabled) override;

	DrawCmd* GetSelectedObject();
	int GetSelectedIndex() const;

	SceneManager& GetScene() override;
	Camera& GetCamera() override;
	const Camera& GetCamera() const override;
};
#endif
