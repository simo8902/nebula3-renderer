// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "src/DeferredRenderer.h"
#include "src/rendering/abstract/IRenderer.h"
#include "src/ShaderManager.h"
#include "src/ValidationLayer.h"
#include <cstdlib>
#include <memory>

static constexpr bool kDisableParticles = false;
static constexpr bool kDisableAnimations = false;
static constexpr bool kEnableWebAddon = false;
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
#endif
}

int main()
{
	try {
		EnableAnsiColors();
		ApplyRuntimeToggles();

		const std::unique_ptr<NDEVC::Graphics::IRenderer> &renderer = std::make_unique<DeferredRenderer>();
		renderer->Initialize();

		/*
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01.n3",
			glm::vec3(126.0f, -1.0f, -66.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/decal_sand_01.n3",
			glm::vec3(129.0f, 0.0f, -66.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01.n3",
			glm::vec3(126.0f, -1.0f, -66.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/decal_grassgreen_01.n3",
			glm::vec3(127.0f, -1.0f, -64.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/decal_sand_01.n3",
			glm::vec3(123.831f, -0.999996f, -67.0106f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01.n3",
			glm::vec3(126.0f, -1.0f, -62.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01.n3",
			glm::vec3(122.0f, -1.0f, -66.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/decal_sand_01.n3",
			glm::vec3(127.288f, -1.0f, -68.6167f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01.n3",
			glm::vec3(122.0f, -1.0f, -62.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/decal_sand_01.n3",
			glm::vec3(122.224f, -1.0f, -68.1599f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/decal_grassgreen_01.n3",
			glm::vec3(122.0f, -1.0f, -68.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01_puddle_02.n3",
			glm::vec3(130.0f, -1.0f, -66.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01_puddle_03.n3",
			glm::vec3(126.0f, -1.0f, -70.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01_puddle_02.n3",
			glm::vec3(122.0f, -1.0f, -70.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01_puddle_02.n3",
			glm::vec3(130.0f, -1.0f, -70.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
		renderer->AppendModel("C:/drasa_online/work/models/t011_kingshill/tile_ground_cobblestone_01.n3",
			glm::vec3(126.0f, -1.0f, -58.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 1.0f));
*/

		renderer->RenderFrame();

	}catch (std::exception& e) {
		std::cerr << "Fatal: " << e.what() << "\n";
		return 1;
	}

	return EXIT_SUCCESS;
}
