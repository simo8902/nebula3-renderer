// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "src/Engine/Core/Initialization.h"
#include "src/ValidationLayer.h"
#include <cstdlib>
#include <exception>
#include <iostream>

static constexpr bool kDisableParticles = false;
static constexpr bool kDisableAnimations = false;
static constexpr bool kEnableWebAddon = false;
static constexpr bool kEditorRouteInput = true;
static constexpr const char* kAssetRootPath = "C:\\drasa_online\\work";
static constexpr const char* kModelsRootPath = "C:\\drasa_online\\work\\models";
static constexpr const char* kMeshesRootPath = "C:\\drasa_online\\work\\meshes";
static constexpr const char* kMapsRootPath = "C:\\drasa_online\\work\\maps";
static constexpr const char* kAnimsRootPath = "C:\\drasa_online\\work\\anims";
static constexpr const char* kTexturesRootPath = "C:\\drasa_online\\work\\textures";

static void ApplyRuntimeToggles() {
#if defined(_WIN32)
	_putenv_s("NDEVC_DISABLE_PARTICLES", kDisableParticles ? "1" : "0");
	_putenv_s("NDEVC_DISABLE_ANIMATIONS", kDisableAnimations ? "1" : "0");
	_putenv_s("NDEVC_WEB_ADDON_ENABLED", kEnableWebAddon ? "1" : "0");
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
	setenv("NDEVC_STARTUP_MAP", kAssetRootPath, 1);
	setenv("NDEVC_MODELS_ROOT", kModelsRootPath, 1);
	setenv("NDEVC_MESHES_ROOT", kMeshesRootPath, 1);
	setenv("NDEVC_MAPS_ROOT", kMapsRootPath, 1);
	setenv("NDEVC_ANIMS_ROOT", kAnimsRootPath, 1);
	setenv("NDEVC_TEXTURES_ROOT", kTexturesRootPath, 1);
	setenv("NDEVC_EDITOR_MODE", "1", 1);
	setenv("NDEVC_EDITOR_ROUTE_INPUT", kEditorRouteInput ? "1" : "0", 1);
#endif
}

int main()
{
	try {
		EnableAnsiColors();
		ApplyRuntimeToggles();

		const Initialization init;
		init.RunMainLoop();

	}catch (std::exception& e) {
		std::cerr << "Fatal: " << e.what() << "\n";
		return 1;
	}

	return EXIT_SUCCESS;
}
