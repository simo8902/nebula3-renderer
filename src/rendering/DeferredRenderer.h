// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DEFERREDRENDERER_H
#define NDEVC_DEFERREDRENDERER_H

// ── Work/Play Mode Policy System ─────────────────────────────────────────
enum class RenderMode { Work, Play };

struct FramePolicy {
	bool  editorLayoutEnabled      = true;
	bool  viewportOnlyUI           = false;
	bool  dirtyRenderingEnabled    = false;
	bool  limitEditorPanelRefresh  = false;
	bool  vsyncEnabled             = true;
	int   targetFps                = 0;      // <= 0 means uncapped
};

FramePolicy BuildPolicy(RenderMode mode);

#include "Rendering/Rendering.h"
#include "Rendering/Interfaces/IRenderer.h"
#include "Rendering/Camera.h"
#include "Rendering/Mesh.h"
#include "Rendering/GLStateDebug.h"
#include "Platform/NDEVcHeaders.h"
#include "Rendering/Shader.h"
#include "Rendering/Interfaces/IShaderManager.h"
#include "Assets/Model/ModelInstance.h"
#include "Assets/Map/MapLoader.h"
#include "Rendering/OpenGL/GLHandles.h"
#include "Rendering/GLHandles.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Platform/IPlatform.h"
#include "Platform/IWindow.h"
#include "Input/IInputSystem.h"
#include "Rendering/Interfaces/ITexture.h"
#include "Rendering/Interfaces/ISampler.h"
#include "Engine/SceneManager.h"
#include "Engine/SceneSnapshot.h"
#include "Core/FrameArena.h"
#include "Input/InputSnapshot.h"
#include "Rendering/RenderContext.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

#include "Rendering/RenderGraph.h"
#include "Rendering/RenderResourceRegistry.h"
#include "Rendering/FrameGate.h"
#include <thread>

// ---------------------------------------------------------------------------
// Rolling frame-time statistics — p50/p95/p99 for frame/cpu/swap
// ---------------------------------------------------------------------------
struct RollingStats {
    static constexpr int kWindow = 600;
    float buf[kWindow] = {};
    int   pos   = 0;
    int   count = 0;

    void Push(float v) {
        buf[pos % kWindow] = v;
        ++pos;
        if (count < kWindow) ++count;
    }

    float Percentile(float pct) const {
        if (count == 0) return 0.0f;
        float tmp[kWindow];
        std::copy(buf, buf + count, tmp);
        std::sort(tmp, tmp + count);
        const int idx = static_cast<int>(pct * 0.01f * (count - 1) + 0.5f);
        return tmp[std::max(0, std::min(idx, count - 1))];
    }
};

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
	NDEVC::GL::GLSamplerHandle gSamplerRepeat;
	NDEVC::GL::GLSamplerHandle gSamplerClamp;
	NDEVC::GL::GLVAOHandle screenVAO;
	NDEVC::GL::GLTexHandle blackCubeTex;

	std::unique_ptr<NDEVC::Graphics::IShaderManager> shaderManager;
	SceneManager* scene_ = nullptr;
	NDEVC::GL::GLTexHandle whiteTex, blackTex, normalTex;
	bool shutdownComplete_ = false;

	bool viewportDisabled_ = false;

	bool filterEventsOnly = false;
	int activeEventFilterIndex = 0;

	std::unique_ptr<RenderGraph> renderGraph_;
	std::unique_ptr<RenderResourceRegistry> renderResources_;

	std::shared_ptr<NDEVC::Graphics::IRenderState> cachedBlitState_;

	// Standard engine camera is now global (see Rendering.h)
	
	int frameDrawCalls_ = 0;

	NDEVC::Graphics::GL::UniqueVertexArray quadVAO;
	NDEVC::Graphics::GL::UniqueBuffer quadVBO;
	NDEVC::Graphics::GL::UniqueVertexArray editorPreviewVAO;
	NDEVC::Graphics::GL::UniqueBuffer editorPreviewVBO;
	NDEVC::Graphics::GL::UniqueBuffer editorPreviewColorVBO;
	NDEVC::Graphics::GL::UniqueProgram editorPreviewProgram;
	bool optRenderLOG = false;
	bool optCheckGLErrors = false;
	bool scenePrepared = false;
	using RendererClock = std::chrono::steady_clock;
	RendererClock::time_point rendererClockStart_ = RendererClock::now();

	double lastFrame = 0.0;
	double deltaTime = 0.0;

	static constexpr double CAMERA_FIXED_TIMESTEP = 1.0 / 60.0;
	InputSnapshot currentInput_;

	// ── Phase 5: double-buffered arenas + render thread ──────────────────
	// frameArenas_[arenaFlip_ % 2] is written by the main thread (BuildSnapshot).
	// The render thread reads from the same slot during that frame; the main
	// resets the OTHER slot for the next frame, so no aliasing occurs.
	FrameArena    frameArenas_[2];
	int           arenaFlip_          = 0;
	SceneSnapshot lastSnapshot_;        // written by main, read by render thread (gate-protected)
	FrameGate     gate_;
	std::jthread  renderThread_;
	bool          contextTransferred_       = false;
	// Written by main thread before PostFrame; read by render thread after WaitFrame.
	// Gate ordering makes these safe without atomics.
	bool          pendingViewportDisabled_  = false;
	bool          pendingDirtySkip_         = false;
	bool          pendingAllowViewportInput_ = false;

	void InitScreenQuad() {
		quadVAO.reset();
		quadVBO.reset();

		glCreateVertexArrays(1, quadVAO.put());
		glCreateBuffers(1, quadVBO.put());

		const float v[] = {
			-1.f,-1.f,  0.f,0.f,
			 1.f,-1.f,  1.f,0.f,
			-1.f, 1.f,  0.f,1.f,
			 1.f, 1.f,  1.f,1.f
		};
		glNamedBufferStorage(quadVBO, sizeof(v), v, 0);

		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);

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

	void InitEditorPreviewGrid() {
		editorPreviewVAO.reset();
		editorPreviewVBO.reset();
		editorPreviewColorVBO.reset();
		editorPreviewProgram.reset();

		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> colors;
		constexpr int halfExtent = 20;
		constexpr float spacing = 1.0f;
		positions.reserve((halfExtent * 2 + 1) * 4 + 6);
		colors.reserve(positions.capacity());

		auto pushLine = [&](glm::vec3 a, glm::vec3 b, glm::vec3 color) {
			positions.push_back(a);
			positions.push_back(b);
			colors.push_back(color);
			colors.push_back(color);
		};

		for (int i = -halfExtent; i <= halfExtent; ++i) {
			const float p = static_cast<float>(i) * spacing;
			const glm::vec3 minor(0.28f, 0.30f, 0.34f);
			const glm::vec3 major(0.42f, 0.45f, 0.50f);
			const glm::vec3 color = (i == 0) ? major : minor;
			pushLine(glm::vec3(-halfExtent * spacing, 0.0f, p), glm::vec3(halfExtent * spacing, 0.0f, p), color);
			pushLine(glm::vec3(p, 0.0f, -halfExtent * spacing), glm::vec3(p, 0.0f, halfExtent * spacing), color);
		}
		pushLine(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(0.9f, 0.18f, 0.14f));
		pushLine(glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.2f, 0.85f, 0.22f));
		pushLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.2f, 0.45f, 1.0f));

		glCreateVertexArrays(1, editorPreviewVAO.put());
		glCreateBuffers(1, editorPreviewVBO.put());
		glCreateBuffers(1, editorPreviewColorVBO.put());

		glNamedBufferStorage(editorPreviewVBO,
		             static_cast<GLsizeiptr>(positions.size() * sizeof(glm::vec3)),
		             positions.data(), 0);

		glNamedBufferStorage(editorPreviewColorVBO,
		             static_cast<GLsizeiptr>(colors.size() * sizeof(glm::vec3)),
		             colors.data(), 0);

		glBindVertexArray(editorPreviewVAO);
		// AAA Strategy: Pure DSA attribute binding
		glVertexArrayVertexBuffer(editorPreviewVAO, 0, editorPreviewVBO, 0, sizeof(glm::vec3));
		glVertexArrayVertexBuffer(editorPreviewVAO, 1, editorPreviewColorVBO, 0, sizeof(glm::vec3));

		glEnableVertexArrayAttrib(editorPreviewVAO, 0);
		glVertexArrayAttribFormat(editorPreviewVAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
		glVertexArrayAttribBinding(editorPreviewVAO, 0, 0);

		glEnableVertexArrayAttrib(editorPreviewVAO, 1);
		glVertexArrayAttribFormat(editorPreviewVAO, 1, 3, GL_FLOAT, GL_FALSE, 0);
		glVertexArrayAttribBinding(editorPreviewVAO, 1, 1);

		glBindVertexArray(0);

		const char* vertexSrc = R"GLSL(
#version 460 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 0) uniform mat4 uViewProjection;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";
		const char* fragmentSrc = R"GLSL(
#version 460 core
in vec3 vColor;
layout(location = 0) out vec4 oColor;
void main() {
    oColor = vec4(vColor, 1.0);
}
)GLSL";
		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vs, 1, &vertexSrc, nullptr);
		glCompileShader(vs);
		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fs, 1, &fragmentSrc, nullptr);
		glCompileShader(fs);
		editorPreviewProgram.id = glCreateProgram();
		glAttachShader(editorPreviewProgram.id, vs);
		glAttachShader(editorPreviewProgram.id, fs);
		glLinkProgram(editorPreviewProgram.id);
		glDeleteShader(vs);
		glDeleteShader(fs);
	}

	void DrawEditorPreviewViewport() {
		if (!editorPreviewVAO || !editorPreviewProgram) return;

		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Draw Editor Preview Grid");
		NC::LOGGING::Log("[DEBUG] DrawEditorPreviewViewport called! width=", width, " height=", height);

		GLuint fbo = device_ ? device_->GetDefaultFramebuffer() : 0;
		NC::LOGGING::Log("[DEBUG] DrawEditorPreviewViewport fbo=", fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, width, height);
		DumpGLState("DrawEditorPreviewViewport");
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_STENCIL_TEST);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_GREATER);
		glDepthMask(GL_TRUE);
		glDisable(GL_CULL_FACE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.15f, 0.15f, 0.15f, 1.0f); // AAA standard viewport background
		glClearDepth(0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		const glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
		glUseProgram(editorPreviewProgram);
		GLint loc = glGetUniformLocation(editorPreviewProgram, "uViewProjection");
		if (loc >= 0) {
			glUniformMatrix4fv(loc, 1, GL_FALSE, &viewProjection[0][0]);
		}
		glBindVertexArray(editorPreviewVAO);
		glDrawArrays(GL_LINES, 0, (20 * 2 + 1) * 4 + 6);
		glBindVertexArray(0);
		glUseProgram(0);
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

	::FrameProfile frameProfile_;
	int profileFrameCounter_ = 0;
	static constexpr int kProfileLogInterval = 1200;

    RollingStats rsFrame_;
    RollingStats rsCpu_;
    RollingStats rsSwap_;

	// Frame-level fence sync for GPU pacing
	static constexpr int kMaxFramesInFlight = 2;
	GLsync frameFences_[kMaxFramesInFlight + 1] = {};
	int frameFenceIdx_ = 0;

	// ── Work/Play Mode Policy State ─────────────────────────────────────
	RenderMode   renderMode_        = RenderMode::Work;
	FramePolicy  activePolicy_      = {};
	bool         modeSwitchPending_ = false;

	// Dirty-frame tracking: skip expensive passes when nothing changed
	bool   dirtyFlag_              = true;
	bool   dirtyFrameSkippedLast_  = false;
	glm::mat4 dirtyLastViewMatrix_       = glm::mat4(0.0f);
	glm::mat4 dirtyLastProjectionMatrix_ = glm::mat4(0.0f);
	int    dirtyLastWidth_         = 0;
	int    dirtyLastHeight_        = 0;

	void MarkDirty();
	bool ConsumeDirty();
	void HandleDroppedPaths();
	bool ProcessPendingDroppedMapLoad(double currentFrame);
	void QueueDroppedMapLoad(const std::string& mapDropPath);

	// Editor panel refresh rate limiting
	double panelLastUpdateTime_    = 0.0;
	float  panelEffectiveHz_       = 60.0f;
	static constexpr float kPanelIdleHz   = 15.0f;
	static constexpr float kPanelActiveHz = 60.0f;
	double fpsCounterTime_   = 0.0;
	int    fpsCounterFrames_ = 0;
	float  displayFps_       = 0.0f;
	bool   shiftHeldAtLastLeftClick_ = false;
	bool   lmbWasDown_               = false;

	// Input state tracking
	bool   isRotating_               = false;
	double lastMouseX_               = 0.0;
	double lastMouseY_               = 0.0;
	float  accumulatedScrollDelta_   = 0.0f;

	std::vector<std::string> pendingDroppedPaths_;
	enum class MapDropLoadStage : int {
		Idle = 0,
		Queued,
		LoadFile,
		ApplyScene,
		Complete,
		Failed
	};
	MapDropLoadStage mapDropLoadStage_ = MapDropLoadStage::Idle;
	std::string mapDropLoadPath_;
	std::unique_ptr<MapData> mapDropLoadedMap_;
	double mapDropLoadStartSec_ = 0.0;
	double mapDropLoadFileSec_ = 0.0;
	double mapDropLoadApplySec_ = 0.0;
	double mapDropLoadTotalSec_ = 0.0;
	double mapDropLoadDisplayUntilSec_ = 0.0;
	float mapDropLoadProgress_ = 0.0f;
	std::string mapDropLoadStatus_;

	void ApplyPolicy(const FramePolicy& policy);
	void LogPolicySnapshot(const char* reason) const;

	bool editorModeEnabled_ = false;
	bool editorHosted_ = false;
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

	void initGLFW();
	void setupRenderPasses();
	void PrepareSceneForRenderPassSetup();
	void SetupModernRenderGraph();
	void CreateCachedRenderStatesForPasses();
	void LogRenderLoopAnimationClips() const;
	void renderSingleFrame();
	void RenderThreadLoop();
	void RenderFrameGL(bool synchronous);
	double GetTimeSeconds() const;
	bool PollFrameInput();
	void HandleModeSwitchInput();
	void UpdateDirtyFrameState();
	void ApplyRenderPassBaseline(bool shouldExecuteRenderPasses);
	void ExecuteModernRenderPasses(bool shouldExecuteRenderPasses,
	                               std::function<void()> onDrawlistsReady = {});
	void WaitForFrameFence();
	void CreateFrameFence();
	void UpdateFrameProfileLogs();
	void HandleFrameDebugInput(bool allowViewportKeyboardInput);
	void HandleViewportPicking(bool allowViewportKeyboardInput);
	FrameFlags BuildFrameFlags() const;
	RenderContext BuildRenderContext();
	void ApplyRenderContextResults(const RenderContext& ctx);
	void releaseOwnedGLResources();
	void SaveLastScenePath(const std::string& scenePath);
	std::string LoadLastScenePath() const;
	void AutoLoadLastScene();
	void WriteWebSnapshot(const char* reason);

public:
	DeferredRenderer();
	~DeferredRenderer() override;

	static void SetPendingExternalPlatform(
	    std::unique_ptr<NDEVC::Platform::IPlatform> platform,
	    std::shared_ptr<NDEVC::Platform::IWindow>   window,
	    GLADloadproc                                loader);

	void resizeFramebuffers(int newWidth, int newHeight);

	void Initialize() override;
	void Shutdown() override;
	void UpdateFrameTime() override;
	void SetFrameDeltaTime(double dt) override;
	void PollEvents() override;
	void UpdateCameraFixed();
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

	void AttachScene(SceneManager& scene) override;
	SceneManager& GetScene() override;
	Camera& GetCamera() override;
	const Camera& GetCamera() const override;

	RenderMode GetRenderMode() const;
	void SetRenderMode(RenderMode mode);
	void SetEditorHosted(bool hosted);

	void QueueDroppedPaths(const std::vector<std::string>& paths);
};
#endif
