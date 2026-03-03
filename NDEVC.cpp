// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#define _CRT_SECURE_NO_WARNINGS

#include "src/Engine/Core/Initialization.h"
#include "Core/Logger.h"
#include "Core/VFS.h"
#include "Rendering/ValidationLayer.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "glad/glad.h"
#include "GLFW/glfw3.h"

static constexpr bool kDisableParticles = true; // 2 fps
static constexpr bool kDisableAnimations = true; // 3 fps
static constexpr bool kDisableParticlePass = true;

static constexpr bool kDisableGeometryPass = false;
static constexpr bool kDisableDecalPass = false;

// fwd pass
static constexpr bool kDisableForwardPass = true;
static constexpr bool kDisableWaterPass = true;
static constexpr bool kDisableEnvironmentAlphaPass = true;
static constexpr bool kDisableRefractionPass = true;
static constexpr bool kDisablePostAlphaUnlitPass = true;

// shadows and lighting
static constexpr bool kDisablePointLightPass = false;
static constexpr bool kDisableShadowPass = false;
static constexpr bool kDisableShadows = false;
static constexpr bool kDisableCompositionPass = false;
static constexpr bool kDisableComposePass = false;
static constexpr bool kDisableLighting = false;
static constexpr bool kDisableLightingPass = false;
static constexpr bool kDisableSLPass = false;
static constexpr bool kDisableEnvPass = false;

static constexpr bool kEnableWebAddon = false;
static constexpr bool kEditorRouteInput = true;
static constexpr bool kImGuiViewportOnly = false;
static constexpr bool kUseLegacyDeferredInit = false;
static constexpr bool kDisableFrustumCulling = false;
static constexpr bool kDisableFaceCulling = false;
static constexpr bool kForceNonBatchedGeometry = false;
static constexpr bool kDisableViewport = false;
static constexpr bool kNoPresentWhenViewportDisabled = true;
static constexpr bool kDisableFrameFenceWait = true;
static constexpr bool kFullscreenExclusive = false;
static constexpr const char* kAssetRootPath = "C:\\drasa_online\\work";
static constexpr const char* kModelsRootPath = "C:\\drasa_online\\work\\models";
static constexpr const char* kMeshesRootPath = "C:\\drasa_online\\work\\meshes";
static constexpr const char* kMapsRootPath = "C:\\drasa_online\\work\\maps";
static constexpr const char* kAnimsRootPath = "C:\\drasa_online\\work\\anims";
static constexpr const char* kTexturesRootPath = "C:\\drasa_online\\work\\textures";
static constexpr const char* kScenesRootPath = "C:\\drasa_online\\work\\scenes";

static bool EnvVarExists(const char* name) {
	if (!name || !name[0]) return false;
	return std::getenv(name) != nullptr;
}

static void SetEnvDefault(const char* name, const char* value) {
	if (!name || !name[0] || EnvVarExists(name)) return;
#if defined(_WIN32)
	_putenv_s(name, value ? value : "");
#else
	setenv(name, value ? value : "", 0);
#endif
}

static void SetEnvValue(const char* name, const char* value) {
	if (!name || !name[0]) return;
#if defined(_WIN32)
	_putenv_s(name, value ? value : "");
#else
	setenv(name, value ? value : "", 1);
#endif
}

static std::string NormalizePackagedPathOrFallback(const std::string& rawPath, const char* fallback) {
	if (!rawPath.empty()) {
		std::string s = rawPath;
		for (char& c : s) {
			if (c == '\\') c = '/';
		}
		while (!s.empty() && (s.front() == '"' || s.front() == ' ')) {
			s.erase(s.begin());
		}

		std::string low = s;
		std::transform(low.begin(), low.end(), low.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		static constexpr const char* kMarkers[] = {
			"/shaders/", "/maps/", "/models/", "/meshes/",
			"/textures/", "/anims/", "/scenes/", nullptr
		};
		for (int i = 0; kMarkers[i]; ++i) {
			const size_t pos = low.find(kMarkers[i]);
			if (pos != std::string::npos) {
				return s.substr(pos + 1);
			}
		}
		if (!(s.size() >= 2 && s[1] == ':')) {
			return s;
		}
	}
	return fallback ? std::string(fallback) : std::string();
}

static void ApplyPackageRuntimeOverrides() {
	namespace fs = std::filesystem;

	auto discoverNdpkNearExe = []() -> std::string {
#if defined(_WIN32)
		wchar_t exeBuf[32768] = {};
		if (!GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)))) {
			return {};
		}
		std::error_code scanEc;
		const fs::path exeDir = fs::path(exeBuf).parent_path();
		for (const auto& entry : fs::directory_iterator(exeDir, scanEc)) {
			if (entry.path().extension() == ".ndpk") {
				return entry.path().string();
			}
		}
#endif
		return {};
	};

	// Auto-discover a .ndpk blob placed next to the executable when
	// NDEVC_PACKAGE_PATH is not set (i.e. no batch file / launcher needed).
	if (!std::getenv("NDEVC_PACKAGE_PATH") || !std::getenv("NDEVC_PACKAGE_PATH")[0]) {
		if (const std::string discoveredPath = discoverNdpkNearExe(); !discoveredPath.empty()) {
			SetEnvValue("NDEVC_PACKAGE_PATH", discoveredPath.c_str());
			NC::LOGGING::Log("[APP] Auto-discovered NDPK: ", discoveredPath);
		}
	}

	const char* ndpk = std::getenv("NDEVC_PACKAGE_PATH");
	if (!ndpk || !ndpk[0]) return;

	fs::path ndpkPath = fs::path(ndpk);
	std::error_code ec;
	if (!fs::exists(ndpkPath, ec) || !fs::is_regular_file(ndpkPath, ec)) {
		NC::LOGGING::Warning("[APP] NDEVC_PACKAGE_PATH invalid, retrying auto-discovery: ", ndpkPath.string());
		if (const std::string discoveredPath = discoverNdpkNearExe(); !discoveredPath.empty()) {
			ndpkPath = fs::path(discoveredPath);
			SetEnvValue("NDEVC_PACKAGE_PATH", discoveredPath.c_str());
			NC::LOGGING::Log("[APP] Auto-discovered NDPK fallback: ", discoveredPath);
		} else {
			NC::LOGGING::Error("[APP] NDEVC_PACKAGE_PATH invalid: ", ndpkPath.string());
			return;
		}
	}

	// Mount the NDPK entirely in memory via OS file-mapping.
	// Zero bytes are written to disk — all assets are served from the mapped view.
	std::string startupMapPath;
	if (!NC::VFS::Instance().MountNdpk(ndpkPath.string(), startupMapPath)) {
		NC::LOGGING::Error("[APP] VFS mount failed for: ", ndpkPath.string());
		return;
	}

	// Standalone mode: force virtual package roots so runtime never exposes/dev-depends
	// on work paths like C:\drasa_online\work.
	SetEnvValue("NDEVC_SOURCE_DIR", ".");
	SetEnvValue("NDEVC_MODELS_ROOT", "./models");
	SetEnvValue("NDEVC_MESHES_ROOT", "./meshes");
	SetEnvValue("NDEVC_MAPS_ROOT", "./maps");
	SetEnvValue("NDEVC_ANIMS_ROOT", "./anims");
	SetEnvValue("NDEVC_TEXTURES_ROOT", "./textures");
	SetEnvValue("NDEVC_SCENES_ROOT", "./scenes");
	SetEnvValue("NDEVC_VSYNC", "0");
	SetEnvValue("NDEVC_EDITOR_MODE", "0");
	// Work standalone keeps a minimal play-mode overlay; Playtest/Shipping are pure fullscreen viewport.
	const uint8_t buildProfile = NC::VFS::Instance().GetProfile();
	SetEnvValue("NDEVC_IMGUI_VIEWPORT_ONLY", buildProfile == 0 ? "0" : "1");
	if (!startupMapPath.empty()) {
		const std::string runtimeStartupMap =
			NormalizePackagedPathOrFallback(startupMapPath, startupMapPath.c_str());
		SetEnvValue("NDEVC_STARTUP_MAP", runtimeStartupMap.c_str());
		startupMapPath = runtimeStartupMap;
	}
	NC::LOGGING::Log("[APP] VFS mounted NDPK=", ndpkPath.string(),
	                 " profile=", static_cast<int>(buildProfile),
	                 " startupMap=", startupMapPath.empty() ? "<none>" : startupMapPath);
}

static void ApplyRuntimeToggles() {
	NC::LOGGING::Log("[APP] ApplyRuntimeToggles begin");
	SetEnvDefault("NDEVC_DISABLE_PARTICLES", kDisableParticles ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_ANIMATIONS", kDisableAnimations ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_SHADOW_PASS", kDisableShadowPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_SHADOWS", kDisableShadows ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_GEOMETRY_PASS", kDisableGeometryPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_DECAL_PASS", kDisableDecalPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_LIGHTING_PASS", kDisableLightingPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_POINT_LIGHT_PASS", kDisablePointLightPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_COMPOSITION_PASS", kDisableCompositionPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_FORWARD_PASS", kDisableForwardPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_ENVIRONMENT_ALPHA_PASS", kDisableEnvironmentAlphaPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_REFRACTION_PASS", kDisableRefractionPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_WATER_PASS", kDisableWaterPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_POST_ALPHA_UNLIT_PASS", kDisablePostAlphaUnlitPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_PARTICLE_PASS", kDisableParticlePass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_COMPOSE_PASS", kDisableComposePass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_LIGHTING", kDisableLighting ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_SL_PASS", kDisableSLPass ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_ENV_PASS", kDisableEnvPass ? "1" : "0");
	SetEnvDefault("NDEVC_WEB_ADDON_ENABLED", kEnableWebAddon ? "1" : "0");
	SetEnvDefault("NDEVC_USE_LEGACY_DEFERRED_INIT", kUseLegacyDeferredInit ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_FRUSTUM_CULLING", kDisableFrustumCulling ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_FACE_CULLING", kDisableFaceCulling ? "1" : "0");
	SetEnvDefault("NDEVC_FORCE_NON_BATCHED_GEOMETRY", kForceNonBatchedGeometry ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_VIEWPORT", kDisableViewport ? "1" : "0");
	SetEnvDefault("NDEVC_NO_PRESENT_WHEN_VIEWPORT_DISABLED", kNoPresentWhenViewportDisabled ? "1" : "0");
	SetEnvDefault("NDEVC_DISABLE_FRAME_FENCE_WAIT", kDisableFrameFenceWait ? "1" : "0");
	SetEnvDefault("NDEVC_SOURCE_DIR", SOURCE_DIR);
	SetEnvDefault("NDEVC_STARTUP_MAP", "");
	SetEnvDefault("NDEVC_MODELS_ROOT", kModelsRootPath);
	SetEnvDefault("NDEVC_MESHES_ROOT", kMeshesRootPath);
	SetEnvDefault("NDEVC_MAPS_ROOT", kMapsRootPath);
	SetEnvDefault("NDEVC_ANIMS_ROOT", kAnimsRootPath);
	SetEnvDefault("NDEVC_TEXTURES_ROOT", kTexturesRootPath);
	SetEnvDefault("NDEVC_FULLSCREEN_EXCLUSIVE", kFullscreenExclusive ? "1" : "0");
	SetEnvDefault("NDEVC_VSYNC", "0");
	SetEnvDefault("NDEVC_SCENES_ROOT", kScenesRootPath);
	SetEnvDefault("NDEVC_EDITOR_MODE", "1");
	SetEnvDefault("NDEVC_EDITOR_ROUTE_INPUT", kEditorRouteInput ? "1" : "0");
	SetEnvDefault("NDEVC_IMGUI_VIEWPORT_ONLY", kImGuiViewportOnly ? "1" : "0");
	SetEnvDefault("NDEVC_RENDER_MODE", "work");
	NC::LOGGING::Log("[APP] Toggle defaults particlesDisabled=", (kDisableParticles ? 1 : 0),
	                 " animationsDisabled=", (kDisableAnimations ? 1 : 0),
	                 " shadowPassDisabled=", (kDisableShadowPass ? 1 : 0),
	                 " shadowsDisabled=", (kDisableShadows ? 1 : 0),
	                 " geometryPassDisabled=", (kDisableGeometryPass ? 1 : 0),
	                 " decalPassDisabled=", (kDisableDecalPass ? 1 : 0),
	                 " lightingPassDisabled=", (kDisableLightingPass ? 1 : 0),
	                 " pointLightPassDisabled=", (kDisablePointLightPass ? 1 : 0),
	                 " compositionPassDisabled=", (kDisableCompositionPass ? 1 : 0),
	                 " forwardPassDisabled=", (kDisableForwardPass ? 1 : 0),
	                 " envAlphaPassDisabled=", (kDisableEnvironmentAlphaPass ? 1 : 0),
	                 " refractionPassDisabled=", (kDisableRefractionPass ? 1 : 0),
	                 " waterPassDisabled=", (kDisableWaterPass ? 1 : 0),
	                 " postAlphaUnlitPassDisabled=", (kDisablePostAlphaUnlitPass ? 1 : 0),
	                 " particlePassDisabled=", (kDisableParticlePass ? 1 : 0),
	                 " composePassDisabled=", (kDisableComposePass ? 1 : 0),
	                 " webAddon=", (kEnableWebAddon ? 1 : 0),
	                 " editorRouteInput=", (kEditorRouteInput ? 1 : 0),
	                 " imguiViewportOnly=", (kImGuiViewportOnly ? 1 : 0),
	                 " legacyDeferredInit=", (kUseLegacyDeferredInit ? 1 : 0),
	                 " frustumCullingDisabled=", (kDisableFrustumCulling ? 1 : 0),
	                 " faceCullingDisabled=", (kDisableFaceCulling ? 1 : 0),
	                 " forceNonBatchedGeometry=", (kForceNonBatchedGeometry ? 1 : 0),
	                 " frameFenceWaitDisabled=", (kDisableFrameFenceWait ? 1 : 0),
	                 " slPassDisabled=", (kDisableSLPass ? 1 : 0),
	                 " envPassDisabled=", (kDisableEnvPass ? 1 : 0));
	if (!NC::VFS::Instance().IsMounted()) {
		NC::LOGGING::Log("[APP] Path defaults assets=", kAssetRootPath,
		                 " models=", kModelsRootPath,
		                 " meshes=", kMeshesRootPath,
		                 " maps=", kMapsRootPath,
		                 " anims=", kAnimsRootPath,
		                 " textures=", kTexturesRootPath);
	}
}

int main()
{

	try {
		EnableAnsiColors();
		ApplyPackageRuntimeOverrides();
		ApplyRuntimeToggles();

		const Initialization init;
		init.RunMainLoop();


	}catch (std::exception& e) {
		NC::LOGGING::Error("[APP] Fatal: ", e.what());
		return 1;
	}

	NC::LOGGING::Log("[APP] main exit success");
	return EXIT_SUCCESS;
}
