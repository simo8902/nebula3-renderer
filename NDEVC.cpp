// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "src/Engine/Core/Initialization.h"
#include "Core/Logger.h"
#include "Rendering/ValidationLayer.h"
#include <cstdlib>
#include <exception>

static constexpr bool kDisableParticles = false;
static constexpr bool kDisableAnimations = false;
static constexpr bool kEnableWebAddon = false;
static constexpr bool kEditorRouteInput = true;
static constexpr bool kUseLegacyDeferredInit = false;
static constexpr bool kDisableFrustumCulling = true;
static constexpr bool kDisableFaceCulling = true;
static constexpr bool kForceNonBatchedGeometry = true;
static constexpr const char* kAssetRootPath = "C:\\drasa_online\\work";
static constexpr const char* kModelsRootPath = "C:\\drasa_online\\work\\models";
static constexpr const char* kMeshesRootPath = "C:\\drasa_online\\work\\meshes";
static constexpr const char* kMapsRootPath = "C:\\drasa_online\\work\\maps";
static constexpr const char* kAnimsRootPath = "C:\\drasa_online\\work\\anims";
static constexpr const char* kTexturesRootPath = "C:\\drasa_online\\work\\textures";

static void ApplyRuntimeToggles() {
	NC::LOGGING::Log("[APP] ApplyRuntimeToggles begin");
#if defined(_WIN32)
	_putenv_s("NDEVC_DISABLE_PARTICLES", kDisableParticles ? "1" : "0");
	_putenv_s("NDEVC_DISABLE_ANIMATIONS", kDisableAnimations ? "1" : "0");
	_putenv_s("NDEVC_WEB_ADDON_ENABLED", kEnableWebAddon ? "1" : "0");
	_putenv_s("NDEVC_USE_LEGACY_DEFERRED_INIT", kUseLegacyDeferredInit ? "1" : "0");
	_putenv_s("NDEVC_DISABLE_FRUSTUM_CULLING", kDisableFrustumCulling ? "1" : "0");
	_putenv_s("NDEVC_DISABLE_FACE_CULLING", kDisableFaceCulling ? "1" : "0");
	_putenv_s("NDEVC_FORCE_NON_BATCHED_GEOMETRY", kForceNonBatchedGeometry ? "1" : "0");
	_putenv_s("NDEVC_STARTUP_MAP", kAssetRootPath);
	_putenv_s("NDEVC_MODELS_ROOT", kModelsRootPath);
	_putenv_s("NDEVC_MESHES_ROOT", kMeshesRootPath);
	_putenv_s("NDEVC_MAPS_ROOT", kMapsRootPath);
	_putenv_s("NDEVC_ANIMS_ROOT", kAnimsRootPath);
	_putenv_s("NDEVC_TEXTURES_ROOT", kTexturesRootPath);
	_putenv_s("NDEVC_EDITOR_MODE", "1");
	_putenv_s("NDEVC_EDITOR_ROUTE_INPUT", kEditorRouteInput ? "1" : "0");
#else
	setenv("NDEVC_DISABLE_PARTICLES", kDisableParticles ? "1" : "0", 1);
	setenv("NDEVC_DISABLE_ANIMATIONS", kDisableAnimations ? "1" : "0", 1);
	setenv("NDEVC_WEB_ADDON_ENABLED", kEnableWebAddon ? "1" : "0", 1);
	setenv("NDEVC_USE_LEGACY_DEFERRED_INIT", kUseLegacyDeferredInit ? "1" : "0", 1);
	setenv("NDEVC_DISABLE_FRUSTUM_CULLING", kDisableFrustumCulling ? "1" : "0", 1);
	setenv("NDEVC_DISABLE_FACE_CULLING", kDisableFaceCulling ? "1" : "0", 1);
	setenv("NDEVC_FORCE_NON_BATCHED_GEOMETRY", kForceNonBatchedGeometry ? "1" : "0", 1);
	setenv("NDEVC_STARTUP_MAP", kAssetRootPath, 1);
	setenv("NDEVC_MODELS_ROOT", kModelsRootPath, 1);
	setenv("NDEVC_MESHES_ROOT", kMeshesRootPath, 1);
	setenv("NDEVC_MAPS_ROOT", kMapsRootPath, 1);
	setenv("NDEVC_ANIMS_ROOT", kAnimsRootPath, 1);
	setenv("NDEVC_TEXTURES_ROOT", kTexturesRootPath, 1);
	setenv("NDEVC_EDITOR_MODE", "1", 1);
	setenv("NDEVC_EDITOR_ROUTE_INPUT", kEditorRouteInput ? "1" : "0", 1);
#endif
	NC::LOGGING::Log("[APP] Toggles particlesDisabled=", (kDisableParticles ? 1 : 0),
	                 " animationsDisabled=", (kDisableAnimations ? 1 : 0),
	                 " webAddon=", (kEnableWebAddon ? 1 : 0),
	                 " editorRouteInput=", (kEditorRouteInput ? 1 : 0),
	                 " legacyDeferredInit=", (kUseLegacyDeferredInit ? 1 : 0),
	                 " frustumCullingDisabled=", (kDisableFrustumCulling ? 1 : 0),
	                 " faceCullingDisabled=", (kDisableFaceCulling ? 1 : 0),
	                 " forceNonBatchedGeometry=", (kForceNonBatchedGeometry ? 1 : 0));
	NC::LOGGING::Log("[APP] Paths assets=", kAssetRootPath,
	                 " models=", kModelsRootPath,
	                 " meshes=", kMeshesRootPath,
	                 " maps=", kMapsRootPath,
	                 " anims=", kAnimsRootPath,
	                 " textures=", kTexturesRootPath);
}

int main()
{
	try {
		NC::LOGGING::Log("[APP] main start");
		EnableAnsiColors();
		ApplyRuntimeToggles();

		const Initialization init;
		init.RunMainLoop();
		NC::LOGGING::Log("[APP] main loop completed");

	}catch (std::exception& e) {
		NC::LOGGING::Error("[APP] Fatal: ", e.what());
		return 1;
	}

	NC::LOGGING::Log("[APP] main exit success");
	return EXIT_SUCCESS;
}
