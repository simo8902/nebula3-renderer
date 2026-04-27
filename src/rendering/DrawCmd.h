// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DRAWCMD_H
#define NDEVC_DRAWCMD_H

#include "Rendering/Mesh.h"
#include "Rendering/Interfaces/ITexture.h"
#include <cstdint>
#include <string>
#include "glm.hpp"

inline uint32_t DrawCmdHash(const char* s, size_t len) noexcept {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) { h ^= static_cast<uint8_t>(s[i]); h *= 16777619u; }
    return h;
}
inline uint32_t DrawCmdHash(const std::string& s) noexcept { return DrawCmdHash(s.data(), s.size()); }

enum class DrawBucket : uint8_t {
    Solid, AlphaTest, Decal, Water, Refraction, PostAlphaUnlit, SimpleLayer, Environment, EnvironmentAlpha
};

struct DrawCmd {
	Mesh* mesh = nullptr;
	uint32_t shaderHash   = 0;
	uint32_t nodeNameHash = 0;
	uint32_t nodeTypeHash = 0;

	glm::mat4 worldMatrix{ 1.0f };
	glm::mat4 rootMatrix{ 1.0f };
	glm::vec4 localBoxMin{ 0.0f };
	glm::vec4 localBoxMax{ 0.0f };
	glm::vec4 position{ 0.0f };
	glm::vec4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	glm::vec4 scale{ 1.0f, 1.0f, 1.0f, 1.0f };
	uint32_t megaVertexOffset = 0;
	uint32_t megaIndexOffset  = 0;
	const Node* sourceNode = nullptr;
	mutable glm::vec3 cullWorldCenter{ 0.0f };
	mutable float     cullWorldRadius   = 0.0f;
	mutable bool      cullBoundsValid   = false;
	mutable uint64_t  cullTransformHash = 0ull;
	mutable glm::mat4 cachedInvWorldMatrix{ 1.0f };
	mutable uint64_t  invWorldMatrixHash = ~0ull;

	int  group = -1;
	NDEVC::Graphics::ITexture* tex[12]{};
	void* instance = nullptr;
	uint64_t groupEnabledMask = ~0ull;

	float decalScale     = 1.0f;
	int   cullMode       = 0;
	bool  isDecal        = false;
	bool  receivesDecals = false;
	int   decalPriority  = 0;

	bool     alphaTest                      = false;
	float    alphaCutoff                    = 0.5f;
	bool     hasShaderVarAnimations         = false;
	bool     hasPotentialTransformAnimation = false;
	bool     userDisabled                   = false;
	bool     disabled                       = false;
	bool     costControlDisabled            = false;
	bool     isStatic                       = true;
	uint32_t gpuMaterialIndex               = UINT32_MAX;
	DrawBucket bucket                       = DrawBucket::Solid;
	glm::mat4 cachedModelSpaceTransform{ 1.0f };

	// Pre-cached at load time — eliminates per-frame shader param lookups
	bool     cachedIsAdditive           = false;
	bool     cachedHasSpecMap           = false;
	bool     cachedHasCustomDiffMap     = false;
	bool     cachedHasVelocity          = false;
	int      cachedTwoSided             = 0;
	int      cachedIsFlatNormal         = 0;
	int      cachedWaterCullMode        = 2;
	float    cachedIntensity0           = 0.25f;
	float    cachedMatEmissiveIntensity = 1.0f;
	float    cachedMatSpecularIntensity = 0.0f;
	float    cachedMatSpecularPower     = 32.0f;
	float    cachedScale                = 1.0f;
	float    cachedBumpScale            = 1.0f;
	float    cachedAlphaBlendFactor     = 1.0f;
	float    cachedMayaAnimableAlpha    = 1.0f;
	float    cachedEncodefactor         = 1.0f;
	glm::vec2 cachedVelocity{ 0.0f, 0.0f };
	glm::vec4 cachedCustomColor2{ 0.0f, 0.0f, 0.0f, 0.0f };
	glm::vec4 cachedTintingColour{ 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec4 cachedHighlightColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec4 cachedLuminance{ 0.299f, 0.587f, 0.114f, 1.0f };
	bool shadowFiltered = false;
};
#endif
