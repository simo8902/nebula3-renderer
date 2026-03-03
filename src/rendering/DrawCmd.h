// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DRAWCMD_H
#define NDEVC_DRAWCMD_H

#include "Rendering/Mesh.h"
#include "Rendering/Interfaces/ITexture.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "glm.hpp"

struct DrawCmd {
	Mesh* mesh = nullptr;
	std::string shdr;
	std::string nodeName;
	std::string modelNodeType;

	glm::mat4 worldMatrix{ 1.0f };
	glm::mat4 rootMatrix{ 1.0f };
	glm::vec4 localBoxMin{ 0.0f };
	glm::vec4 localBoxMax{ 0.0f };
	glm::vec4 position{ 0.0f };
	glm::vec4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	glm::vec4 scale{ 1.0f, 1.0f, 1.0f, 1.0f };
	uint32_t megaVertexOffset = 0;
	uint32_t megaIndexOffset = 0;
	const Node* sourceNode = nullptr;
	mutable glm::vec3 cullWorldCenter{ 0.0f };
	mutable float cullWorldRadius = 0.0f;
	mutable bool cullBoundsValid = false;
	mutable uint64_t cullTransformHash = 0ull;
	mutable glm::mat4 cachedInvWorldMatrix{1.0f};
	mutable uint64_t  invWorldMatrixHash = ~0ull;

	int group = -1;
	NDEVC::Graphics::ITexture* tex[12]{};
	void* instance = nullptr;
	std::vector<uint8_t> groupEnabled;

	float decalScale = 1.0f;
	int cullMode = 0;
	bool isDecal = false;
	bool receivesDecals = false;
	int decalPriority = 0;

	std::unordered_map<std::string, float> shaderParamsFloat;
	std::unordered_map<std::string, int> shaderParamsInt;
	std::unordered_map<std::string, glm::vec4> shaderParamsVec4;
	std::unordered_map<std::string, std::string> shaderParamsTexture;
	std::unordered_map<std::string, float> animatedShaderParamsFloat;

	bool alphaTest = false;
	float alphaCutoff = 0.5f;
	bool hasShaderVarAnimations = false;
	bool hasPotentialTransformAnimation = false;
	bool userDisabled = false;
	bool disabled = false;
	bool costControlDisabled = false; // set by ApplyDistanceCostControl; cleared on aggression restore
	mutable bool frustumCulled = false;
	bool isStatic = true;
	uint32_t gpuMaterialIndex = UINT32_MAX;

	// Pre-cached at load time — eliminates per-frame unordered_map lookups
	bool cachedIsAdditive = false;
	bool cachedHasSpecMap = false;
	bool cachedHasCustomDiffMap = false;
	bool cachedHasVelocity = false;
	int  cachedTwoSided = 0;
	int  cachedIsFlatNormal = 0;
	int  cachedWaterCullMode = 2;
	float cachedIntensity0 = 0.25f;
	float cachedMatEmissiveIntensity = 1.0f;
	float cachedMatSpecularIntensity = 0.0f;
	float cachedMatSpecularPower = 32.0f;
	float cachedScale = 1.0f;
	float cachedBumpScale = 1.0f;
	float cachedAlphaBlendFactor = 1.0f;
	glm::vec2 cachedVelocity{ 0.0f, 0.0f };
	bool shadowFiltered = false;
};
#endif
