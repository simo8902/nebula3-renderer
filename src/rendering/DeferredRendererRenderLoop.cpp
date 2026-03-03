// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"
#include "Rendering/DeferredRendererAnimation.h"
#include "Rendering/SelectionRaycaster.h"
#include "Assets/Parser.h"
#include "Rendering/GLStateDebug.h"
#include "Animation/AnimationSystem.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "Rendering/OpenGL/OpenGLDevice.h"
#include "Platform/GLFWPlatform.h"
#include "Rendering/OpenGL/OpenGLShaderManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <thread>

#define GLM_ENABLE_EXPERIMENTAL
#include "Rendering/DrawBatchSystem.h"
#include "Rendering/MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"
#include "Assets/Model/ModelServer.h"
#include "Assets/Servers/MeshServer.h"
#include "Assets/Servers/TextureServer.h"
#include "Assets/Map/MapHeader.h"
#include "Assets/Particles/ParticleServer.h"
#include "Core/Logger.h"
#include "gtx/norm.hpp"

namespace {
bool ReadEnvToggle(const char* name) {
    if (!name || !name[0]) return false;
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
}

uint64_t ReadEnvUint64(const char* name, uint64_t fallbackValue) {
    if (!name || !name[0]) return fallbackValue;
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return fallbackValue;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    const bool ok = (end != value && end && *end == '\0');
    std::free(value);
    return ok ? static_cast<uint64_t>(parsed) : fallbackValue;
#else
    const char* value = std::getenv(name);
    if (!value || !value[0]) return fallbackValue;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    return (end != value && end && *end == '\0') ? static_cast<uint64_t>(parsed) : fallbackValue;
#endif
}

bool ParticlesDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_PARTICLES");
    return disabled;
}

bool AnimationsDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ANIMATIONS");
    return disabled;
}

bool FrustumCullingDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_FRUSTUM_CULLING");
    return disabled;
}

bool FaceCullingDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_FACE_CULLING");
    return disabled;
}

bool ForceNonBatchedGeometry() {
    static const bool enabled = ReadEnvToggle("NDEVC_FORCE_NON_BATCHED_GEOMETRY");
    return enabled;
}

bool NoPresentWhenViewportDisabled() {
    static const bool enabled = ReadEnvToggle("NDEVC_NO_PRESENT_WHEN_VIEWPORT_DISABLED");
    return enabled;
}

bool FrameFenceWaitEnabled() {
    static const bool enabled = !ReadEnvToggle("NDEVC_DISABLE_FRAME_FENCE_WAIT");
    return enabled;
}

uint64_t FrameFenceWaitBudgetNs() {
    static const uint64_t budgetNs = ReadEnvUint64("NDEVC_FRAME_FENCE_WAIT_NS", 1000000ull);
    return budgetNs;
}


uint64_t HashMatrix4(const glm::mat4& matrix) {
    uint64_t hash = 0x6f3d9b6f3d9b6f3dull;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            uint32_t bits = 0;
            const float value = matrix[c][r];
            std::memcpy(&bits, &value, sizeof(bits));
            hash ^= (static_cast<uint64_t>(bits) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2));
        }
    }
    return hash;
}

bool PassesFrustumCulling(const DrawCmd& dc,
                          const Camera::Frustum& frustum,
                          bool disableFrustumCulling,
                          bool keepStaticVisible) {
    if (disableFrustumCulling) return true;
    if (keepStaticVisible && dc.isStatic) return true;

    glm::vec3 worldCenter(0.0f);
    float radius = 0.001f;
    // Fix #1: static objects never change their world matrix, skip hashing once bounds are cached.
    if (dc.cullBoundsValid && dc.isStatic) {
        worldCenter = dc.cullWorldCenter;
        radius = dc.cullWorldRadius;
    } else {
        const uint64_t transformHash = HashMatrix4(dc.worldMatrix);
        if (dc.cullBoundsValid && dc.cullTransformHash == transformHash) {
            worldCenter = dc.cullWorldCenter;
            radius = dc.cullWorldRadius;
        } else {
        const glm::vec3 localMin(dc.localBoxMin);
        const glm::vec3 localMax(dc.localBoxMax);
        const glm::vec3 localExtent = localMax - localMin;
        const bool hasBounds =
            std::isfinite(localMin.x) && std::isfinite(localMin.y) && std::isfinite(localMin.z) &&
            std::isfinite(localMax.x) && std::isfinite(localMax.y) && std::isfinite(localMax.z) &&
            (std::abs(localExtent.x) > 1e-5f || std::abs(localExtent.y) > 1e-5f || std::abs(localExtent.z) > 1e-5f);
        if (!hasBounds) return true;

        const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
        worldCenter = glm::vec3(dc.worldMatrix * glm::vec4(localCenter, 1.0f));
        if (!std::isfinite(worldCenter.x) || !std::isfinite(worldCenter.y) || !std::isfinite(worldCenter.z)) return true;

        const float localRadius = glm::length(localExtent) * 0.5f;
        const float sx = glm::length(glm::vec3(dc.worldMatrix[0]));
        const float sy = glm::length(glm::vec3(dc.worldMatrix[1]));
        const float sz = glm::length(glm::vec3(dc.worldMatrix[2]));
        const float maxScale = std::max(sx, std::max(sy, sz));
        if (!std::isfinite(localRadius) || !std::isfinite(maxScale)) return true;
        radius = std::max(localRadius * maxScale, 0.001f);

        dc.cullWorldCenter = worldCenter;
        dc.cullWorldRadius = radius;
        dc.cullBoundsValid = true;
        dc.cullTransformHash = transformHash;
        }
    }

    for (int i = 0; i < 6; ++i) {
        const float dist = glm::dot(frustum.planes[i].normal, worldCenter) + frustum.planes[i].d;
        if (dist < -radius * 1.2f) return false;
    }
    return true;
}

glm::vec3 GetDrawSortCenterWS(const DrawCmd& dc) {
    if (dc.cullBoundsValid &&
        std::isfinite(dc.cullWorldCenter.x) &&
        std::isfinite(dc.cullWorldCenter.y) &&
        std::isfinite(dc.cullWorldCenter.z)) {
        return dc.cullWorldCenter;
    }
    const glm::vec3 center(dc.worldMatrix[3][0], dc.worldMatrix[3][1], dc.worldMatrix[3][2]);
    if (std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z)) {
        return center;
    }
    return glm::vec3(0.0f);
}

float ComputeTransparentSortDepth(const DrawCmd& dc,
                                  const glm::vec3& eyePos,
                                  const glm::vec3& viewDir) {
    const glm::vec3 worldCenter = GetDrawSortCenterWS(dc);
    return glm::dot(worldCenter - eyePos, viewDir);
}

void SortTransparentDrawsBackToFront(std::vector<const DrawCmd*>& draws,
                                     const glm::vec3& eyePos,
                                     const glm::vec3& viewDirRaw) {
    if (draws.size() < 2) return;

    glm::vec3 viewDir = viewDirRaw;
    const float dirLen2 = glm::dot(viewDir, viewDir);
    if (!std::isfinite(dirLen2) || dirLen2 <= 1e-8f) {
        viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        viewDir = glm::normalize(viewDir);
    }

    std::stable_sort(draws.begin(), draws.end(), [&](const DrawCmd* a, const DrawCmd* b) {
        if (!a || !b) return a < b;
        const float depthA = ComputeTransparentSortDepth(*a, eyePos, viewDir);
        const float depthB = ComputeTransparentSortDepth(*b, eyePos, viewDir);
        if (std::fabs(depthA - depthB) > 1e-4f) {
            return depthA > depthB;
        }
        if (a->nodeName != b->nodeName) return a->nodeName < b->nodeName;
        if (a->shdr != b->shdr) return a->shdr < b->shdr;
        if (a->group != b->group) return a->group < b->group;
        return a < b;
    });
}

const char* EventModeText(bool enabled) {
    return enabled ? "event-filtered" : "all";
}

void DebugPrintNearestInstances(const MapData* map, const glm::vec3& target, size_t maxResults) {
    if (!map) return;

    struct NearestInstance {
        size_t index;
        float distSq;
    };

    std::vector<NearestInstance> nearest;
    nearest.reserve(map->instances.size());

    for (size_t i = 0; i < map->instances.size(); ++i) {
        const auto& inst = map->instances[i];
        const glm::vec3 p(inst.pos.x, inst.pos.y, inst.pos.z);
        const glm::vec3 d = p - target;
        nearest.push_back({i, glm::dot(d, d)});
    }

    if (nearest.empty()) {
        std::cout << "[SEARCH] No instances in map\n";
        return;
    }

    const size_t resultCount = std::min(maxResults, nearest.size());
    std::partial_sort(nearest.begin(), nearest.begin() + resultCount, nearest.end(),
        [](const NearestInstance& a, const NearestInstance& b) { return a.distSq < b.distSq; });

    std::cout << "[SEARCH] nearest " << resultCount << "/" << nearest.size()
              << " to pos=(" << target.x << "," << target.y << "," << target.z << ")\n";
    for (size_t n = 0; n < resultCount; ++n) {
        const auto& entry = nearest[n];
        const auto& inst = map->instances[entry.index];
        const glm::vec3 p(inst.pos.x, inst.pos.y, inst.pos.z);

        std::string tmpl = "?";
        if (inst.templ_index >= 0 && inst.templ_index < static_cast<int>(map->templates.size())) {
            const auto& t = map->templates[inst.templ_index];
            if (t.gfx_res_id < map->string_table.size()) {
                tmpl = map->string_table[t.gfx_res_id];
            }
        }

        std::cout << std::setw(5) << entry.index << "  "
                  << "d=" << std::sqrt(entry.distSq) << " "
                  << "T=(" << p.x << "," << p.y << "," << p.z << ") "
                  << "tmpl=" << tmpl
                  << " grp=" << inst.group_index
                  << " evt=" << inst.index_to_mapping
                  << "\n";
    }
}

GLuint GetFrameGraphTexture(const std::unique_ptr<NDEVC::Graphics::FrameGraph>& graph, const char* name) {
    if (!graph || !name) return 0;
    auto texture = graph->getTextureInterface(name);
    return texture ? *(GLuint*)texture->GetNativeHandle() : 0;
}
}

static const glm::vec3 kLightDirToSun = glm::normalize(glm::vec3(0.0f, 0.5f, -0.8f));

extern bool gEnableGLErrorChecking;

using UniformID = NDEVC::Graphics::IShader::UniformID;
static constexpr UniformID U_PROJECTION = NDEVC::Graphics::IShader::MakeUniformID("projection");
static constexpr UniformID U_VIEW = NDEVC::Graphics::IShader::MakeUniformID("view");
static constexpr UniformID U_INV_PROJECTION = NDEVC::Graphics::IShader::MakeUniformID("invProjection");
static constexpr UniformID U_MODEL = NDEVC::Graphics::IShader::MakeUniformID("model");
static constexpr UniformID U_TEXTURE_TRANSFORM0 = NDEVC::Graphics::IShader::MakeUniformID("textureTransform0");
static constexpr UniformID U_MAT_EMISSIVE_INTENSITY = NDEVC::Graphics::IShader::MakeUniformID("MatEmissiveIntensity");
static constexpr UniformID U_MAT_SPECULAR_INTENSITY = NDEVC::Graphics::IShader::MakeUniformID("MatSpecularIntensity");
static constexpr UniformID U_MAT_SPECULAR_POWER = NDEVC::Graphics::IShader::MakeUniformID("MatSpecularPower");
static constexpr UniformID U_USE_SKINNING = NDEVC::Graphics::IShader::MakeUniformID("UseSkinning");
static constexpr UniformID U_USE_INSTANCING = NDEVC::Graphics::IShader::MakeUniformID("UseInstancing");
static constexpr UniformID U_ALPHA_TEST = NDEVC::Graphics::IShader::MakeUniformID("alphaTest");
static constexpr UniformID U_ALPHA_CUTOFF = NDEVC::Graphics::IShader::MakeUniformID("alphaCutoff");
static constexpr UniformID U_DIFF_MAP0 = NDEVC::Graphics::IShader::MakeUniformID("DiffMap0");
static constexpr UniformID U_SPEC_MAP0 = NDEVC::Graphics::IShader::MakeUniformID("SpecMap0");
static constexpr UniformID U_BUMP_MAP0 = NDEVC::Graphics::IShader::MakeUniformID("BumpMap0");
static constexpr UniformID U_EMSV_MAP0 = NDEVC::Graphics::IShader::MakeUniformID("EmsvMap0");
static constexpr UniformID U_TWO_SIDED = NDEVC::Graphics::IShader::MakeUniformID("twoSided");
static constexpr UniformID U_IS_FLAT_NORMAL = NDEVC::Graphics::IShader::MakeUniformID("isFlatNormal");
static constexpr UniformID U_RECEIVES_DECALS = NDEVC::Graphics::IShader::MakeUniformID("ReceivesDecals");
static constexpr UniformID U_CAMERA_POS = NDEVC::Graphics::IShader::MakeUniformID("CameraPos");
static constexpr UniformID U_LIGHT_DIR_WS = NDEVC::Graphics::IShader::MakeUniformID("LightDirWS");
static constexpr UniformID U_LIGHT_COLOR = NDEVC::Graphics::IShader::MakeUniformID("LightColor");
static constexpr UniformID U_AMBIENT_COLOR = NDEVC::Graphics::IShader::MakeUniformID("AmbientColor");
static constexpr UniformID U_GNORMAL_DEPTH_PACKED = NDEVC::Graphics::IShader::MakeUniformID("gNormalDepthPacked");
static constexpr UniformID U_GPOSITION_WS = NDEVC::Graphics::IShader::MakeUniformID("gPositionWS");
static constexpr UniformID U_SHADOW_MAP_CASCADE0 = NDEVC::Graphics::IShader::MakeUniformID("shadowMapCascade0");
static constexpr UniformID U_SHADOW_MAP_CASCADE1 = NDEVC::Graphics::IShader::MakeUniformID("shadowMapCascade1");
static constexpr UniformID U_SHADOW_MAP_CASCADE2 = NDEVC::Graphics::IShader::MakeUniformID("shadowMapCascade2");
static constexpr UniformID U_SHADOW_MAP_CASCADE3 = NDEVC::Graphics::IShader::MakeUniformID("shadowMapCascade3");
static constexpr UniformID U_NUM_CASCADES = NDEVC::Graphics::IShader::MakeUniformID("numCascades");
static constexpr UniformID U_SCREEN_SIZE = NDEVC::Graphics::IShader::MakeUniformID("screenSize");
static constexpr UniformID U_MVP = NDEVC::Graphics::IShader::MakeUniformID("mvp");
static constexpr UniformID U_LIGHT_POS_RANGE = NDEVC::Graphics::IShader::MakeUniformID("lightPosRange");
static constexpr UniformID U_LIGHT_COLOR_IN = NDEVC::Graphics::IShader::MakeUniformID("lightColorIn");
static constexpr UniformID U_GALBEDO_SPEC = NDEVC::Graphics::IShader::MakeUniformID("gAlbedoSpec");
static constexpr UniformID U_LIGHT_BUFFER_TEX = NDEVC::Graphics::IShader::MakeUniformID("lightBufferTex");
static constexpr UniformID U_GEMISSIVE_TEX = NDEVC::Graphics::IShader::MakeUniformID("gEmissiveTex");
static constexpr UniformID U_USE_INSTANCED_POINT_LIGHTS = NDEVC::Graphics::IShader::MakeUniformID("UseInstancedPointLights");
static bool IsShadowPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_SHADOW_PASS");
    return disabled;
}

static bool IsShadowsDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_SHADOWS") || IsShadowPassDisabled();
    return disabled;
}

static bool IsLightingDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_LIGHTING");
    return disabled;
}

static bool IsGeometryPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_GEOMETRY_PASS");
    return disabled;
}

static bool IsLightingPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_LIGHTING_PASS");
    return disabled;
}

static bool IsPointLightPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_POINT_LIGHT_PASS");
    return disabled;
}

static bool IsCompositionPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_COMPOSITION_PASS");
    return disabled;
}

static bool IsDecalPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_DECAL_PASS");
    return disabled;
}

static bool IsForwardPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_FORWARD_PASS");
    return disabled;
}

static bool IsComposePassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_COMPOSE_PASS");
    return disabled;
}

static bool IsEnvironmentAlphaPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ENVIRONMENT_ALPHA_PASS");
    return disabled;
}

static bool IsWaterPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_WATER_PASS");
    return disabled;
}

static bool IsPostAlphaUnlitPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_POST_ALPHA_UNLIT_PASS");
    return disabled;
}

static bool IsParticlePassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_PARTICLE_PASS");
    return disabled;
}

static bool IsAlphaDepthPrepassEnabled() {
    static const bool enabled = []() {
        const bool forceDisable = ReadEnvToggle("NDEVC_DISABLE_ALPHA_DEPTH_PREPASS");
        const bool explicitEnable = ReadEnvToggle("NDEVC_ENABLE_ALPHA_DEPTH_PREPASS");
        if (forceDisable) return false;
        if (explicitEnable) return true;
        return true;
    }();
    return enabled;
}

#define kDisableShadowPass (IsShadowPassDisabled())
#define kDisableShadows (IsShadowsDisabled())
#define kDisableLighting (IsLightingDisabled())
#define kDisableGeometryPass (IsGeometryPassDisabled())
#define kDisableLightingPass (IsLightingPassDisabled())
#define kDisablePointLightPass (IsPointLightPassDisabled())
#define kDisableCompositionPass (IsCompositionPassDisabled())
#define kDisableDecalPass (IsDecalPassDisabled())
#define kDisableForwardPass (IsForwardPassDisabled())
#define kDisableComposePass (IsComposePassDisabled())
#define kDisableEnvironmentAlphaPass (IsEnvironmentAlphaPassDisabled())
#define kDisableWaterPass (IsWaterPassDisabled())
#define kDisablePostAlphaUnlitPass (IsPostAlphaUnlitPassDisabled())
#define kDisableParticlePass (IsParticlePassDisabled())
#define kEnableAlphaDepthPrepass (IsAlphaDepthPrepassEnabled())
static constexpr bool kDisableFog = false;
static constexpr bool kDisableViewDependentSpecular = false;
static constexpr bool kDisableViewDependentReflections = false;
static constexpr bool kForceEnvironmentThroughStandardGeometryPath = true;
static constexpr bool kLogGroundDecalReceiveSolidDiffuse = false;
static constexpr bool kParticleEmitterTransformsAreStatic = false;
static bool IsRefractionPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_REFRACTION_PASS");
    return disabled;
}

#define kDisableRefractionPass (IsRefractionPassDisabled())

static bool IsSLPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_SL_PASS");
    return disabled;
}
static bool IsEnvPassDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ENV_PASS");
    return disabled;
}
#define kDisableSLPass (IsSLPassDisabled())
#define kDisableEnvPass (IsEnvPassDisabled())
static const bool kLogShaderCompat = ReadEnvToggle("NDEVC_LOG_SHADER_COMPAT");
static const bool kForceNonInstancedNormalMatrixPath =
    ReadEnvToggle("NDEVC_FORCE_NON_INSTANCED_NORMAL_MATRIX");

static void checkGLError(const char* label) {
    if (!gEnableGLErrorChecking) return;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL Error at " << label << ": 0x" << std::hex << err << std::dec;
        switch(err) {
            case GL_INVALID_ENUM:                  std::cerr << " (INVALID_ENUM)"; break;
            case GL_INVALID_VALUE:                 std::cerr << " (INVALID_VALUE)"; break;
            case GL_INVALID_OPERATION:             std::cerr << " (INVALID_OPERATION)"; break;
            case GL_OUT_OF_MEMORY:                 std::cerr << " (OUT_OF_MEMORY)"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: std::cerr << " (INVALID_FRAMEBUFFER_OPERATION)"; break;
        }
        std::cerr << "\n";
    }
}

static GLuint CompileRuntimeShader(GLenum type, const char* source, const char* label) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(logLen, 1)));
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "[GEOMETRY][Compat] " << label << " compile failed: " << log.data() << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint GetPackedDeferredCompatProgram() {
    static GLuint program = 0;
    static bool attempted = false;
    if (program != 0) return program;
    if (attempted) return 0;
    attempted = true;

    static const char* kVS = R"(#version 460 core
out vec2 TexCoords;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    TexCoords = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

    static const char* kFS = R"(#version 460 core
in vec2 TexCoords;
layout(location = 0) out vec4 outPositionVS;
layout(location = 1) out vec4 outNormalDepthPacked;
uniform sampler2D gPackedNormalDepth;
uniform sampler2D gPositionWS;
uniform sampler2D gBaseNormalDepth;
uniform mat4 view;
uniform mat4 invView;
uniform mat4 invProjection;
uniform mat3 invViewRot;

vec3 decodeSphereNormal(vec2 enc) {
    const float k = 0.281262308;
    vec2 f = (enc - 0.5) / k;
    float f2 = dot(f, f);
    float denom = max(1.0 + f2, 1e-6);
    vec3 n;
    n.xy = (2.0 * f) / denom;
    n.z = (1.0 - f2) / denom;
    return normalize(n);
}

float decodeViewDistance(vec2 packedDepth) {
    float packedDepthValue = packedDepth.x * 256.0 + packedDepth.y;
    return packedDepthValue * 256.0;
}

vec3 reconstructViewPos(vec2 uv, float viewDistance) {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipFar = vec4(ndc, 1.0, 1.0);
    vec4 viewFar = invProjection * clipFar;
    vec3 dir = normalize(viewFar.xyz / max(abs(viewFar.w), 1e-6));
    return dir * viewDistance;
}

void main() {
    vec4 packedSample = texture(gPackedNormalDepth, TexCoords);
    vec4 baseNormal = texture(gBaseNormalDepth, TexCoords);
    bool hasPacked = dot(packedSample.xy, packedSample.xy) >= 1e-6;
    bool hasBase = dot(baseNormal.xyz, baseNormal.xyz) >= 1e-6;
    if (!hasPacked && !hasBase) {
        discard;
    }

    vec3 worldPos = texture(gPositionWS, TexCoords).xyz;
    bool hasWorldPos = dot(worldPos, worldPos) >= 1e-10;
    float viewDistance = decodeViewDistance(packedSample.zw);
    bool hasPackedDistance = viewDistance > 1e-6;
    vec3 viewPos = hasPackedDistance
        ? reconstructViewPos(TexCoords, viewDistance)
        : (view * vec4(worldPos, 1.0)).xyz;
    if (!hasWorldPos) {
        worldPos = (invView * vec4(viewPos, 1.0)).xyz;
    }

    vec3 worldNormal;
    if (hasPacked) {
        vec3 viewNormal = decodeSphereNormal(packedSample.xy);
        worldNormal = normalize(invViewRot * viewNormal);
    } else {
        worldNormal = normalize(baseNormal.xyz * 2.0 - 1.0);
    }

    outPositionVS = vec4(viewPos, 1.0);
    outNormalDepthPacked = vec4(worldNormal * 0.5 + 0.5, 1.0);
}
)";

    GLuint vs = CompileRuntimeShader(GL_VERTEX_SHADER, kVS, "packed-compat VS");
    GLuint fs = CompileRuntimeShader(GL_FRAGMENT_SHADER, kFS, "packed-compat FS");
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint logLen = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(logLen, 1)));
        glGetProgramInfoLog(p, logLen, nullptr, log.data());
        std::cerr << "[GEOMETRY][Compat] program link failed: " << log.data() << "\n";
        glDeleteProgram(p);
        return 0;
    }

    program = p;
    return program;
}

// ---------------------------------------------------------------------------
// setupRenderPasses -- everything that was before the while-loop in initLOOP
// ---------------------------------------------------------------------------
void DeferredRenderer::setupRenderPasses()
{
    if (!scenePrepared) {
        InvalidateSelection();
        if (!scene_.currentMap) {
            std::cerr << "map load FAILED (scene_.currentMap is null)\n";
        } else {
            if (optRenderLOG) std::cout << "Map loaded: " << scene_.currentMap->instances.size() << " instances\n";
            scene_.LoadMapInstances(scene_.currentMap);
            std::cout << "[ANIM] initialized clips=" << gClips.size() << "\n";
        }

        constexpr bool ENABLE_DRAW_QUEUE_LOGGING = false;

        if (ENABLE_DRAW_QUEUE_LOGGING) {
            auto logDrawQueue = [](const char* name, const std::vector<DrawCmd>& queue) {
                std::cout << "\n========== " << name << " ========== (" << queue.size() << " objects)\n";
                int idx = 0;
                for (const auto& dc : queue) {
                    std::cout << "  [" << idx++ << "] "
                              << "Node: " << dc.nodeName
                              << " | Shader: " << dc.shdr
                              << " | Mesh: " << (dc.mesh ? "YES" : "NO")
                              << " | Group: " << dc.group;
                    if (dc.mesh) {
                        std::cout << " | Groups: " << dc.mesh->groups.size()
                                  << " | Verts: " << dc.mesh->verts.size();
                    }
                    std::cout << " | AlphaTest: " << (dc.alphaTest ? "YES" : "NO");
                    std::cout << "\n";
                }
            };

            std::cout << "\n" << std::string(80, '=') << "\n";
            std::cout << "DRAW QUEUE SUMMARY\n";
            std::cout << std::string(80, '=') << "\n";
            logDrawQueue("SOLID DRAWS", solidDraws);
            logDrawQueue("ALPHA TEST DRAWS", alphaTestDraws);
            std::cout << std::string(80, '=') << "\n\n";
        }

        MeshServer::instance().buildMegaBuffer();

        auto setMegaOffsets = [](std::vector<DrawCmd>& draws) {
            for (auto& dc : draws) {
                if (!dc.mesh) continue;
                dc.megaVertexOffset = dc.mesh->megaVertexOffset;
                dc.megaIndexOffset = dc.mesh->megaIndexOffset;
            }
        };

        setMegaOffsets(solidDraws);
        setMegaOffsets(simpleLayerDraws);
        setMegaOffsets(alphaTestDraws);
        setMegaOffsets(decalDraws);
        setMegaOffsets(waterDraws);
        setMegaOffsets(refractionDraws);
        setMegaOffsets(environmentDraws);
        setMegaOffsets(environmentAlphaDraws);
        setMegaOffsets(postAlphaUnlitDraws);

        ApplyDisabledDrawFlags();

        solidBatchSystem_.init(solidDraws);
        alphaTestBatchSystem_.init(alphaTestDraws);
        environmentBatchSystem_.init(environmentDraws);
        environmentAlphaBatchSystem_.init(environmentAlphaDraws);
        decalBatchSystem_.init(decalDraws);

        rebuildAnimatedDrawLists();
        scenePrepared = true;
    }

    if (optRenderLOG) std::cout << "[BEFORE LOOP] sceneObjs: " << solidDraws.size() << " environmentObjs: " << environmentDraws.size()
     << " environmentAlphaObjs: " << environmentAlphaDraws.size() << " particleObjs: " << scene_.particleNodes.size()
     << " decalDraws: " << decalDraws.size() << " simpleLayerDraws: " << simpleLayerDraws.size() << " alphaTestDraws: " << alphaTestDraws.size()
    << " refractionDraws: " << refractionDraws.size() << " postAlphaUnlitDraws: " << postAlphaUnlitDraws.size() << " waterDraws: " << waterDraws.size()
        << std::endl;

    
    // bindDrawTextures and renderMeshDraw are now standalone member functions
    // defined at the bottom of this file.

    using namespace NDEVC::Graphics;

    shadowGraph = std::make_unique<FrameGraph>(device_.get(), SHADOW_WIDTH, SHADOW_HEIGHT);

    geometryGraph = std::make_unique<FrameGraph>(device_.get(), width, height);
    const bool useCompactGBuffer = !ReadEnvToggle("NDEVC_DISABLE_COMPACT_GBUFFER");
    NC::LOGGING::Log("[GBuffer] Compact mode=", useCompactGBuffer ? "ON" : "OFF",
                     " (disable via NDEVC_DISABLE_COMPACT_GBUFFER=1)");
    geometryGraph->declareResource("gPositionVS", useCompactGBuffer ? Format::RGBA16F : Format::RGBA32F);
    geometryGraph->declareResource("gNormalDepthPacked", Format::RGBA16F);
    geometryGraph->declareResource("gNormalDepthCompat", Format::RGBA16F);
    geometryGraph->declareResource("gNormalDepthEncoded", Format::RGBA16F);
    geometryGraph->declareResource("gAlbedoSpec", Format::RGBA8_UNORM);
    geometryGraph->declareResource("gPositionWS", useCompactGBuffer ? Format::RGBA16F : Format::RGBA32F);
    geometryGraph->declareResource("gEmissive", Format::RGBA16F);
    geometryGraph->declareResource("gDepth", Format::D24_UNORM_S8_UINT, true);

    lightingGraph = std::make_unique<FrameGraph>(device_.get(), width, height);
    lightingGraph->declareResource("lightBuffer", Format::RGBA16F);
    lightingGraph->declareResource("sceneColor", Format::RGBA16F);

    particleGraph = std::make_unique<FrameGraph>(device_.get(), width, height);
    decalGraph = std::make_unique<FrameGraph>(device_.get(), width, height);

    auto hasDeferredGeometryInputs = [this]() -> bool {
        return !solidDraws.empty() || !alphaTestDraws.empty() ||
               !simpleLayerDraws.empty() ||
               !environmentDraws.empty() || !environmentAlphaDraws.empty();
    };
    auto hasForwardPassInputs = [this]() -> bool {
        return !environmentAlphaDraws.empty() || !refractionDraws.empty() ||
               !waterDraws.empty() || !postAlphaUnlitDraws.empty() ||
               !scene_.particleNodes.empty();
    };
    auto hasSceneColorProducerInputs =
        [hasDeferredGeometryInputs, hasForwardPassInputs]() -> bool {
            return hasDeferredGeometryInputs() || hasForwardPassInputs();
        };

auto& shadowPass = shadowGraph->addPass("ShadowCascades");
shadowPass.depthTest = true;
shadowPass.depthWrite = true;
shadowPass.cullFace = true;
// Shadow pass clears per cascade inside renderCascadedShadows().
shadowPass.clearDepth = false;
shadowPass.externalFBO = true;
shadowPass.shouldSkip = [this]() {
    return kDisableShadowPass || (solidDraws.empty() && simpleLayerDraws.empty());
};
shadowPass.execute = [this]() {
    if (kDisableShadowPass) {
        if (optRenderLOG) std::cout << "[SHADOW] Disabled by kDisableShadowPass\n";
        return;
    }
    if (optRenderLOG) std::cout << "[SHADOW] Begin shadow cascade rendering\n";
    glm::vec3 camPos = camera_.getPosition();
    glm::vec3 camForward = camera_.getForward();
    renderCascadedShadows(camPos, camForward);
    if (gEnableGLErrorChecking) {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) std::cerr << "[SHADOW] Error: 0x" << std::hex << err << std::dec << "\n";
    }
    if (optRenderLOG) std::cout << "[SHADOW] Complete\n";
};
shadowGraph->compile();

    auto& geometryPass = geometryGraph->addPass("Geometry");
    geometryPass.writes = {"gPositionVS", "gNormalDepthPacked", "gAlbedoSpec", "gPositionWS", "gEmissive", "gNormalDepthEncoded", "gDepth"};
    geometryPass.depthTest = true;
    geometryPass.depthWrite = true;
    geometryPass.cullFace = true;
    geometryPass.stencilTest = true;
    geometryPass.clearColorBuffer = true;
    geometryPass.clearColor = glm::vec4(0, 0, 0, 0);
    geometryPass.clearDepth = true;
    geometryPass.clearStencil = true;
    geometryPass.shouldSkip = [hasSceneColorProducerInputs]() {
        return !hasSceneColorProducerInputs();
    };
    geometryPass.execute = [this]() {
        using GClock = std::chrono::steady_clock;
        auto gMs = [](GClock::time_point t0) {
            return std::chrono::duration<double, std::milli>(GClock::now() - t0).count();
        };
        if (kDisableGeometryPass) {
            if (optRenderLOG) std::cout << "[GEOMETRY] Disabled by kDisableGeometryPass\n";
            return;
        }
        auto tGeomSetup = GClock::now();
        if (optRenderLOG) std::cout << "[GEOMETRY] Begin geometry pass\n";
        if (optRenderLOG) std::cout << "[GEOMETRY] whiteTex=" << whiteTex << " gSamplerRepeat=" << gSamplerRepeat << "\n";

        if (!solidDraws.empty()) {
            auto& first = solidDraws.front();
            if (optRenderLOG) std::cout << "[GEOMETRY] First draw textures: " << first.tex[0] << "," << first.tex[1] << "," << first.tex[2] << "," << first.tex[3] << "\n";
        }
    if (optRenderLOG) {
        GLint fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        std::cout << "[GEOMETRY] FBO=" << fbo << " Viewport=" << vp[2] << "x" << vp[3] << "\n";
    }

    {
        static bool sStencilSetupDone = false;
        if (!sStencilSetupDone) {
            sStencilSetupDone = true;
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glStencilMask(0xFF);
        }
    }

    const bool bindlessAvailable = TextureServer::sBindlessSupported && materialSSBO_.valid();
    auto standardShader = shaderManager->GetShader("standard");
    decltype(standardShader) shader;
    const char* geometryShaderName = nullptr;
    bool useBindlessGeometry = false;
    if (bindlessAvailable) {
        shader = shaderManager->GetShader("NDEVCdeferred_bindless");
        if (shader) {
            geometryShaderName = "NDEVCdeferred_bindless";
            useBindlessGeometry = true;
        }
    }
    if (!shader) {
        shader = standardShader ? standardShader : shaderManager->GetShader("NDEVCdeferred");
        geometryShaderName = standardShader ? "standard" : "NDEVCdeferred";
    }
    if (!shader) {
        std::cerr << "[GEOMETRY] ERROR: Shader 'standard'/'NDEVCdeferred' not found\n";
        return;
    }
    const GLuint deferredProgram = shader->GetNativeHandle()
        ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
        : 0;
    if (optRenderLOG) std::cout << "[GEOMETRY] Shader: " << geometryShaderName << " ID: " << deferredProgram << "\n";

    static GLuint sLastDeferredProgram = 0;
    static GLint normalMatrixLoc = -1;
    static GLint depthScaleLoc = -1;
    if (sLastDeferredProgram != deferredProgram) {
        sLastDeferredProgram = deferredProgram;
        deferredShaderCompatibilityLogged_ = false;
        deferredShaderHasNormalMatrixUniform_ = false;
        deferredShaderUsesPackedGBuffer_ = false;
        normalMatrixLoc = -1;
        depthScaleLoc = -1;
        if (deferredProgram != 0) {
            normalMatrixLoc = glGetUniformLocation(deferredProgram, "normalMatrix");
            depthScaleLoc = glGetUniformLocation(deferredProgram, "depthScale");
            deferredShaderHasNormalMatrixUniform_ = normalMatrixLoc >= 0;

            const GLint packedNormalOutLoc = glGetFragDataLocation(deferredProgram, "gNormalDepth");
            const GLint standardNormalOutLoc = glGetFragDataLocation(deferredProgram, "gNormalDepthPacked");
            const GLint positionVSOutLoc = glGetFragDataLocation(deferredProgram, "gPositionVS");
            deferredShaderUsesPackedGBuffer_ =
                (packedNormalOutLoc == 0 && standardNormalOutLoc < 0 && positionVSOutLoc < 0);

            if (deferredShaderHasNormalMatrixUniform_ && (optRenderLOG || kLogShaderCompat)) {
                if (kForceNonInstancedNormalMatrixPath) {
                    std::cout << "[GEOMETRY] " << geometryShaderName << " uses 'normalMatrix'; switching deferred draws to non-instanced per-draw normalMatrix uploads.\n";
                } else {
                    std::cout << "[GEOMETRY] " << geometryShaderName << " uses 'normalMatrix'; keeping instanced deferred draws with view-space fallback normalMatrix.\n";
                }
            }
            if (deferredShaderUsesPackedGBuffer_ && (optRenderLOG || kLogShaderCompat)) {
                std::cout << "[GEOMETRY] " << geometryShaderName << " writes packed legacy G-buffer outputs; enabling packed-to-standard G-buffer compatibility pass.\n";
            }
            deferredShaderCompatibilityLogged_ = true;
        }
    }
    deferredPackedCompatReady_ = !deferredShaderUsesPackedGBuffer_;

    constexpr GLsizei kStandardGBufferDrawBufferCount = 5;
    constexpr GLenum kStandardGBufferDrawBuffers[kStandardGBufferDrawBufferCount] = {
        GL_NONE, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4
    };
    constexpr GLsizei kPackedDeferredDrawBufferCount = 4;
    constexpr GLenum kPackedDeferredDrawBuffers[kPackedDeferredDrawBufferCount] = {
        GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4
    };
    auto setGeometryDrawBufferLayout = [&](bool packedDeferredLayout) {
        static GLint cachedMaxDrawBuffers = -1;
        if (cachedMaxDrawBuffers < 0) {
            glGetIntegerv(GL_MAX_DRAW_BUFFERS, &cachedMaxDrawBuffers);
            if (cachedMaxDrawBuffers <= 0) {
                cachedMaxDrawBuffers = 1;
            }
        }
        static GLuint sCachedDBProgram = 0;
        static bool sCachedPackedLayout = false;
        static bool sCachedLayoutSet = false;
        if (sCachedDBProgram != deferredProgram) {
            sCachedDBProgram = deferredProgram;
            sCachedLayoutSet = false;
        }
        if (sCachedLayoutSet && sCachedPackedLayout == packedDeferredLayout) return;
        sCachedPackedLayout = packedDeferredLayout;
        sCachedLayoutSet = true;
        const GLenum* drawBuffers = packedDeferredLayout ? kPackedDeferredDrawBuffers : kStandardGBufferDrawBuffers;
        const GLsizei desiredCount = packedDeferredLayout ? kPackedDeferredDrawBufferCount : kStandardGBufferDrawBufferCount;
        GLsizei drawBufferCount = desiredCount;
        if (drawBufferCount > static_cast<GLsizei>(cachedMaxDrawBuffers)) {
            drawBufferCount = static_cast<GLsizei>(cachedMaxDrawBuffers);
        }
        static bool warnedDrawBufferClamp = false;
        if (drawBufferCount < desiredCount && !warnedDrawBufferClamp) {
            std::cerr << "[GEOMETRY] GL_MAX_DRAW_BUFFERS=" << cachedMaxDrawBuffers
                      << "; clamping geometry MRT draw buffers to " << drawBufferCount << ".\n";
            warnedDrawBufferClamp = true;
        }
        glDrawBuffers(drawBufferCount, drawBuffers);
    };

    shader->PrecacheUniform(U_PROJECTION, "projection");
    shader->PrecacheUniform(U_VIEW, "view");
    shader->PrecacheUniform(U_MODEL, "model");
    shader->PrecacheUniform(U_TEXTURE_TRANSFORM0, "textureTransform0");
    shader->PrecacheUniform(U_USE_SKINNING, "UseSkinning");
    shader->PrecacheUniform(U_USE_INSTANCING, "UseInstancing");
    if (!useBindlessGeometry) {
        shader->PrecacheUniform(U_MAT_EMISSIVE_INTENSITY, "MatEmissiveIntensity");
        shader->PrecacheUniform(U_MAT_SPECULAR_INTENSITY, "MatSpecularIntensity");
        shader->PrecacheUniform(U_MAT_SPECULAR_POWER, "MatSpecularPower");
        shader->PrecacheUniform(U_ALPHA_TEST, "alphaTest");
        shader->PrecacheUniform(U_ALPHA_CUTOFF, "alphaCutoff");
        shader->PrecacheUniform(U_DIFF_MAP0, "DiffMap0");
        shader->PrecacheUniform(U_SPEC_MAP0, "SpecMap0");
        shader->PrecacheUniform(U_BUMP_MAP0, "BumpMap0");
        shader->PrecacheUniform(U_EMSV_MAP0, "EmsvMap0");
        shader->PrecacheUniform(U_TWO_SIDED, "twoSided");
        shader->PrecacheUniform(U_IS_FLAT_NORMAL, "isFlatNormal");
        shader->PrecacheUniform(U_RECEIVES_DECALS, "ReceivesDecals");
    }

    shader->Use();
    {
        static GLuint sCachedGeomProgram = 0;
        static glm::mat4 sCachedGeomProj(0.0f), sCachedGeomView(0.0f);
        const bool progChanged = (sCachedGeomProgram != deferredProgram);
        if (progChanged) sCachedGeomProgram = deferredProgram;
        const glm::mat4 curProj = camera_.getProjectionMatrix();
        const glm::mat4 curView = camera_.getViewMatrix();
        if (progChanged || curProj != sCachedGeomProj) {
            shader->SetMat4(U_PROJECTION, curProj);
            sCachedGeomProj = curProj;
        }
        if (progChanged || curView != sCachedGeomView) {
            shader->SetMat4(U_VIEW, curView);
            sCachedGeomView = curView;
        }
        if (progChanged) {
            shader->SetMat4(U_TEXTURE_TRANSFORM0, glm::mat4(1.0f));
            shader->SetInt(U_USE_SKINNING, 0);
            shader->SetInt(U_USE_INSTANCING, 1);
            if (!useBindlessGeometry) {
                shader->SetFloat(U_MAT_EMISSIVE_INTENSITY, 0.0f);
                shader->SetFloat(U_MAT_SPECULAR_INTENSITY, 0.0f);
                shader->SetFloat(U_MAT_SPECULAR_POWER, 32.0f);
                shader->SetInt(U_ALPHA_TEST, 0);
                shader->SetFloat(U_ALPHA_CUTOFF, 0.5f);
                shader->SetInt(U_DIFF_MAP0, 0);
                shader->SetInt(U_SPEC_MAP0, 1);
                shader->SetInt(U_BUMP_MAP0, 2);
                shader->SetInt(U_EMSV_MAP0, 3);
                shader->SetInt("diffMapSampler", 0);
                shader->SetInt("specMapSampler", 1);
                shader->SetInt("bumpMapSampler", 2);
                shader->SetInt("emsvSampler", 3);
                shader->SetInt(U_TWO_SIDED, 0);
                shader->SetInt(U_IS_FLAT_NORMAL, 0);
                shader->SetVec4("fogDistances", glm::vec4(180.0f, 520.0f, 0.0f, 0.0f));
                shader->SetVec4("fogColor", glm::vec4(0.61f, 0.58f, 0.52f, 0.0f));
                shader->SetVec4("heightFogColor", glm::vec4(0.61f, 0.58f, 0.52f, 100000.0f));
                shader->SetVec4("pixelSize", glm::vec4(
                    1.0f / static_cast<float>(std::max(width, 1)),
                    1.0f / static_cast<float>(std::max(height, 1)),
                    0.0f, 0.0f));
                shader->SetFloat("encodefactor", 1.0f);
                shader->SetFloat("alphaBlendFactor", 1.0f);
                shader->SetFloat("mayaAnimableAlpha", 1.0f);
                shader->SetFloat("AlphaClipRef", 128.0f);
                shader->SetVec4("customColor2", glm::vec4(0.0f));
            }
        }
        if (depthScaleLoc >= 0 && progChanged) {
            glUniform1f(depthScaleLoc, 1.0f);
        }
        if (normalMatrixLoc >= 0 && (progChanged || curView != sCachedGeomView)) {
            const glm::mat3 viewOnlyNormalMatrix = glm::transpose(glm::inverse(glm::mat3(curView)));
            glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, &viewOnlyNormalMatrix[0][0]);
        }
    }

    setGeometryDrawBufferLayout(deferredShaderUsesPackedGBuffer_);

    if (device_) {
        if (cachedGeomState_) device_->ApplyRenderState(cachedGeomState_.get());
    } else {
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    glm::mat4 viewProj = camera_.getProjectionMatrix() * camera_.getViewMatrix();
    const Camera::Frustum& frustum = frameFrustum_;
    const bool disableFrustumCulling = FrustumCullingDisabled();
    const bool disableFaceCulling = FaceCullingDisabled();
    UpdateVisibilityThisFrame(frustum);
    if (!visResolveSkipped_) {
        alphaTestBatchSystem_.MarkStaticVisibilityDirty();
        alphaTestBatchSystem_.updateStaticVisibility(alphaTestDraws, &frustum);
        environmentBatchSystem_.MarkStaticVisibilityDirty();
        environmentBatchSystem_.updateStaticVisibility(environmentDraws, &frustum);
        environmentAlphaBatchSystem_.MarkStaticVisibilityDirty();
        environmentAlphaBatchSystem_.updateStaticVisibility(environmentAlphaDraws, &frustum);
    }
    auto isDrawVisible = [&](const DrawCmd& dc, const Camera::Frustum& fr, bool keepStaticVisible) -> bool {
        return PassesFrustumCulling(dc, fr, disableFrustumCulling, keepStaticVisible);
    };

    auto drawDeferredCmd = [&](const DrawCmd& dc, bool forceAlphaTest) -> bool {
        if (!dc.mesh || dc.disabled) return false;
        if (!isDrawVisible(dc, frustum, true)) return false;

        shader->SetMat4(U_MODEL, dc.worldMatrix);
        if (normalMatrixLoc >= 0) {
            const glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(camera_.getViewMatrix() * dc.worldMatrix)));
            glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, &normalMatrix[0][0]);
        }
        shader->SetInt(U_RECEIVES_DECALS, dc.receivesDecals ? 1 : 0);
        shader->SetFloat(U_MAT_EMISSIVE_INTENSITY, dc.cachedMatEmissiveIntensity);
        shader->SetFloat(U_MAT_SPECULAR_INTENSITY, dc.cachedMatSpecularIntensity);
        shader->SetFloat(U_MAT_SPECULAR_POWER, dc.cachedMatSpecularPower);
        shader->SetInt(U_ALPHA_TEST, forceAlphaTest ? 1 : (dc.alphaTest ? 1 : 0));
        shader->SetFloat(U_ALPHA_CUTOFF, dc.alphaCutoff);
        shader->SetInt(U_TWO_SIDED, dc.cachedTwoSided);
        shader->SetInt(U_IS_FLAT_NORMAL, dc.cachedIsFlatNormal);
        shader->SetFloat("alphaBlendFactor", dc.cachedAlphaBlendFactor);
        shader->SetFloat("AlphaClipRef", glm::clamp(dc.alphaCutoff * 256.0f, 0.0f, 255.0f));
        auto customTintIt = dc.shaderParamsVec4.find("customColor2");
        const glm::vec4 customTint = (customTintIt != dc.shaderParamsVec4.end())
            ? customTintIt->second
            : glm::vec4(0.0f);
        shader->SetVec4("customColor2", customTint);

        const bool hasSpecMap = dc.cachedHasSpecMap;
        bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
        bindTexture(1, hasSpecMap ? (dc.tex[1] ? toTextureHandle(dc.tex[1]) : blackTex) : blackTex);
        bindTexture(2, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
        bindTexture(3, dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex);
        bindSampler(0, gSamplerRepeat);
        bindSampler(1, gSamplerRepeat);
        bindSampler(2, gSamplerRepeat);
        bindSampler(3, gSamplerRepeat);
        glStencilFunc(GL_ALWAYS, dc.receivesDecals ? 1 : 0, 0xFF);

        if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
            auto& g = dc.mesh->groups[dc.group];
            glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                reinterpret_cast<void*>(static_cast<intptr_t>((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t))));
        } else {
            for (const auto& g : dc.mesh->groups) {
                glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                    reinterpret_cast<void*>(static_cast<intptr_t>((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t))));
            }
        }
        return true;
    };

    const bool useNonInstancedNormalMatrixPath =
        ForceNonBatchedGeometry() ||
        (deferredShaderHasNormalMatrixUniform_ && kForceNonInstancedNormalMatrixPath);

    {
        static int sCachedCullMode = -1; // -1=unset, 0=disabled, 1=enabled
        const int wantCull = disableFaceCulling ? 0 : 1;
        if (sCachedCullMode != wantCull) {
            sCachedCullMode = wantCull;
            if (disableFaceCulling) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
        }
    }

    frameProfile_.geomSetup = gMs(tGeomSetup);

    constexpr int kGpuGeomSolidPhase = 0;
    constexpr int kGpuGeomAlphaPhase = 1;
    constexpr int kGpuGeomSimpleLayerPhase = 2;
    constexpr int kGpuGeomEnvironmentPhase = 3;
    auto markGeomGpuTimestamp = [&](int phase, bool begin) {
        if (!gpuQueriesInit_) return;
        const int idx = phase * 2 + (begin ? 0 : 1);
        glQueryCounter(gpuGeomTsDB_[gpuQueryBufWrite_][idx], GL_TIMESTAMP);
    };

    markGeomGpuTimestamp(kGpuGeomSolidPhase, true);
    if (useNonInstancedNormalMatrixPath) {
        auto t0 = GClock::now();
        shader->SetInt(U_USE_INSTANCING, 0);
        MegaBuffer::instance().bind();
        int renderedSolid = 0;
        for (const auto& dc : solidDraws) {
            if (drawDeferredCmd(dc, false)) ++renderedSolid;
        }
        frameDrawCalls_ += renderedSolid;
        frameProfile_.geomSolidFlush = gMs(t0);
        if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Solid draws (non-instanced normalMatrix path): ", renderedSolid);
    } else {
        { auto t0 = GClock::now();
        if (!visResolveSkipped_) solidBatchSystem_.MarkStaticVisibilityDirty();
        solidBatchSystem_.cull(solidDraws, frustum);
        frameProfile_.geomSolidCull = gMs(t0); }
        if (optRenderLOG) std::cout << "[GEOMETRY] Solid draws: " << solidDraws.size() << "\n";

        this->clearError("Geometry::PreSolidFlush");
        if (useBindlessGeometry && materialSSBO_.valid()) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO_);
        }
        { auto t0 = GClock::now();
        solidBatchSystem_.flush(samplerRepeat_abstracted.get(), 4, deferredProgram, useBindlessGeometry);
        frameProfile_.geomSolidFlush = gMs(t0); }

        this->clearError("Geometry::AfterSolidFlush");

        for (int i = 0; i < 5; i++) {
            if (device_) {
                device_->BindSampler(nullptr, i);
            } else {
                glBindSampler(i, 0);
            }
        }
    }

    int solidAnimOverlayDrawCount = 0;
    float animTime = static_cast<float>(glfwGetTime());

    shader->SetInt(U_USE_INSTANCING, 0);
    for (size_t dcIndex : solidShaderVarAnimatedIndices) {
        if (dcIndex >= solidDraws.size()) continue;
        auto& dc = solidDraws[dcIndex];
        if (!dc.mesh) continue;
        if (dc.disabled) continue;
        if (!isDrawVisible(dc, frustum, true)) continue;

        float dcAnimTime = animTime;
        if (dc.instance) {
            auto itSpawn = scene_.instanceSpawnTimes.find(dc.instance);
            if (itSpawn != scene_.instanceSpawnTimes.end()) {
                dcAnimTime = std::max(0.0f, animTime - static_cast<float>(itSpawn->second));
            }
        }

        if (!dc.sourceNode) continue;
        static thread_local std::unordered_map<std::string, float> animParams;
        DeferredRendererAnimation::SampleShaderVarAnimations(dc.sourceNode, dcAnimTime, animParams);
        if (animParams.empty()) continue;

        clearError("Lighting::PassStart");
        shader->Use();
        shader->SetMat4(U_MODEL, dc.worldMatrix);
        for (const auto& [paramName, value] : animParams) {
            shader->SetFloat(paramName, value);
        }
        this->bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
        this->bindTexture(1, dc.tex[1] ? toTextureHandle(dc.tex[1]) : whiteTex);
        this->bindTexture(2, dc.tex[2] ? toTextureHandle(dc.tex[2]) : whiteTex);
        this->bindTexture(3, dc.tex[3] ? toTextureHandle(dc.tex[3]) : whiteTex);

        MegaBuffer::instance().bind();
        if (dc.group >= 0 && dc.group < (int)dc.mesh->groups.size()) {
            auto& g = dc.mesh->groups[dc.group];
            glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                (void*)(intptr_t)((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t)));
            solidAnimOverlayDrawCount++;
        }
    }

    frameDrawCalls_ += solidAnimOverlayDrawCount;
    if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Solid animated overlays: ", solidAnimOverlayDrawCount);
    markGeomGpuTimestamp(kGpuGeomSolidPhase, false);

    markGeomGpuTimestamp(kGpuGeomAlphaPhase, true);
    if (!alphaTestDraws.empty()) {
        if (useNonInstancedNormalMatrixPath) {
            auto t0 = GClock::now();
            shader->SetInt(U_ALPHA_TEST, 1);
            shader->SetInt(U_USE_INSTANCING, 0);
            MegaBuffer::instance().bind();
            int renderedAlpha = 0;
            for (const auto& dc : alphaTestDraws) {
                if (drawDeferredCmd(dc, true)) ++renderedAlpha;
            }
            frameDrawCalls_ += renderedAlpha;
            frameProfile_.geomAlphaFlush = gMs(t0);
            if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Alpha test draws (non-instanced normalMatrix path): ", renderedAlpha);
            shader->SetInt(U_ALPHA_TEST, 0);
        } else {
            { auto t0 = GClock::now();
            alphaTestBatchSystem_.cullGeneric(alphaTestDraws, frameFrustum_, 1, 5);
            frameProfile_.geomAlphaCull = gMs(t0); }

            bool ranAlphaDepthPrepass = false;
            if (kEnableAlphaDepthPrepass) {
                auto alphaDepthStd = shaderManager->GetShader("NDEVCdeferred_alpha_depth");
                auto alphaDepthBindless = shaderManager->GetShader("NDEVCdeferred_alpha_depth_bindless");
                decltype(alphaDepthStd) alphaDepthShader = alphaDepthStd;
                bool useBindlessAlphaDepth = false;

                if (useBindlessGeometry && materialSSBO_.valid()) {
                    if (alphaDepthBindless) {
                        alphaDepthShader = alphaDepthBindless;
                        useBindlessAlphaDepth = true;
                    } else {
                        static bool warnedMissingBindlessDepth = false;
                        if (!warnedMissingBindlessDepth) {
                            NC::LOGGING::Warning("[GEOMETRY] Alpha depth prepass bindless shader missing; skipping prepass in bindless mode.");
                            warnedMissingBindlessDepth = true;
                        }
                        alphaDepthShader.reset();
                    }
                }

                if (alphaDepthShader) {
                    const GLuint alphaDepthProgram = alphaDepthShader->GetNativeHandle()
                        ? *reinterpret_cast<GLuint*>(alphaDepthShader->GetNativeHandle())
                        : 0;

                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glDepthMask(GL_TRUE);
                    glDepthFunc(GL_LEQUAL);
                    glStencilMask(0x00);

                    alphaDepthShader->Use();
                    alphaDepthShader->SetMat4(U_PROJECTION, camera_.getProjectionMatrix());
                    alphaDepthShader->SetMat4(U_VIEW, camera_.getViewMatrix());
                    alphaDepthShader->SetMat4(U_TEXTURE_TRANSFORM0, glm::mat4(1.0f));
                    alphaDepthShader->SetInt(U_USE_SKINNING, 0);
                    alphaDepthShader->SetInt(U_USE_INSTANCING, 1);

                    if (!useBindlessAlphaDepth) {
                        alphaDepthShader->SetInt(U_DIFF_MAP0, 0);
                        alphaDepthShader->SetInt(U_ALPHA_TEST, 1);
                        alphaDepthShader->SetFloat(U_ALPHA_CUTOFF, 0.5f);
                        alphaDepthShader->SetFloat("alphaBlendFactor", 1.0f);
                        alphaDepthShader->SetFloat("mayaAnimableAlpha", 1.0f);
                        alphaDepthShader->SetFloat("AlphaClipRef", 128.0f);
                    } else {
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO_);
                    }

                    MegaBuffer::instance().bind();
                    alphaTestBatchSystem_.flush(samplerRepeat_abstracted.get(), 5, alphaDepthProgram, useBindlessAlphaDepth);
                    ranAlphaDepthPrepass = true;

                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glStencilMask(0xFF);
                }
            }

            shader->Use();
            if (!useBindlessGeometry) {
                shader->SetInt(U_ALPHA_TEST, 1);
            }
            shader->SetInt(U_USE_INSTANCING, 1);
            if (ranAlphaDepthPrepass) {
                glDepthFunc(GL_EQUAL);
                glDepthMask(GL_FALSE);
            }

            MegaBuffer::instance().bind();
            if (useBindlessGeometry && materialSSBO_.valid()) {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO_);
            }
            { auto t0 = GClock::now();
            alphaTestBatchSystem_.flush(samplerRepeat_abstracted.get(), 5, deferredProgram, useBindlessGeometry);
            frameProfile_.geomAlphaFlush = gMs(t0); }

            const int alphaTestBatches = static_cast<int>(alphaTestBatchSystem_.activeBatches().size());
            frameDrawCalls_ += alphaTestBatches;
            if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Alpha test draws (batched): batches=", alphaTestBatches, " objs=", alphaTestDraws.size());
            if (ranAlphaDepthPrepass) {
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
            }
        }
    }
    markGeomGpuTimestamp(kGpuGeomAlphaPhase, false);

    // Non-deferred shaders use the standard G-buffer location mapping.
    setGeometryDrawBufferLayout(false);

    // Non-deferred shaders use the standard G-buffer location mapping.
    setGeometryDrawBufferLayout(false);

    markGeomGpuTimestamp(kGpuGeomSimpleLayerPhase, true);
    if (!kDisableSLPass && !simpleLayerDraws.empty()) {
        auto tSL = GClock::now();
        auto slGBufferShader     = shaderManager->GetShader("simplelayer_gbuffer");
        auto slGBufferClipShader = shaderManager->GetShader("simplelayer_gbuffer_clip");
        if (!slGBufferShader || !slGBufferClipShader) {
            std::cerr << "[GEOMETRY] simplelayer gbuffer shader variants not found\n";
        } else {
            constexpr GLenum kSlGBufDrawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, kSlGBufDrawBuffers);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);

            const glm::mat4 slView    = camera_.getViewMatrix();
            const glm::mat4 slProj    = camera_.getProjectionMatrix();
            const glm::mat4 invSlView = glm::inverse(slView);
            const glm::mat3 slInvViewRot = glm::mat3(invSlView);

            // ── Fix #2: cache invWorld per DrawCmd to avoid glm::inverse every frame ──
            // ── Fix #4: batch into instanced indirect draws grouped by texture-state  ──

            // Scratch: build sort entries for all visible simpleLayer draws
            struct SlEntry {
                uint64_t texKey;
                uint64_t geomKey;
                const DrawCmd* dc;
                float layerTiling;
            };
            // Use a thread-local temp vector to avoid repeated allocation
            static thread_local std::vector<SlEntry> slEntries;

            // Skip vis loop entirely when camera is static and SL cache is still valid
            const bool slFullSkip = slGBufCacheValid_ && slViewProjCacheValid_ &&
                (viewProj == slCachedViewProj_) && !slGBufGroups_.empty();

            if (!slFullSkip) {
            slEntries.clear();

            auto tSLVis = GClock::now();
            for (const auto& dc : simpleLayerDraws) {
                if (!dc.mesh || dc.disabled) continue;
                if (!isDrawVisible(dc, frameFrustum_, true)) continue;

                // Fix #2: update cached inverse world matrix
                const uint64_t wh = HashMatrix4(dc.worldMatrix);
                if (dc.invWorldMatrixHash != wh) {
                    dc.cachedInvWorldMatrix = glm::inverse(dc.worldMatrix);
                    dc.invWorldMatrixHash   = wh;
                }

                const float layerTiling = [&]() -> float {
                    auto it = dc.shaderParamsFloat.find("layerTiling");
                    return it != dc.shaderParamsFloat.end() ? it->second : 1.0f;
                }();

                // Geometry key: uniquely identifies a mesh draw group
                uint32_t gCount = 0, gFirst = 0;
                if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                    gCount = dc.mesh->groups[dc.group].indexCount();
                    gFirst = dc.megaIndexOffset + dc.mesh->groups[dc.group].firstIndex();
                } else if (!dc.mesh->groups.empty()) {
                    // multi-group: use first group for key; each will get its own command
                    gCount = dc.mesh->groups[0].indexCount();
                    gFirst = dc.megaIndexOffset + dc.mesh->groups[0].firstIndex();
                } else { continue; }

                // Texture key: encodes state that requires draw-call separation
                auto ptrBits = [](const void* p) { return reinterpret_cast<uintptr_t>(p); };
                uint64_t tk = 0;
                auto mix64 = [&](uint64_t v) {
                    tk ^= (v * 0x9e3779b97f4a7c15ull) + 0x6c62272e07bb0142ull + (tk << 6) + (tk >> 2);
                };
                if (dc.alphaTest) mix64(ptrBits(dc.tex[0]));
                mix64(ptrBits(dc.tex[2]));
                mix64(ptrBits(dc.tex[6]));
                mix64(ptrBits(dc.tex[7]));
                mix64(static_cast<uint64_t>(dc.alphaTest));
                mix64(static_cast<uint64_t>(static_cast<uint32_t>(dc.cullMode)));
                mix64(static_cast<uint64_t>(dc.receivesDecals ? 1u : 0u));
                if (dc.alphaTest) {
                    uint32_t cutoffBits;
                    const float cut = glm::clamp(dc.alphaCutoff * 256.0f, 0.0f, 255.0f);
                    std::memcpy(&cutoffBits, &cut, 4);
                    mix64(static_cast<uint64_t>(cutoffBits));
                }
                const uint64_t geomKey = (static_cast<uint64_t>(gCount) << 32) | gFirst;
                slEntries.push_back({tk, geomKey, &dc, layerTiling});
            }
            frameProfile_.geomSLVis = gMs(tSLVis);
            frameProfile_.geomSLVisibleCount = static_cast<int>(slEntries.size());
            } else {
                frameProfile_.geomSLVis = 0.0f;
                frameProfile_.geomSLVisibleCount = static_cast<int>(slEntries.size());
            }

            if (!slEntries.empty() || slFullSkip) {
                if (slFullSkip) {
                    // Camera static: skip vis hash + cache rebuild, rebind cached SSBOs
                    frameProfile_.slCacheHit = 1;
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slGBufWorldMatSSBO_);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, slGBufInvWorldSSBO_);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, slGBufTilingSSBO_);
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, slGBufIndirectBuffer_);
                } else {
                // Compute visibility hash (draw identity + transform state)
                uint64_t visHash = slEntries.size();
                for (const auto& e : slEntries)
                    visHash ^= (reinterpret_cast<uintptr_t>(e.dc) * 0x9e3779b97f4a7c15ull)
                             ^ (e.dc->invWorldMatrixHash * 0x517cc1b727220a95ull);

                const bool slCacheHit = slGBufCacheValid_ && visHash == slGBufVisHash_;
                frameProfile_.slCacheHit = slCacheHit ? 1 : 0;

                if (!slCacheHit) {
                // Sort by (texKey, geomKey) for contiguous grouping
                auto tSLSort = GClock::now();
                std::sort(slEntries.begin(), slEntries.end(), [](const SlEntry& a, const SlEntry& b) {
                    if (a.texKey != b.texKey) return a.texKey < b.texKey;
                    return a.geomKey < b.geomKey;
                });
                frameProfile_.geomSLSort = gMs(tSLSort);

                // Build per-frame flat instance arrays and group descriptors
                auto tSLGroup = GClock::now();
                slGBufGroups_.clear();
                slGBufCmds_.clear();
                slGBufWorldMats_.clear();
                slGBufInvWorldMats_.clear();
                slGBufTilings_.clear();

                uint64_t lastTK = ~0ull, lastGK = ~0ull;
                for (const auto& e : slEntries) {
                    // New texture group?
                    if (e.texKey != lastTK) {
                        SlGBufGroup grp{};
                        const DrawCmd& dc = *e.dc;
                        if (dc.alphaTest) grp.texSlot[0] = dc.tex[0];
                        grp.texSlot[1] = dc.tex[2];
                        grp.texSlot[2] = dc.tex[6];
                        grp.texSlot[3] = dc.tex[7];
                        grp.alphaTest    = dc.alphaTest;
                        grp.alphaClipRef = glm::clamp(dc.alphaCutoff * 256.0f, 0.0f, 255.0f);
                        grp.cullMode     = dc.cullMode;
                        grp.receivesDecals = dc.receivesDecals;
                        grp.cmdOffset    = static_cast<uint32_t>(slGBufCmds_.size());
                        grp.cmdCount     = 0;
                        slGBufGroups_.push_back(grp);
                        lastTK = e.texKey;
                        lastGK = ~0ull; // force new geom entry
                    }

                    // Handle single-group or multi-group meshes
                    const DrawCmd& dc = *e.dc;
                    const auto addGeomGroup = [&](uint32_t count, uint32_t firstIndex) {
                        const uint64_t gk = (static_cast<uint64_t>(count) << 32) | firstIndex;
                        // For texKey change we reset lastGK; within same texKey we detect new geom
                        // Multi-group meshes emit multiple geomKeys per entry (handled below)
                        bool newGeom = (gk != lastGK || e.texKey != lastTK);
                        (void)newGeom; // lastTK already handled above
                        // Check if the last command in this group matches
                        SlGBufGroup& grp = slGBufGroups_.back();
                        bool sameGeom = (grp.cmdCount > 0) &&
                            (slGBufCmds_[grp.cmdOffset + grp.cmdCount - 1].count      == count) &&
                            (slGBufCmds_[grp.cmdOffset + grp.cmdCount - 1].firstIndex == firstIndex);
                        if (!sameGeom) {
                            DrawCommand cmd{};
                            cmd.count         = count;
                            cmd.instanceCount = 0;
                            cmd.firstIndex    = firstIndex;
                            cmd.baseVertex    = 0;
                            cmd.baseInstance  = static_cast<uint32_t>(slGBufWorldMats_.size());
                            slGBufCmds_.push_back(cmd);
                            grp.cmdCount++;
                            lastGK = gk;
                        }
                        slGBufCmds_[grp.cmdOffset + grp.cmdCount - 1].instanceCount++;
                        slGBufWorldMats_.push_back(dc.worldMatrix);
                        slGBufInvWorldMats_.push_back(dc.cachedInvWorldMatrix);
                        slGBufTilings_.push_back(e.layerTiling);
                    };

                    if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                        auto& g = dc.mesh->groups[dc.group];
                        if (g.indexCount() > 0)
                            addGeomGroup(g.indexCount(),
                                         dc.megaIndexOffset + g.firstIndex());
                    } else {
                        for (const auto& g : dc.mesh->groups) {
                            if (g.indexCount() > 0)
                                addGeomGroup(g.indexCount(),
                                             dc.megaIndexOffset + g.firstIndex());
                        }
                    }
                }
                frameProfile_.geomSLGroup = gMs(tSLGroup);
                frameProfile_.geomSLGroupCount = static_cast<int>(slGBufGroups_.size());

                auto tSLUpload = GClock::now();
                // Upload world matrices SSBO (binding=1)
                {
                    const size_t matBytes = slGBufWorldMats_.size() * sizeof(glm::mat4);
                    if (!slGBufWorldMatSSBO_) glGenBuffers(1, slGBufWorldMatSSBO_.put());
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, slGBufWorldMatSSBO_);
                    if (matBytes > slGBufWorldMatSSBOCapacity_) {
                        slGBufWorldMatSSBOCapacity_ = matBytes * 2;
                        glBufferData(GL_SHADER_STORAGE_BUFFER,
                                     static_cast<GLsizeiptr>(slGBufWorldMatSSBOCapacity_),
                                     nullptr, GL_STREAM_DRAW);
                    }
                    void* p = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                        static_cast<GLsizeiptr>(matBytes),
                        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
                    if (p) { std::memcpy(p, slGBufWorldMats_.data(), matBytes); glUnmapBuffer(GL_SHADER_STORAGE_BUFFER); }
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slGBufWorldMatSSBO_);
                }

                // Upload invWorld SSBO (binding=2)
                {
                    const size_t matBytes = slGBufInvWorldMats_.size() * sizeof(glm::mat4);
                    if (!slGBufInvWorldSSBO_) glGenBuffers(1, slGBufInvWorldSSBO_.put());
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, slGBufInvWorldSSBO_);
                    if (matBytes > slGBufInvWorldSSBOCapacity_) {
                        slGBufInvWorldSSBOCapacity_ = matBytes * 2;
                        glBufferData(GL_SHADER_STORAGE_BUFFER,
                                     static_cast<GLsizeiptr>(slGBufInvWorldSSBOCapacity_),
                                     nullptr, GL_STREAM_DRAW);
                    }
                    void* p = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                        static_cast<GLsizeiptr>(matBytes),
                        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
                    if (p) { std::memcpy(p, slGBufInvWorldMats_.data(), matBytes); glUnmapBuffer(GL_SHADER_STORAGE_BUFFER); }
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, slGBufInvWorldSSBO_);
                }

                // Upload tiling SSBO (binding=3)
                {
                    const size_t tilBytes = slGBufTilings_.size() * sizeof(float);
                    if (!slGBufTilingSSBO_) glGenBuffers(1, slGBufTilingSSBO_.put());
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, slGBufTilingSSBO_);
                    if (tilBytes > slGBufTilingSSBOCapacity_) {
                        slGBufTilingSSBOCapacity_ = tilBytes * 2;
                        glBufferData(GL_SHADER_STORAGE_BUFFER,
                                     static_cast<GLsizeiptr>(slGBufTilingSSBOCapacity_),
                                     nullptr, GL_STREAM_DRAW);
                    }
                    void* p = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                        static_cast<GLsizeiptr>(tilBytes),
                        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
                    if (p) { std::memcpy(p, slGBufTilings_.data(), tilBytes); glUnmapBuffer(GL_SHADER_STORAGE_BUFFER); }
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, slGBufTilingSSBO_);
                }

                // Upload indirect draw buffer
                {
                    const size_t cmdBytes = slGBufCmds_.size() * sizeof(DrawCommand);
                    if (!slGBufIndirectBuffer_) glGenBuffers(1, slGBufIndirectBuffer_.put());
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, slGBufIndirectBuffer_);
                    if (cmdBytes > slGBufIndirectBufferCapacity_) {
                        slGBufIndirectBufferCapacity_ = cmdBytes * 2;
                        glBufferData(GL_DRAW_INDIRECT_BUFFER,
                                     static_cast<GLsizeiptr>(slGBufIndirectBufferCapacity_),
                                     nullptr, GL_STREAM_DRAW);
                    }
                    void* p = glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, 0,
                        static_cast<GLsizeiptr>(cmdBytes),
                        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
                    if (p) { std::memcpy(p, slGBufCmds_.data(), cmdBytes); glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER); }
                }
                frameProfile_.geomSLUpload = gMs(tSLUpload);

                slGBufVisHash_ = visHash;
                slGBufCacheValid_ = true;
                slCachedViewProj_ = viewProj;
                slViewProjCacheValid_ = true;
                } else {
                    // Cache hit: re-bind existing SSBOs (skip sort+group+upload)
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slGBufWorldMatSSBO_);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, slGBufInvWorldSSBO_);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, slGBufTilingSSBO_);
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, slGBufIndirectBuffer_);
                    slCachedViewProj_ = viewProj;
                    slViewProjCacheValid_ = true;
                }
                } // end !slFullSkip

                // Always report group count (valid on both cache hit and miss)
                frameProfile_.geomSLGroupCount = static_cast<int>(slGBufGroups_.size());

                MegaBuffer::instance().bind();

                auto tSLRender = GClock::now();
                // Set per-frame uniforms shared by both shader variants
                const GLuint slProg     = slGBufferShader->GetNativeHandle()
                    ? *reinterpret_cast<GLuint*>(slGBufferShader->GetNativeHandle()) : 0;
                const GLuint slClipProg = slGBufferClipShader->GetNativeHandle()
                    ? *reinterpret_cast<GLuint*>(slGBufferClipShader->GetNativeHandle()) : 0;

                auto setInstancingUniforms = [&](GLuint prog, decltype(slGBufferShader)& sh) {
                    sh->Use();
                    sh->SetInt("UseInstancing", 1);
                    sh->SetMat4("view",       slView);       // column-vector convention (no transpose)
                    sh->SetMat4("projection", slProj);
                    sh->SetMat4("invView",    invSlView);
                    if (prog) {
                        const GLint loc = glGetUniformLocation(prog, "invViewRot");
                        if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_FALSE, &slInvViewRot[0][0]);
                    }
                };
                setInstancingUniforms(slProg,     slGBufferShader);
                setInstancingUniforms(slClipProg, slGBufferClipShader);

                int renderedGroups = 0;
                for (const auto& grp : slGBufGroups_) {
                    if (grp.cullMode <= 0) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        glCullFace(grp.cullMode == 1 ? GL_FRONT : GL_BACK);
                    }
                    glStencilFunc(GL_ALWAYS, grp.receivesDecals ? 1 : 0, 0xFF);

                    auto& sh = grp.alphaTest ? slGBufferClipShader : slGBufferShader;
                    sh->Use();

                    if (grp.alphaTest) {
                        sh->SetFloat("AlphaClipRef", grp.alphaClipRef);
                        sh->SetInt("diffMapSampler", 0);
                        bindTexture(0, grp.texSlot[0] ? toTextureHandle(grp.texSlot[0]) : whiteTex);
                        bindSampler(0, gSamplerRepeat);
                        sh->SetInt("bumpMapSampler",  1);
                        sh->SetInt("bumpMap1Sampler", 2);
                        sh->SetInt("maskSampler",     3);
                        bindTexture(1, grp.texSlot[1] ? toTextureHandle(grp.texSlot[1]) : normalTex);
                        bindTexture(2, grp.texSlot[2] ? toTextureHandle(grp.texSlot[2])
                                                      : (grp.texSlot[1] ? toTextureHandle(grp.texSlot[1]) : normalTex));
                        bindTexture(3, grp.texSlot[3] ? toTextureHandle(grp.texSlot[3]) : blackTex);
                        bindSampler(1, gSamplerRepeat);
                        bindSampler(2, gSamplerRepeat);
                        bindSampler(3, gSamplerRepeat);
                    } else {
                        sh->SetInt("bumpMapSampler",  0);
                        sh->SetInt("bumpMap1Sampler", 1);
                        sh->SetInt("maskSampler",     2);
                        bindTexture(0, grp.texSlot[1] ? toTextureHandle(grp.texSlot[1]) : normalTex);
                        bindTexture(1, grp.texSlot[2] ? toTextureHandle(grp.texSlot[2])
                                                      : (grp.texSlot[1] ? toTextureHandle(grp.texSlot[1]) : normalTex));
                        bindTexture(2, grp.texSlot[3] ? toTextureHandle(grp.texSlot[3]) : blackTex);
                        bindSampler(0, gSamplerRepeat);
                        bindSampler(1, gSamplerRepeat);
                        bindSampler(2, gSamplerRepeat);
                    }

                    glMultiDrawElementsIndirect(
                        GL_TRIANGLES, GL_UNSIGNED_INT,
                        reinterpret_cast<const void*>(
                            static_cast<intptr_t>(grp.cmdOffset * sizeof(DrawCommand))),
                        static_cast<GLsizei>(grp.cmdCount), 0);

                    frameDrawCalls_ += static_cast<int>(grp.cmdCount);
                    renderedGroups++;
                }

                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
                frameProfile_.geomSLRender = gMs(tSLRender);
            }

            setGeometryDrawBufferLayout(false);
            for (int i = 0; i <= 3; ++i) {
                if (device_) device_->BindSampler(nullptr, i);
                else         glBindSampler(i, 0);
            }
            glDisable(GL_CULL_FACE);

            if (optRenderLOG)
                NC::LOGGING::Log("[GEOMETRY] SimpleLayer gbuffer groups: ", slGBufGroups_.size(),
                                 " cmds: ", slGBufCmds_.size(),
                                 " objs: ", slGBufWorldMats_.size());
        }
        frameProfile_.geomSL = gMs(tSL);
    }
    markGeomGpuTimestamp(kGpuGeomSimpleLayerPhase, false);

        markGeomGpuTimestamp(kGpuGeomEnvironmentPhase, true);
        auto tEnv = GClock::now();
        if (!kDisableEnvPass && !environmentDraws.empty()) {
            if (device_) {
                device_->ApplyRenderState((disableFaceCulling ? cachedEnvStateNoCull_ : cachedEnvState_).get());
            } else {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glEnable(GL_STENCIL_TEST);
                glStencilMask(0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                if (disableFaceCulling) {
                    glDisable(GL_CULL_FACE);
                } else {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                }
            }

            if (kForceEnvironmentThroughStandardGeometryPath) {
                auto stdShader = shader;
                if (!stdShader) {
                    std::cerr << "[GEOMETRY] Shader " << geometryShaderName << " not found for environment fallback\n";
                } else {
                    setGeometryDrawBufferLayout(deferredShaderUsesPackedGBuffer_);
                    stdShader->Use();
                    stdShader->SetMat4(U_PROJECTION, camera_.getProjectionMatrix());
                    stdShader->SetMat4(U_VIEW, camera_.getViewMatrix());
                    stdShader->SetMat4(U_TEXTURE_TRANSFORM0, glm::mat4(1.0f));
                    stdShader->SetFloat(U_MAT_EMISSIVE_INTENSITY, 0.0f);
                    stdShader->SetFloat(U_MAT_SPECULAR_INTENSITY, 0.0f);
                    stdShader->SetFloat(U_MAT_SPECULAR_POWER, 32.0f);
                    stdShader->SetInt(U_USE_SKINNING, 0);
                    stdShader->SetInt(U_USE_INSTANCING, 1);
                    stdShader->SetInt(U_ALPHA_TEST, 0);
                    stdShader->SetFloat(U_ALPHA_CUTOFF, 0.5f);
                    stdShader->SetInt(U_DIFF_MAP0, 0);
                    stdShader->SetInt(U_SPEC_MAP0, 1);
                    stdShader->SetInt(U_BUMP_MAP0, 2);
                    stdShader->SetInt(U_EMSV_MAP0, 3);
                    stdShader->SetInt(U_TWO_SIDED, 0);
                    stdShader->SetInt(U_IS_FLAT_NORMAL, 0);
                    if (depthScaleLoc >= 0) {
                        glUniform1f(depthScaleLoc, 1.0f);
                    }
                    if (normalMatrixLoc >= 0) {
                        const glm::mat3 viewOnlyNormalMatrix = glm::transpose(glm::inverse(glm::mat3(camera_.getViewMatrix())));
                        glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, &viewOnlyNormalMatrix[0][0]);
                    }
                    if (useNonInstancedNormalMatrixPath) {
                        stdShader->SetInt(U_USE_INSTANCING, 0);
                        MegaBuffer::instance().bind();
                        int renderedEnv = 0;
                        for (const auto& dc : environmentDraws) {
                            if (!dc.mesh || dc.disabled) continue;
                            if (!isDrawVisible(dc, frameFrustum_, true)) continue;

                            stdShader->SetMat4(U_MODEL, dc.worldMatrix);
                            if (normalMatrixLoc >= 0) {
                                const glm::mat3 normalMatrix =
                                    glm::transpose(glm::inverse(glm::mat3(camera_.getViewMatrix() * dc.worldMatrix)));
                                glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, &normalMatrix[0][0]);
                            }
                            stdShader->SetFloat(U_MAT_EMISSIVE_INTENSITY, dc.cachedMatEmissiveIntensity);
                            stdShader->SetFloat(U_MAT_SPECULAR_INTENSITY, dc.cachedMatSpecularIntensity);
                            stdShader->SetFloat(U_MAT_SPECULAR_POWER, dc.cachedMatSpecularPower);
                            stdShader->SetInt(U_RECEIVES_DECALS, dc.receivesDecals ? 1 : 0);
                            stdShader->SetInt(U_ALPHA_TEST, dc.alphaTest ? 1 : 0);
                            stdShader->SetFloat(U_ALPHA_CUTOFF, dc.alphaCutoff);
                            stdShader->SetInt(U_TWO_SIDED, dc.cachedTwoSided);
                            stdShader->SetInt(U_IS_FLAT_NORMAL, dc.cachedIsFlatNormal);

                            const bool hasSpecMap = dc.cachedHasSpecMap;
                            bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
                            bindTexture(1, hasSpecMap ? (dc.tex[1] ? toTextureHandle(dc.tex[1]) : blackTex) : blackTex);
                            bindTexture(2, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
                            bindTexture(3, dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex);
                            bindSampler(0, gSamplerRepeat);
                            bindSampler(1, gSamplerRepeat);
                            bindSampler(2, gSamplerRepeat);
                            bindSampler(3, gSamplerRepeat);
                            glStencilFunc(GL_ALWAYS, dc.receivesDecals ? 1 : 0, 0xFF);

                            if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                                auto& g = dc.mesh->groups[dc.group];
                                glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                                    reinterpret_cast<void*>(static_cast<intptr_t>((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t))));
                            } else {
                                for (const auto& g : dc.mesh->groups) {
                                    glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                                        reinterpret_cast<void*>(static_cast<intptr_t>((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t))));
                                }
                            }
                            ++renderedEnv;
                        }
                        frameDrawCalls_ += renderedEnv;
                        if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Environment draws via deferred non-instanced normalMatrix path: ", renderedEnv);
                    } else {
                        environmentBatchSystem_.cullGeneric(environmentDraws, frameFrustum_, 3, 4);

                        MegaBuffer::instance().bind();
                        environmentBatchSystem_.flush(samplerRepeat_abstracted.get(), 4, deferredProgram);

                        const int environmentBatches = static_cast<int>(environmentBatchSystem_.activeBatches().size());
                        frameDrawCalls_ += environmentBatches;
                        if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Environment draws via standard path: batches=", environmentBatches, " objs=", environmentDraws.size());
                    }
                }
            } else {
                setGeometryDrawBufferLayout(false);
                auto envShader = shaderManager->GetShader("environment");
                if (!envShader) {
                    std::cerr << "Shader environment not found\n";
                } else {
                    envShader->Use();
                    envShader->SetMat4("projection", camera_.getProjectionMatrix());
                    envShader->SetMat4("view", camera_.getViewMatrix());
                    envShader->SetMat4("textureTransform0", glm::mat4(1.0f));
                    envShader->SetInt("UseSkinning", 0);
                    envShader->SetInt("UseInstancing", 0);
                    envShader->SetVec3("eyePos", camera_.getPosition());
                    envShader->SetInt("DiffMap0", 0);
                    envShader->SetInt("SpecMap0", 1);
                    envShader->SetInt("BumpMap0", 2);
                    envShader->SetInt("EmsvMap0", 3);
                    envShader->SetInt("CubeMap0", 4);

                    int renderedEnv = 0;
                    for (const auto& dc : environmentDraws) {
                        if (!dc.mesh) continue;
                        if (dc.disabled) continue;
                        if (!isDrawVisible(dc, frameFrustum_, true)) continue;

                        envShader->SetMat4("model", dc.worldMatrix);
                        envShader->SetFloat("Intensity0", dc.cachedIntensity0);
                        envShader->SetFloat("MatEmissiveIntensity", dc.cachedMatEmissiveIntensity);
                        envShader->SetFloat("MatSpecularIntensity", dc.cachedMatSpecularIntensity);
                        envShader->SetFloat("MatSpecularPower", dc.cachedMatSpecularPower);
                        envShader->SetInt("DisableViewDependentReflection", kDisableViewDependentReflections ? 1 : 0);
                        envShader->SetInt("ReceivesDecals", dc.receivesDecals ? 1 : 0);
                        envShader->SetInt("alphaTest", dc.alphaTest ? 1 : 0);
                        envShader->SetFloat("alphaCutoff", dc.alphaCutoff);
                        envShader->SetInt("twoSided", dc.cachedTwoSided);
                        envShader->SetInt("isFlatNormal", dc.cachedIsFlatNormal);
                        glStencilFunc(GL_ALWAYS, dc.receivesDecals ? 1 : 0, 0xFF);

                        const bool hasSpecMap = dc.cachedHasSpecMap;

                        bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
                        bindTexture(1, hasSpecMap ? (dc.tex[1] ? toTextureHandle(dc.tex[1]) : blackTex) : blackTex);
                        bindTexture(2, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
                        bindTexture(3, dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex);
                        glActiveTexture(GL_TEXTURE4);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, dc.tex[9] ? toTextureHandle(dc.tex[9]) : blackCubeTex);

                        bindSampler(0, gSamplerRepeat);
                        bindSampler(1, gSamplerRepeat);
                        bindSampler(2, gSamplerRepeat);
                        bindSampler(3, gSamplerRepeat);
                        bindSampler(4, 0);

                        if (dc.group >= 0) dc.mesh->drawGroup(dc.group);
                        else dc.mesh->drawMulti();
                        renderedEnv++;
                    }

                    frameDrawCalls_ += renderedEnv;
                    if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Environment draws: ", renderedEnv);

                    for (int i = 0; i < 6; i++) bindSampler(i, 0);
                    glActiveTexture(GL_TEXTURE4);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                    glActiveTexture(GL_TEXTURE0);
                    glBindVertexArray(0);
                }
            }
            checkGLError("After environment (geometry pass)");
        }
        frameProfile_.geomEnv = gMs(tEnv);
        markGeomGpuTimestamp(kGpuGeomEnvironmentPhase, false);

    setGeometryDrawBufferLayout(false);
    if (optRenderLOG) std::cout << "[GEOMETRY] Complete\n";
};

    auto& packedCompatPass = geometryGraph->addPass("PackedDeferredCompat");
    packedCompatPass.reads = {"gNormalDepthEncoded", "gNormalDepthPacked", "gPositionWS"};
    packedCompatPass.writes = {"gPositionVS", "gNormalDepthCompat"};
    packedCompatPass.clearColorBuffer = false;
    packedCompatPass.clearColor = glm::vec4(0, 0, 0, 0);
    packedCompatPass.depthTest = false;
    packedCompatPass.depthWrite = false;
    packedCompatPass.cullFace = false;
    packedCompatPass.shouldSkip = [hasSceneColorProducerInputs]() {
        return !hasSceneColorProducerInputs();
    };
    packedCompatPass.execute = [this]() {
        if (!deferredShaderUsesPackedGBuffer_) {
            deferredPackedCompatReady_ = true;
            return;
        }

        deferredPackedCompatReady_ = false;

        const GLuint packedNormalDepth = GetFrameGraphTexture(geometryGraph, "gNormalDepthEncoded");
        const GLuint baseNormalDepth = GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
        const GLuint gPosWS = GetFrameGraphTexture(geometryGraph, "gPositionWS");
        if (packedNormalDepth == 0 || baseNormalDepth == 0 || gPosWS == 0) {
            static bool warnedMissingCompatInputs = false;
            if (!warnedMissingCompatInputs) {
                std::cerr << "[GEOMETRY][Compat] Missing packed G-buffer inputs, lighting compatibility disabled.\n";
                warnedMissingCompatInputs = true;
            }
            return;
        }

        const GLuint compatProgram = GetPackedDeferredCompatProgram();
        if (compatProgram == 0) {
            static bool warnedCompatProgram = false;
            if (!warnedCompatProgram) {
                std::cerr << "[GEOMETRY][Compat] Failed to create compatibility shader, lighting compatibility disabled.\n";
                warnedCompatProgram = true;
            }
            return;
        }

        constexpr GLfloat kZeroColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, kZeroColor);
        glClearBufferfv(GL_COLOR, 1, kZeroColor);

        glUseProgram(compatProgram);

        static GLuint cachedCompatProgram = 0;
        static GLint locPackedNormalDepth = -1, locPositionWS = -1, locBaseNormalDepth = -1;
        static GLint locView = -1, locInvView = -1, locInvProjection = -1, locInvViewRot = -1;
        if (cachedCompatProgram != compatProgram) {
            cachedCompatProgram = compatProgram;
            locPackedNormalDepth = glGetUniformLocation(compatProgram, "gPackedNormalDepth");
            locPositionWS        = glGetUniformLocation(compatProgram, "gPositionWS");
            locBaseNormalDepth   = glGetUniformLocation(compatProgram, "gBaseNormalDepth");
            locView              = glGetUniformLocation(compatProgram, "view");
            locInvView           = glGetUniformLocation(compatProgram, "invView");
            locInvProjection     = glGetUniformLocation(compatProgram, "invProjection");
            locInvViewRot        = glGetUniformLocation(compatProgram, "invViewRot");
            glUniform1i(locPackedNormalDepth, 0);
            glUniform1i(locPositionWS,        1);
            glUniform1i(locBaseNormalDepth,   2);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, packedNormalDepth);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gPosWS);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, baseNormalDepth);

        const glm::mat4 view = camera_.getViewMatrix();
        const glm::mat4 invView = glm::inverse(view);
        const glm::mat4 invProjection = glm::inverse(camera_.getProjectionMatrix());
        const glm::mat3 invViewRot = glm::mat3(invView);
        glUniformMatrix4fv(locView,          1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(locInvView,       1, GL_FALSE, &invView[0][0]);
        glUniformMatrix4fv(locInvProjection, 1, GL_FALSE, &invProjection[0][0]);
        glUniformMatrix3fv(locInvViewRot,    1, GL_FALSE, &invViewRot[0][0]);

        glBindVertexArray(screenVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        deferredPackedCompatReady_ = true;
    };

geometryGraph->compile();

    auto& decalPass = decalGraph->addPass("Decals");
    decalPass.depthTest = true;
    decalPass.depthWrite = false;
    decalPass.cullFace = true;
    decalPass.cullMode = CullMode::Back;
    decalPass.blend = true;
    decalPass.blendSrc = BlendFactor::SrcAlpha;
    decalPass.blendDst = BlendFactor::OneMinusSrcAlpha;
    decalPass.externalFBO = true;
    decalPass.bindExternalFBO = [this]() {
        if (gDepthDecalFBO != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, gDepthDecalFBO);
        }
    };
    decalPass.shouldSkip = [this, hasDeferredGeometryInputs]() {
        return kDisableDecalPass || decalDraws.empty() || !hasDeferredGeometryInputs();
    };
    decalPass.execute = [this]() {
        if (kDisableDecalPass) {
            if (optRenderLOG) std::cout << "[DECALS] Disabled by kDisableDecalPass\n";
            return;
        }
        if (decalDraws.empty()) {
            if (optRenderLOG) std::cout << "[DECALS] No decals to render\n";
            return;
        }

        if (optRenderLOG) std::cout << "[DECALS] Begin decal pass\n";
        GLuint gPosWS = GetFrameGraphTexture(geometryGraph, "gPositionWS");
        GLuint gAlbedoSpec = GetFrameGraphTexture(geometryGraph, "gAlbedoSpec");
        GLuint gNormalDepth = 0;
        if (deferredShaderUsesPackedGBuffer_) {
            gNormalDepth = GetFrameGraphTexture(geometryGraph, "gNormalDepthCompat");
            if (gNormalDepth == 0) {
                // Fallback keeps decals available if compat output is unavailable.
                gNormalDepth = GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
            }
        } else {
            gNormalDepth = GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
        }
        GLuint gDepth = GetFrameGraphTexture(geometryGraph, "gDepth");
        if (optRenderLOG) std::cout << "[DECALS] Geometry textures: PosWS=" << gPosWS << " Albedo=" << gAlbedoSpec << " Depth=" << gDepth << "\n";
        if (gPosWS == 0 || gAlbedoSpec == 0 || gDepth == 0 || gNormalDepth == 0) {
            std::cerr << "[DECALS] Missing G-buffer inputs, skipping decal pass.\n";
            return;
        }

        static GLuint decalFBOLastAlbedo = 0, decalFBOLastDepth = 0;
        const bool decalFBONeedsSetup = (gDepthDecalFBO == 0) ||
                                        (gAlbedoSpec != decalFBOLastAlbedo) ||
                                        (gDepth != decalFBOLastDepth);
        if (gDepthDecalFBO == 0) {
            glGenFramebuffers(1, gDepthDecalFBO.put());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, gDepthDecalFBO);

        if (decalFBONeedsSetup) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gAlbedoSpec, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);
            {
                GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
                glDrawBuffers(1, bufs);
            }
            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "[DECALS] ERROR: FBO incomplete 0x" << std::hex << fboStatus << std::dec << "\n";
                return;
            }
            decalFBOLastAlbedo = gAlbedoSpec;
            decalFBOLastDepth = gDepth;
        }

        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        if (FaceCullingDisabled()) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0x00);


        bool useBindlessDecals = false;
        auto decalShaderBindless = shaderManager->GetShader("NDEVCdecal_mesh_bindless");
        auto decalShaderStd = shaderManager->GetShader("NDEVCdecal_mesh");
        decltype(decalShaderStd) decalShader;
        if (TextureServer::sBindlessSupported && decalShaderBindless && decalMaterialSSBO_.valid()) {
            decalShader = decalShaderBindless;
            useBindlessDecals = true;
        } else {
            decalShader = decalShaderStd;
        }

        if (!decalShader) {
             std::cerr << "    [ERROR] Decal shader not found! Skipping decals.\n";
        } else {
            decalShader->Use();

            glm::mat4 viewMatrix = camera_.getViewMatrix();
            glm::mat4 projMatrix = camera_.getProjectionMatrix();

            decalShader->SetMat4("projection", projMatrix);
            decalShader->SetMat4("view", viewMatrix);
            decalShader->SetVec2("screenSize", glm::vec2((float)width, (float)height));
            decalShader->SetInt("gPositionWS", 0);
            decalShader->SetInt("gNormalDepthPacked", 1);
            if (!useBindlessDecals) {
                decalShader->SetInt("DiffMap0", 2);
                decalShader->SetInt("EmsvMap0", 3);
                decalShader->SetFloat("DecalScale", 1.0f);
                decalShader->SetInt("DecalDiffuseMode", 0);
            }

            this->bindTexture(0, gPosWS);
            glBindSampler(0, 0);
            this->bindTexture(1, gNormalDepth);
            glBindSampler(1, 0);

            if (useBindlessDecals) {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, decalMaterialSSBO_);
            }

            decalBatchSystem_.cullDecals(decalDraws);
            MegaBuffer::instance().bind();
            decalBatchSystem_.flushDecals(samplerRepeat_abstracted.get(), samplerClamp_abstracted.get(), useBindlessDecals);

            const int decalBatches = static_cast<int>(decalBatchSystem_.activeBatches().size());
            frameDrawCalls_ += useBindlessDecals ? 1 : decalBatches;
            if (optRenderLOG) NC::LOGGING::Log("[DECALS] Submitted: batches=", decalBatches,
                " objs=", static_cast<int>(decalDraws.size()),
                " bindless=", useBindlessDecals ? 1 : 0);

            glBindVertexArray(0);
            glBindSampler(0, 0);
            glBindSampler(1, 0);
            if (!useBindlessDecals) {
                glBindSampler(2, 0);
                glBindSampler(3, 0);
            }

            checkGLError("After decals");
        }

        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        checkGLError("After decal cleanup");
        if (optRenderLOG) std::cout << "[DECALS] Complete\n";
    };

    decalGraph->compile();

    auto& lightingPass = lightingGraph->addPass("Lighting");
    lightingPass.writes = {"lightBuffer"};
    lightingPass.depthTest = false;
    lightingPass.depthWrite = false;
    lightingPass.cullFace = false;
    lightingPass.clearColorBuffer = true;
    lightingPass.clearColor = glm::vec4(0, 0, 0, 0);
    lightingPass.shouldSkip = [hasSceneColorProducerInputs]() {
        return !hasSceneColorProducerInputs();
    };
    lightingPass.execute = [this]() {
        if (kDisableLightingPass) {
            if (optRenderLOG) std::cout << "[LIGHTING] Disabled by kDisableLightingPass\n";
            return;
        }
        if (optRenderLOG) std::cout << "[LIGHTING] Begin lighting pass\n";
        clearError("Lighting::PassStart");
        GLuint gNormalDepth = deferredShaderUsesPackedGBuffer_
            ? GetFrameGraphTexture(geometryGraph, "gNormalDepthCompat")
            : GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
        GLuint gPosWS = GetFrameGraphTexture(geometryGraph, "gPositionWS");
        if (optRenderLOG) std::cout << "[LIGHTING] G-buffer textures: Normal=" << gNormalDepth << " PosWS=" << gPosWS << "\n";
        if (gNormalDepth == 0 || gPosWS == 0) {
            return;
        }

        auto shader = shaderManager->GetShader("lighting");
        if (!shader) {
            std::cerr << "[LIGHTING] ERROR: Shader not found\n";
            return;
        }
        shader->PrecacheUniform(U_CAMERA_POS, "CameraPos");
        shader->PrecacheUniform(U_LIGHT_DIR_WS, "LightDirWS");
        shader->PrecacheUniform(U_LIGHT_COLOR, "LightColor");
        shader->PrecacheUniform(U_AMBIENT_COLOR, "AmbientColor");
        shader->PrecacheUniform(U_VIEW, "view");
        shader->PrecacheUniform(U_GNORMAL_DEPTH_PACKED, "gNormalDepthPacked");
        shader->PrecacheUniform(U_GPOSITION_WS, "gPositionWS");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE0, "shadowMapCascade0");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE1, "shadowMapCascade1");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE2, "shadowMapCascade2");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE3, "shadowMapCascade3");
        shader->PrecacheUniform(U_NUM_CASCADES, "numCascades");
        shader->Use();
        shader->SetVec3(U_CAMERA_POS, camera_.getPosition());
        shader->SetMat4(U_VIEW, camera_.getViewMatrix());
        constexpr UniformID kShadowCascadeUniforms[] = {
            U_SHADOW_MAP_CASCADE0,
            U_SHADOW_MAP_CASCADE1,
            U_SHADOW_MAP_CASCADE2,
            U_SHADOW_MAP_CASCADE3
        };
        constexpr int kShadowCascadeUniformCount =
            static_cast<int>(sizeof(kShadowCascadeUniforms) / sizeof(kShadowCascadeUniforms[0]));
        const bool shadowsEnabled = !kDisableShadows;
        const int shadowCascadeCount = shadowsEnabled
            ? ((NUM_CASCADES < kShadowCascadeUniformCount) ? NUM_CASCADES : kShadowCascadeUniformCount)
            : 0;
        if (shadowsEnabled && shadowCascadeCount > 0) {
            shader->SetMat4Array("lightSpaceMatrices", lightSpaceMatrices, shadowCascadeCount);
            for (int i = 0; i < shadowCascadeCount + 1; ++i) {
                shader->SetFloat(("cascadeSplits[" + std::to_string(i) + "]").c_str(), cascadeSplits[i]);
            }
        }
        bindTexture(0, gNormalDepth);
        bindTexture(1, gPosWS);

        static GLuint cachedLightingProgram = 0;
        const GLuint lightingProgram = shader->GetNativeHandle()
            ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle()) : 0;
        if (cachedLightingProgram != lightingProgram) {
            cachedLightingProgram = lightingProgram;
            shader->SetInt(U_GNORMAL_DEPTH_PACKED, 0);
            shader->SetInt(U_GPOSITION_WS, 1);
            shader->SetInt(U_NUM_CASCADES, shadowCascadeCount);
            shader->SetInt("DisableShadows", shadowsEnabled ? 0 : 1);
            shader->SetInt("DisableViewDependentSpecular", kDisableViewDependentSpecular ? 1 : 0);
            shader->SetVec3(U_LIGHT_DIR_WS, kLightDirToSun);
            shader->SetVec3(U_LIGHT_COLOR, glm::vec3(1.08f, 0.90f, 0.68f));
            shader->SetVec3(U_AMBIENT_COLOR, glm::vec3(0.18f, 0.21f, 0.24f));
        }
        if (shadowsEnabled && shadowCascadeCount > 0) {
            for (int i = 0; i < shadowCascadeCount; ++i) {
                bindTexture(3 + i, shadowMapCascades[i]);
                if (device_ && samplerShadow_abstracted) {
                    device_->BindSampler(samplerShadow_abstracted.get(), 3 + i);
                } else {
                    glBindSampler(3 + i, gSamplerShadow);
                }
                shader->SetInt(kShadowCascadeUniforms[i], 3 + i);
            }
        }
        clearError("Lighting::PreDraw");
        glBindVertexArray(screenVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        clearError("Lighting::PostDraw");
        if (shadowsEnabled && shadowCascadeCount > 0) {
            for (int i = 3; i < 3 + shadowCascadeCount; ++i) {
                if (device_) {
                    device_->BindSampler(nullptr, i);
                } else {
                    glBindSampler(i, 0);
                }
            }
        }
        clearError("Lighting::AfterDirectional");
        if (optRenderLOG) std::cout << "[LIGHTING] Complete\n";
    };

    auto& pointLightPass = lightingGraph->addPass("PointLights");
    pointLightPass.writes = {"lightBuffer"};
    pointLightPass.depthTest = false;
    pointLightPass.depthWrite = false;
    pointLightPass.cullFace = true;
    pointLightPass.cullMode = CullMode::Front;
    pointLightPass.blend = true;
    pointLightPass.blendSrc = BlendFactor::One;
    pointLightPass.blendDst = BlendFactor::One;
    pointLightPass.shouldSkip = [hasSceneColorProducerInputs]() {
        return !hasSceneColorProducerInputs();
    };
    pointLightPass.execute = [this]() {
        if (optRenderLOG) std::cout << "[POINTLIGHTS] Begin point light pass (" << pointLights.size() << " lights)\n";
        if (kDisablePointLightPass) {
            if (optRenderLOG) std::cout << "[POINTLIGHTS] Disabled by kDisablePointLightPass\n";
            return;
        }
        if (pointLights.empty()) {
            if (optRenderLOG) std::cout << "[POINTLIGHTS] No lights, skipping\n";
            return;
        }

        GLuint gNormalDepth = deferredShaderUsesPackedGBuffer_
            ? GetFrameGraphTexture(geometryGraph, "gNormalDepthCompat")
            : GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
        GLuint gPosWS = GetFrameGraphTexture(geometryGraph, "gPositionWS");
        if (gNormalDepth == 0 || gPosWS == 0) {
            return;
        }

        auto shader = shaderManager->GetShader("pointLight");
        if (!shader) return;
        shader->PrecacheUniform(U_CAMERA_POS, "CameraPos");
        shader->PrecacheUniform(U_SCREEN_SIZE, "screenSize");
        shader->PrecacheUniform(U_GNORMAL_DEPTH_PACKED, "gNormalDepthPacked");
        shader->PrecacheUniform(U_GPOSITION_WS, "gPositionWS");
        shader->PrecacheUniform(U_PROJECTION, "projection");
        shader->PrecacheUniform(U_VIEW, "view");
        shader->PrecacheUniform(U_USE_INSTANCED_POINT_LIGHTS, "UseInstancedPointLights");
        shader->Use();

        glm::mat4 projection = camera_.getProjectionMatrix();
        glm::mat4 view = camera_.getViewMatrix();
        shader->SetMat4(U_PROJECTION, projection);
        shader->SetMat4(U_VIEW, view);

        glm::vec3 camPos = camera_.getPosition();
        shader->SetVec3(U_CAMERA_POS, camPos);
        shader->SetVec2(U_SCREEN_SIZE, glm::vec2((float)width, (float)height));
        shader->SetInt(U_USE_INSTANCED_POINT_LIGHTS, 1);
        shader->SetInt("DisableViewDependentSpecular", kDisableViewDependentSpecular ? 1 : 0);

        bindTexture(0, gNormalDepth);
        shader->SetInt(U_GNORMAL_DEPTH_PACKED, 0);
        bindTexture(1, gPosWS);
        shader->SetInt(U_GPOSITION_WS, 1);

        struct GPUPointLightInstance {
            glm::vec4 posRange;
            glm::vec4 color;
        };
        std::vector<GPUPointLightInstance> gpuLights;
        gpuLights.reserve(pointLights.size());
        for (const auto& light : pointLights) {
            GPUPointLightInstance inst{};
            inst.posRange = glm::vec4(light.position, std::max(0.001f, light.range));
            inst.color = light.color;
            gpuLights.push_back(inst);
        }

        if (pointLightInstanceVBO != 0 && !gpuLights.empty()) {
            if (gpuLights.size() > pointLightInstanceCapacity) {
                size_t newCapacity = pointLightInstanceCapacity ? pointLightInstanceCapacity : 1;
                while (newCapacity < gpuLights.size()) {
                    newCapacity *= 2;
                }
                glBindBuffer(GL_ARRAY_BUFFER, pointLightInstanceVBO);
                glBufferData(GL_ARRAY_BUFFER, newCapacity * sizeof(GPUPointLightInstance), nullptr, GL_DYNAMIC_DRAW);
                pointLightInstanceCapacity = newCapacity;
            }

            glBindBuffer(GL_ARRAY_BUFFER, pointLightInstanceVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, gpuLights.size() * sizeof(GPUPointLightInstance), gpuLights.data());
            glBindVertexArray(sphereVAO);
            glDrawElementsInstanced(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(gpuLights.size()));
            glBindVertexArray(0);
        }
        clearError("PointLights::AfterPass");
        if (optRenderLOG) std::cout << "[POINTLIGHTS] Complete\n";
    };

    auto& compositionPass = lightingGraph->addPass("Composition");
compositionPass.writes = {"sceneColor"};
compositionPass.depthTest = false;
compositionPass.depthWrite = false;
compositionPass.cullFace = false;
compositionPass.shouldSkip = [hasSceneColorProducerInputs]() {
    return !hasSceneColorProducerInputs();
};
compositionPass.execute = [this]() {
    if (optRenderLOG) std::cout << "[COMPOSITION] Begin composition pass\n";
    const bool forceCompatibilityUnlit = deferredShaderUsesPackedGBuffer_ && !deferredPackedCompatReady_;
    const bool disableLightingForPass = kDisableLighting || forceCompatibilityUnlit;
    GLuint gAlbedo = GetFrameGraphTexture(geometryGraph, "gAlbedoSpec");
    GLuint gDepthTex = GetFrameGraphTexture(geometryGraph, "gDepth");
    GLuint gNormalDepth = deferredShaderUsesPackedGBuffer_
        ? GetFrameGraphTexture(geometryGraph, "gNormalDepthCompat")
        : GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
    GLuint gEmissive = GetFrameGraphTexture(geometryGraph, "gEmissive");
    GLuint lightBuf = GetFrameGraphTexture(lightingGraph, "lightBuffer");
    GLuint sceneCol = GetFrameGraphTexture(lightingGraph, "sceneColor");
    if (optRenderLOG) std::cout << "[COMPOSITION] Writing to sceneColor=" << sceneCol << "\n";
    if (optRenderLOG) std::cout << "[COMPOSITION] Reading: Albedo=" << gAlbedo << " Light=" << lightBuf << " Depth=" << gDepthTex << "\n";

    if (kDisableCompositionPass) {
        if (optRenderLOG) std::cout << "[COMPOSITION] Disabled by kDisableCompositionPass (blit gAlbedoSpec)\n";
        auto blit = shaderManager->GetShader("blit");
        if (!blit) {
            std::cerr << "[COMPOSITION] ERROR: blit shader not found\n";
            return;
        }
        blit->Use();
        bindTexture(0, gAlbedo);
        blit->SetInt("tex", 0);
        glBindVertexArray(screenVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        return;
    }

    if (forceCompatibilityUnlit || gNormalDepth == 0 || gDepthTex == 0 || lightBuf == 0) {
        auto blit = shaderManager->GetShader("blit");
        if (!blit) {
            std::cerr << "[COMPOSITION] ERROR: blit shader not found for packed compatibility\n";
            return;
        }
        blit->Use();
        bindTexture(0, gAlbedo);
        blit->SetInt("tex", 0);
        glBindVertexArray(screenVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        return;
    }

    auto shader = shaderManager->GetShader("lightComposition");
    if (!shader) {
        std::cerr << "[COMPOSITION] ERROR: Shader not found\n";
        return;
    }
    shader->PrecacheUniform(U_GALBEDO_SPEC, "gAlbedoSpec");
    shader->PrecacheUniform(U_LIGHT_BUFFER_TEX, "lightBufferTex");
    shader->PrecacheUniform(U_GEMISSIVE_TEX, "gEmissiveTex");
    shader->PrecacheUniform(U_GNORMAL_DEPTH_PACKED, "gNormalDepthPacked");
    shader->PrecacheUniform(U_INV_PROJECTION, "invProjection");
    shader->Use();

    bindTexture(0, gAlbedo);
    shader->SetInt(U_GALBEDO_SPEC, 0);
    bindTexture(1, lightBuf);
    shader->SetInt(U_LIGHT_BUFFER_TEX, 1);
    bindTexture(2, gEmissive);
    shader->SetInt(U_GEMISSIVE_TEX, 2);
    bindTexture(3, gNormalDepth);
    shader->SetInt(U_GNORMAL_DEPTH_PACKED, 3);
    bindTexture(4, gDepthTex);
    shader->SetInt("gDepthTex", 4);
    shader->SetMat4(U_INV_PROJECTION, glm::inverse(camera_.getProjectionMatrix()));
    shader->SetInt("DisableLighting", disableLightingForPass ? 1 : 0);
    shader->SetInt("DisableFog", kDisableFog ? 1 : 0);
    shader->SetVec4("fogColor", glm::vec4(0.5f, 0.5f, 0.5f, 0.0f));
    shader->SetVec4("fogDistances", glm::vec4(0.0f, 10000.0f, 0.0f, 1.0f));

    glBindVertexArray(screenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    if (gEnableGLErrorChecking) {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) std::cerr << "[COMPOSITION] Error: 0x" << std::hex << err << std::dec << "\n";
    }
    if (optRenderLOG) std::cout << "[COMPOSITION] Complete\n";
};

    lightingGraph->compile();

    auto& forwardPass = particleGraph->addPass("Forward");
    forwardPass.externalFBO = true;
    forwardPass.depthTest = true;
    forwardPass.depthWrite = false;
    forwardPass.cullFace = false;
    forwardPass.blend = true;
    forwardPass.blendSrc = BlendFactor::SrcAlpha;
    forwardPass.blendDst = BlendFactor::OneMinusSrcAlpha;
    forwardPass.bindExternalFBO = [this]() {
        if (sceneFBO != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        }
    };
    forwardPass.shouldSkip = [hasForwardPassInputs]() {
        return !hasForwardPassInputs();
    };
    forwardPass.execute = [this]() {
        if (kDisableForwardPass) {
            if (optRenderLOG) std::cout << "[FORWARD] Disabled by kDisableForwardPass\n";
            return;
        }
        if (optRenderLOG) std::cout << "[FORWARD] Begin forward/particle pass\n";

        GLuint sceneColorTex = GetFrameGraphTexture(lightingGraph, "sceneColor");
        GLuint sceneDepthTex = GetFrameGraphTexture(geometryGraph, "gDepth");
        GLuint gPosWS = GetFrameGraphTexture(geometryGraph, "gPositionWS");
        GLuint lightBuf = GetFrameGraphTexture(lightingGraph, "lightBuffer");
        if (optRenderLOG) std::cout << "[FORWARD] Scene textures: Color=" << sceneColorTex << " Depth=" << sceneDepthTex << "\n";

        if (!sceneFBO) {
            glGenFramebuffers(1, sceneFBO.put());
            if (optRenderLOG) std::cout << "[FORWARD] Created FBO " << sceneFBO << "\n";
        }
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        if (optRenderLOG || gEnableGLErrorChecking) {
            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "[FORWARD] ERROR: FBO incomplete 0x" << std::hex << fboStatus << std::dec << "\n";
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return;
            }
        }

        glViewport(0, 0, width, height);
        MegaBuffer::instance().bind();
        glm::mat4 viewMatrix = camera_.getViewMatrix();
        glm::mat4 projMatrix = camera_.getProjectionMatrix();
        glm::mat4 viewProj = projMatrix * viewMatrix;
        glm::mat4 invView = glm::inverse(viewMatrix);
        glm::vec3 eyePos = camera_.getPosition();
        glm::vec3 viewDir = camera_.getForward();
        glm::vec2 invViewport(1.0f / width, 1.0f / height);
        const Camera::Frustum& frustum = frameFrustum_;
        const bool disableFrustumCulling = FrustumCullingDisabled();
        const bool disableFaceCulling = FaceCullingDisabled();
        const float particleTime = static_cast<float>(glfwGetTime());

        static NDEVC::GL::GLBufHandle forwardInstanceMatrixSSBO;
        static size_t forwardInstanceMatrixSSBOCapacity = 0;
        static size_t forwardSsboOffsetAlignment = 0;
        static size_t forwardRingOffset = 0;
        static bool forwardRingOrphaned = false;
        static NDEVC::GL::GLBufHandle forwardMaterialIndexSSBO;
        static size_t forwardMaterialIndexSSBOCapacity = 0;
        static NDEVC::GL::GLBufHandle forwardIndirectBuffer;
        static size_t forwardIndirectBufferCapacity = 0;
        forwardRingOffset = 0;
        forwardRingOrphaned = false;
        int forwardFlushCount = 0;

        auto uploadForwardInstanceMatrices = [&](const std::vector<glm::mat4>& matrices) -> bool {
            if (matrices.empty()) return false;
            if (forwardSsboOffsetAlignment == 0) {
                GLint align = 256;
                glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &align);
                if (align <= 0) align = 256;
                forwardSsboOffsetAlignment = static_cast<size_t>(align);
            }
            if (!forwardInstanceMatrixSSBO) {
                glGenBuffers(1, forwardInstanceMatrixSSBO.put());
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, forwardInstanceMatrixSSBO);
            const size_t bytesRaw = matrices.size() * sizeof(glm::mat4);
            const size_t aligned = (bytesRaw + forwardSsboOffsetAlignment - 1)
                                 & ~(forwardSsboOffsetAlignment - 1);

            if (!forwardRingOrphaned || forwardRingOffset + aligned > forwardInstanceMatrixSSBOCapacity) {
                size_t needed = std::max(size_t(64) * 1024, forwardInstanceMatrixSSBOCapacity);
                while (needed < forwardRingOffset + aligned) needed *= 2;
                if (needed > forwardInstanceMatrixSSBOCapacity)
                    forwardInstanceMatrixSSBOCapacity = needed;
                glBufferData(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLsizeiptr>(forwardInstanceMatrixSSBOCapacity),
                             nullptr, GL_STREAM_DRAW);
                forwardRingOffset = 0;
                forwardRingOrphaned = true;
            }

            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            static_cast<GLintptr>(forwardRingOffset),
                            static_cast<GLsizeiptr>(bytesRaw),
                            matrices.data());
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1,
                              forwardInstanceMatrixSSBO,
                              static_cast<GLintptr>(forwardRingOffset),
                              static_cast<GLsizeiptr>(bytesRaw));
            forwardRingOffset += aligned;
            return true;
        };

        auto drawForwardInstancedOrdered = [&](const std::vector<const DrawCmd*>& draws) -> int {
            if (draws.empty()) return 0;
            static thread_local std::vector<glm::mat4> matrices;
            static thread_local std::vector<DrawCommand> commands;
            matrices.clear();
            commands.clear();
            matrices.reserve(draws.size());
            commands.reserve(draws.size());

            for (const DrawCmd* dcPtr : draws) {
                if (!dcPtr) continue;
                const DrawCmd& dc = *dcPtr;
                if (!dc.mesh) continue;

                uint32_t baseInst = 0;
                bool baseInstInit = false;
                auto appendGroup = [&](const Nvx2Group& g) {
                    const uint64_t first = static_cast<uint64_t>(g.firstIndex());
                    const uint64_t count = static_cast<uint64_t>(g.indexCount());
                    const uint64_t total = static_cast<uint64_t>(dc.mesh->idx.size());
                    if (count == 0) return;
                    if (first >= total || first + count > total) return;

                    if (!baseInstInit) {
                        baseInst = static_cast<uint32_t>(matrices.size());
                        matrices.push_back(dc.worldMatrix);
                        baseInstInit = true;
                    }

                    DrawCommand cmd{};
                    cmd.count = g.indexCount();
                    cmd.instanceCount = 1;
                    cmd.firstIndex = dc.megaIndexOffset + g.firstIndex();
                    cmd.baseVertex = 0;
                    cmd.baseInstance = baseInst;
                    commands.push_back(cmd);
                };

                if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                    appendGroup(dc.mesh->groups[dc.group]);
                } else {
                    for (const auto& g : dc.mesh->groups) {
                        appendGroup(g);
                    }
                }
            }

            if (commands.empty() || matrices.empty()) return 0;
            if (!uploadForwardInstanceMatrices(matrices)) return 0;

            for (const auto& cmd : commands) {
                glDrawElementsInstancedBaseInstance(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(cmd.count),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(cmd.firstIndex) * sizeof(uint32_t)),
                    static_cast<GLsizei>(cmd.instanceCount),
                    static_cast<GLuint>(cmd.baseInstance));
            }
            ++forwardFlushCount;
            return static_cast<int>(commands.size());
        };

        auto drawForwardBindlessMDI = [&](const std::vector<const DrawCmd*>& draws,
                                          bool useWaterCullField) -> int {
            if (draws.empty()) return 0;
            static thread_local std::vector<glm::mat4> matrices;
            static thread_local std::vector<uint32_t> materialIndices;
            static thread_local std::vector<DrawCommand> commands;
            static thread_local std::vector<int> cullModes;
            matrices.clear(); materialIndices.clear(); commands.clear(); cullModes.clear();
            matrices.reserve(draws.size());
            materialIndices.reserve(draws.size());
            commands.reserve(draws.size());
            cullModes.reserve(draws.size());

            for (const DrawCmd* dcPtr : draws) {
                if (!dcPtr || !dcPtr->mesh) continue;
                const DrawCmd& dc = *dcPtr;
                uint32_t baseInst = 0;
                bool baseInstInit = false;
                int cull = useWaterCullField ? dc.cachedWaterCullMode : dc.cullMode;

                auto appendGroup = [&](const Nvx2Group& g) {
                    if (g.indexCount() == 0) return;
                    const uint64_t first = static_cast<uint64_t>(g.firstIndex());
                    const uint64_t count = static_cast<uint64_t>(g.indexCount());
                    const uint64_t total = static_cast<uint64_t>(dc.mesh->idx.size());
                    if (first >= total || first + count > total) return;
                    if (!baseInstInit) {
                        baseInst = static_cast<uint32_t>(matrices.size());
                        matrices.push_back(dc.worldMatrix);
                        materialIndices.push_back(dc.gpuMaterialIndex);
                        baseInstInit = true;
                    }
                    DrawCommand cmd{};
                    cmd.count = g.indexCount();
                    cmd.instanceCount = 1;
                    cmd.firstIndex = dc.megaIndexOffset + g.firstIndex();
                    cmd.baseVertex = 0;
                    cmd.baseInstance = baseInst;
                    commands.push_back(cmd);
                    cullModes.push_back(cull);
                };

                if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                    appendGroup(dc.mesh->groups[dc.group]);
                } else {
                    for (const auto& g : dc.mesh->groups) appendGroup(g);
                }
            }

            if (commands.empty() || matrices.empty()) return 0;
            uploadForwardInstanceMatrices(matrices);

            {
                const size_t bytes = materialIndices.size() * sizeof(uint32_t);
                if (!forwardMaterialIndexSSBO.valid())
                    glGenBuffers(1, forwardMaterialIndexSSBO.put());
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, forwardMaterialIndexSSBO);
                if (bytes > forwardMaterialIndexSSBOCapacity) {
                    forwardMaterialIndexSSBOCapacity = std::max(bytes, size_t(4096));
                    glBufferData(GL_SHADER_STORAGE_BUFFER,
                        static_cast<GLsizeiptr>(forwardMaterialIndexSSBOCapacity),
                        nullptr, GL_STREAM_DRAW);
                }
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    static_cast<GLsizeiptr>(bytes), materialIndices.data());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, forwardMaterialIndexSSBO);
            }

            {
                const size_t bytes = commands.size() * sizeof(DrawCommand);
                if (!forwardIndirectBuffer.valid())
                    glGenBuffers(1, forwardIndirectBuffer.put());
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, forwardIndirectBuffer);
                if (bytes > forwardIndirectBufferCapacity) {
                    forwardIndirectBufferCapacity = std::max(bytes, size_t(4096));
                    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                        static_cast<GLsizeiptr>(forwardIndirectBufferCapacity),
                        nullptr, GL_STREAM_DRAW);
                }
                glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                    static_cast<GLsizeiptr>(bytes), commands.data());
            }

            int totalDraws = 0;
            size_t rangeStart = 0;
            int currentCull = cullModes[0];
            for (size_t i = 0; i <= commands.size(); ++i) {
                bool endRun = (i == commands.size()) || (cullModes[i] != currentCull);
                if (endRun && i > rangeStart) {
                    if (disableFaceCulling || currentCull <= 0) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        glCullFace(currentCull == 1 ? GL_FRONT : GL_BACK);
                    }
                    GLsizei cnt = static_cast<GLsizei>(i - rangeStart);
                    const void* offset = reinterpret_cast<const void*>(rangeStart * sizeof(DrawCommand));
                    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                        offset, cnt, sizeof(DrawCommand));
                    totalDraws += cnt;
                    ++forwardFlushCount;
                    rangeStart = i;
                    if (i < commands.size()) currentCull = cullModes[i];
                }
            }
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            return totalDraws;
        };

        // simpleLayer is authored for deferred/G-buffer and is rendered in geometryPass.
        // Keep this forward path disabled to prevent duplicate draws and stale-depth artifacts.
        constexpr bool kRenderSimpleLayerInForward = false;
        if (kRenderSimpleLayerInForward && !simpleLayerDraws.empty()) {
            auto slShader = shaderManager->GetShader("simplelayer");
            if (!slShader) {
                std::cerr << "[SIMPLELAYER] Shader not found\n";
            } else {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                glDisable(GL_STENCIL_TEST);
                glDisable(GL_BLEND);

                slShader->Use();
                slShader->SetInt("diffMapSampler", 0);
                slShader->SetInt("specMapSampler", 1);
                slShader->SetInt("emsvSampler", 2);
                slShader->SetInt("lightSampler", 3);
                slShader->SetInt("diffMap2Sampler", 4);
                slShader->SetInt("specMap1Sampler", 5);
                slShader->SetInt("maskSampler", 6);
                slShader->SetVec4("pixelSize", glm::vec4(invViewport, 0.0f, 0.0f));
                slShader->SetVec4("fogDistances", glm::vec4(180.0f, 520.0f, 0.0f, 0.0f));
                slShader->SetVec4("fogColor", glm::vec4(0.61f, 0.58f, 0.52f, 0.0f));
                slShader->SetVec4("heightFogColor", glm::vec4(0.61f, 0.58f, 0.52f, 100000.0f));
                slShader->SetFloat("encodefactor", 1.0f);
                slShader->SetInt("UseInstancing", 0);
                bindTexture(3, lightBuf);
                bindSampler(3, gSamplerClamp);

                int renderedSimpleLayer = 0;
                for (const auto& dc : simpleLayerDraws) {
                    if (!dc.mesh || dc.disabled) continue;
                    if (!PassesFrustumCulling(dc, frustum, disableFrustumCulling, true)) continue;

                    if (disableFaceCulling || dc.cullMode <= 0) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        glCullFace(dc.cullMode == 1 ? GL_FRONT : GL_BACK);
                    }

                    const glm::mat4 modelView = viewMatrix * dc.worldMatrix;
                    const glm::mat4 mvp = projMatrix * modelView;
                    slShader->SetMat4("mvp", glm::transpose(mvp));
                    slShader->SetMat4("modelView", glm::transpose(modelView));
                    slShader->SetMat4("model", glm::transpose(dc.worldMatrix));

                    auto customTintIt = dc.shaderParamsVec4.find("customColor2");
                    const glm::vec4 customTint = (customTintIt != dc.shaderParamsVec4.end())
                        ? customTintIt->second
                        : glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                    slShader->SetVec4("customColor2", customTint);

                    auto layerTilingIt = dc.shaderParamsFloat.find("layerTiling");
                    const float layerTiling = (layerTilingIt != dc.shaderParamsFloat.end())
                        ? layerTilingIt->second
                        : 1.0f;
                    slShader->SetFloat("layerTiling", layerTiling);
                    slShader->SetFloat("MatEmissiveIntensity", dc.cachedMatEmissiveIntensity);
                    slShader->SetFloat("MatSpecularIntensity", dc.cachedMatSpecularIntensity);
                    slShader->SetFloat("MatSpecularPower", dc.cachedMatSpecularPower);

                    GLuint diff0 = dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex;
                    GLuint spec0 = dc.tex[1] ? toTextureHandle(dc.tex[1]) : blackTex;
                    GLuint emsv = dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex;
                    GLuint diff1 = dc.tex[4] ? toTextureHandle(dc.tex[4]) : diff0;
                    GLuint spec1 = dc.tex[5] ? toTextureHandle(dc.tex[5]) : spec0;
                    GLuint mask = dc.tex[7] ? toTextureHandle(dc.tex[7]) : blackTex;

                    bindTexture(0, diff0);
                    bindTexture(1, spec0);
                    bindTexture(2, emsv);
                    bindTexture(4, diff1);
                    bindTexture(5, spec1);
                    bindTexture(6, mask);
                    bindSampler(0, gSamplerRepeat);
                    bindSampler(1, gSamplerRepeat);
                    bindSampler(2, gSamplerRepeat);
                    bindSampler(4, gSamplerRepeat);
                    bindSampler(5, gSamplerRepeat);
                    bindSampler(6, gSamplerRepeat);

                    if (dc.group >= 0 && dc.group < (int)dc.mesh->groups.size()) {
                        const auto& g = dc.mesh->groups[dc.group];
                        glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                            (void*)(uintptr_t)((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t)));
                    } else {
                        for (const auto& g : dc.mesh->groups) {
                            if (g.indexCount() == 0) continue;
                            glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                                (void*)(uintptr_t)((dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t)));
                        }
                    }
                    renderedSimpleLayer++;
                }

                frameDrawCalls_ += renderedSimpleLayer;
                if (optRenderLOG) NC::LOGGING::Log("[SIMPLELAYER] Forward draws: ", renderedSimpleLayer, "/", simpleLayerDraws.size());

                for (int i = 0; i <= 6; ++i) bindSampler(i, 0);
                glDisable(GL_CULL_FACE);
                glDepthMask(GL_FALSE);
            }
        }

        // ==================== ENVIRONMENT ALPHA PASS ====================
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][0], GL_TIMESTAMP);
        if (!kDisableEnvironmentAlphaPass && !environmentAlphaDraws.empty()) {
            bool useBindlessEnvAlpha = false;
            auto envShader = shaderManager->GetShader("environmentAlpha");
            if (TextureServer::sBindlessSupported && envAlphaMaterialSSBOCount_ > 0) {
                auto bShader = shaderManager->GetShader("environmentAlpha_bindless");
                if (bShader) { envShader = bShader; useBindlessEnvAlpha = true; }
                else { NC::LOGGING::Log("[ENV_ALPHA] bindless shader NOT found, ssboCount=", envAlphaMaterialSSBOCount_); }
            } else {
                static bool logged = false;
                if (!logged) {
                    NC::LOGGING::Log("[ENV_ALPHA] bindless skipped: sBindless=", TextureServer::sBindlessSupported ? 1 : 0,
                        " ssboCount=", envAlphaMaterialSSBOCount_,
                        " envAlphaDraws=", environmentAlphaDraws.size());
                    logged = true;
                }
            }
            if (!envShader) {
                std::cerr << "[ENV_ALPHA] Environment alpha shader not found\n";
            } else {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                glBlendEquation(GL_FUNC_ADD);
                glDisable(GL_CULL_FACE);

                envShader->Use();
                envShader->SetMat4("projection", camera_.getProjectionMatrix());
                envShader->SetMat4("view", camera_.getViewMatrix());
                envShader->SetMat4("textureTransform0", glm::mat4(1.0f));
                envShader->SetInt("UseSkinning", 0);
                envShader->SetVec3("eyePos", camera_.getPosition());
                envShader->SetInt("DisableViewDependentReflection", kDisableViewDependentReflections ? 1 : 0);
                envShader->SetInt("UseInstancing", 1);

                std::vector<const DrawCmd*> sortedEnvAlpha;
                sortedEnvAlpha.reserve(environmentAlphaDraws.size());
                for (const auto& src : environmentAlphaDraws) {
                    if (!src.mesh || src.disabled) continue;
                    if (!PassesFrustumCulling(src, frameFrustum_, disableFrustumCulling, true)) continue;
                    sortedEnvAlpha.push_back(&src);
                }
                SortTransparentDrawsBackToFront(sortedEnvAlpha, eyePos, viewDir);

                int renderedEnvAlpha = 0;
                int envAlphaBatches = 0;

                if (useBindlessEnvAlpha && !sortedEnvAlpha.empty()) {
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, envAlphaMaterialSSBO_);

                    static thread_local std::vector<glm::mat4> matrices;
                    static thread_local std::vector<uint32_t> materialIndices;
                    static thread_local std::vector<DrawCommand> commands;
                    matrices.clear(); materialIndices.clear(); commands.clear();
                    matrices.reserve(sortedEnvAlpha.size());
                    materialIndices.reserve(sortedEnvAlpha.size());
                    commands.reserve(sortedEnvAlpha.size());

                    for (const DrawCmd* dcPtr : sortedEnvAlpha) {
                        if (!dcPtr || !dcPtr->mesh) continue;
                        const DrawCmd& dc = *dcPtr;
                        uint32_t baseInst = 0;
                        bool baseInstInit = false;
                        auto appendGroup = [&](const Nvx2Group& g) {
                            if (g.indexCount() == 0) return;
                            const uint64_t first = static_cast<uint64_t>(g.firstIndex());
                            const uint64_t count = static_cast<uint64_t>(g.indexCount());
                            const uint64_t total = static_cast<uint64_t>(dc.mesh->idx.size());
                            if (first >= total || first + count > total) return;
                            if (!baseInstInit) {
                                baseInst = static_cast<uint32_t>(matrices.size());
                                matrices.push_back(dc.worldMatrix);
                                materialIndices.push_back(dc.gpuMaterialIndex);
                                baseInstInit = true;
                            }
                            DrawCommand cmd{};
                            cmd.count = g.indexCount();
                            cmd.instanceCount = 1;
                            cmd.firstIndex = dc.megaIndexOffset + g.firstIndex();
                            cmd.baseVertex = 0;
                            cmd.baseInstance = baseInst;
                            commands.push_back(cmd);
                        };
                        if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                            appendGroup(dc.mesh->groups[dc.group]);
                        } else {
                            for (const auto& g : dc.mesh->groups) appendGroup(g);
                        }
                    }

                    if (!commands.empty() && !matrices.empty()) {
                        uploadForwardInstanceMatrices(matrices);

                        {
                            const size_t bytes = materialIndices.size() * sizeof(uint32_t);
                            if (!forwardMaterialIndexSSBO.valid())
                                glGenBuffers(1, forwardMaterialIndexSSBO.put());
                            glBindBuffer(GL_SHADER_STORAGE_BUFFER, forwardMaterialIndexSSBO);
                            if (bytes > forwardMaterialIndexSSBOCapacity) {
                                forwardMaterialIndexSSBOCapacity = std::max(bytes, size_t(4096));
                                glBufferData(GL_SHADER_STORAGE_BUFFER,
                                    static_cast<GLsizeiptr>(forwardMaterialIndexSSBOCapacity),
                                    nullptr, GL_STREAM_DRAW);
                            }
                            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                static_cast<GLsizeiptr>(bytes), materialIndices.data());
                            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, forwardMaterialIndexSSBO);
                        }

                        {
                            const size_t bytes = commands.size() * sizeof(DrawCommand);
                            if (!forwardIndirectBuffer.valid())
                                glGenBuffers(1, forwardIndirectBuffer.put());
                            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, forwardIndirectBuffer);
                            if (bytes > forwardIndirectBufferCapacity) {
                                forwardIndirectBufferCapacity = std::max(bytes, size_t(4096));
                                glBufferData(GL_DRAW_INDIRECT_BUFFER,
                                    static_cast<GLsizeiptr>(forwardIndirectBufferCapacity),
                                    nullptr, GL_STREAM_DRAW);
                            }
                            glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                                static_cast<GLsizeiptr>(bytes), commands.data());
                            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                nullptr, static_cast<GLsizei>(commands.size()), sizeof(DrawCommand));
                            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                        }

                        envAlphaBatches = static_cast<int>(commands.size());
                        renderedEnvAlpha = static_cast<int>(sortedEnvAlpha.size());
                        ++forwardFlushCount;
                    }
                } else {
                    envShader->SetInt("DiffMap0", 0);
                    envShader->SetInt("SpecMap0", 1);
                    envShader->SetInt("BumpMap0", 2);
                    envShader->SetInt("EmsvMap0", 3);
                    envShader->SetInt("EnvironmentMap", 4);

                    struct EnvAlphaVariantKey {
                        GLuint diffTex = 0;
                        GLuint specTex = 0;
                        GLuint bumpTex = 0;
                        GLuint emsvTex = 0;
                        GLuint envCubeTex = 0;
                        uint32_t reflectivityBits = 0;
                        uint32_t alphaBlendBits = 0;
                        int twoSided = 0;
                        int isFlatNormal = 0;

                        bool operator==(const EnvAlphaVariantKey& o) const {
                            return diffTex == o.diffTex &&
                                   specTex == o.specTex &&
                                   bumpTex == o.bumpTex &&
                                   emsvTex == o.emsvTex &&
                                   envCubeTex == o.envCubeTex &&
                                   reflectivityBits == o.reflectivityBits &&
                                   alphaBlendBits == o.alphaBlendBits &&
                                   twoSided == o.twoSided &&
                                   isFlatNormal == o.isFlatNormal;
                        }
                    };
                    auto toBits = [](float v) -> uint32_t {
                        uint32_t bits = 0;
                        std::memcpy(&bits, &v, sizeof(bits));
                        return bits;
                    };
                    auto fromBits = [](uint32_t bits) -> float {
                        float v = 0.0f;
                        std::memcpy(&v, &bits, sizeof(v));
                        return v;
                    };

                    std::vector<const DrawCmd*> batchDraws;
                    batchDraws.reserve(sortedEnvAlpha.size());
                    EnvAlphaVariantKey batchKey{};
                    bool hasBatch = false;
                    auto flushEnvAlphaBatch = [&]() {
                        if (!hasBatch || batchDraws.empty()) return;
                        envShader->SetFloat("Reflectivity", fromBits(batchKey.reflectivityBits));
                        envShader->SetInt("twoSided", batchKey.twoSided);
                        envShader->SetInt("isFlatNormal", batchKey.isFlatNormal);
                        envShader->SetFloat("alphaBlendFactor", fromBits(batchKey.alphaBlendBits));
                        bindTexture(0, batchKey.diffTex);
                        bindTexture(1, batchKey.specTex);
                        bindTexture(2, batchKey.bumpTex);
                        bindTexture(3, batchKey.emsvTex);
                        glActiveTexture(GL_TEXTURE4);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, batchKey.envCubeTex);
                        bindSampler(0, gSamplerRepeat);
                        bindSampler(1, gSamplerRepeat);
                        bindSampler(2, gSamplerRepeat);
                        bindSampler(3, gSamplerRepeat);
                        bindSampler(4, 0);

                        envAlphaBatches += drawForwardInstancedOrdered(batchDraws);
                        renderedEnvAlpha += static_cast<int>(batchDraws.size());
                        batchDraws.clear();
                    };

                    for (const DrawCmd* srcPtr : sortedEnvAlpha) {
                        if (!srcPtr) continue;
                        const DrawCmd& src = *srcPtr;
                        EnvAlphaVariantKey key;
                        key.diffTex = src.tex[0] ? toTextureHandle(src.tex[0]) : whiteTex;
                        key.specTex = src.tex[1] ? toTextureHandle(src.tex[1]) : whiteTex;
                        key.bumpTex = src.tex[2] ? toTextureHandle(src.tex[2]) : normalTex;
                        key.emsvTex = src.tex[3] ? toTextureHandle(src.tex[3]) : blackTex;
                        key.envCubeTex = src.tex[9] ? toTextureHandle(src.tex[9]) : blackCubeTex;
                        key.reflectivityBits = toBits(src.cachedIntensity0);
                        key.alphaBlendBits = toBits(src.cachedAlphaBlendFactor);
                        key.twoSided = src.cachedTwoSided;
                        key.isFlatNormal = src.cachedIsFlatNormal;

                        if (!hasBatch) {
                            batchKey = key;
                            hasBatch = true;
                        } else if (!(key == batchKey)) {
                            flushEnvAlphaBatch();
                            batchKey = key;
                        }
                        batchDraws.push_back(srcPtr);
                    }
                    flushEnvAlphaBatch();
                }

                frameDrawCalls_ += envAlphaBatches;
                if (optRenderLOG) NC::LOGGING::Log("[ENV_ALPHA] Rendered: ", renderedEnvAlpha, "/", environmentAlphaDraws.size(),
                    " batches=", envAlphaBatches, " bindless=", useBindlessEnvAlpha ? 1 : 0);

                for (int i = 0; i < 5; i++) bindSampler(i, 0);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

                glDisable(GL_BLEND);
                checkGLError("After environment alpha");
            }
        }
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][1], GL_TIMESTAMP);

        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][2], GL_TIMESTAMP);
        if (!kDisableRefractionPass && !refractionDraws.empty()) {
            bool useBindlessRefraction = false;
            auto refractionShader = shaderManager->GetShader("refraction");
            if (TextureServer::sBindlessSupported) {
                auto bShader = shaderManager->GetShader("refraction_bindless");
                if (bShader) { refractionShader = bShader; useBindlessRefraction = true; }
            }
            if (!refractionShader) {
                std::cerr << "[REFRACTION] Shader not found\n";
            } else {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                glBlendEquation(GL_FUNC_ADD);

                refractionShader->Use();
                refractionShader->SetMat4("projection", projMatrix);
                refractionShader->SetMat4("view", viewMatrix);
                refractionShader->SetInt("sceneTex", 0);
                refractionShader->SetFloat("time", particleTime);
                refractionShader->SetVec2("invViewport", invViewport);
                refractionShader->SetInt("UseInstancing", 1);
                if (!useBindlessRefraction) {
                    refractionShader->SetMat4("textureTransform0", glm::mat4(1.0f));
                    refractionShader->SetInt("DistortMap", 1);
                }

                std::vector<const DrawCmd*> sortedRefraction;
                sortedRefraction.reserve(refractionDraws.size());
                for (const auto& src : refractionDraws) {
                    if (!src.mesh || src.disabled) continue;
                    if (!PassesFrustumCulling(src, frustum, disableFrustumCulling, true)) continue;
                    sortedRefraction.push_back(&src);
                }
                SortTransparentDrawsBackToFront(sortedRefraction, eyePos, viewDir);

                int renderedRefraction = 0;
                int refractionBatches = 0;

                if (useBindlessRefraction) {
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, refractionMaterialSSBO_);
                    bindTexture(0, sceneColorTex);
                    bindSampler(0, gSamplerClamp);
                    refractionBatches = drawForwardBindlessMDI(sortedRefraction, false);
                    renderedRefraction = static_cast<int>(sortedRefraction.size());
                } else {
                    struct RefractionKey {
                        GLuint distortTex = 0;
                        uint32_t velocityXBits = 0;
                        uint32_t velocityYBits = 0;
                        uint32_t distortionScaleBits = 0;
                        int cullMode = 0;
                        bool operator==(const RefractionKey& o) const {
                            return distortTex == o.distortTex &&
                                   velocityXBits == o.velocityXBits &&
                                   velocityYBits == o.velocityYBits &&
                                   distortionScaleBits == o.distortionScaleBits &&
                                   cullMode == o.cullMode;
                        }
                    };
                    auto toBits = [](float v) -> uint32_t {
                        uint32_t bits = 0;
                        std::memcpy(&bits, &v, sizeof(bits));
                        return bits;
                    };
                    auto fromBits = [](uint32_t bits) -> float {
                        float v = 0.0f;
                        std::memcpy(&v, &bits, sizeof(v));
                        return v;
                    };

                    std::vector<const DrawCmd*> batchDraws;
                    batchDraws.reserve(sortedRefraction.size());
                    RefractionKey batchKey{};
                    bool hasBatch = false;
                    auto flushRefractionBatch = [&]() {
                        if (!hasBatch || batchDraws.empty()) return;
                        if (disableFaceCulling || batchKey.cullMode <= 0) {
                            glDisable(GL_CULL_FACE);
                        } else {
                            glEnable(GL_CULL_FACE);
                            glCullFace(batchKey.cullMode == 1 ? GL_FRONT : GL_BACK);
                        }
                        refractionShader->SetVec2("velocity", glm::vec2(fromBits(batchKey.velocityXBits), fromBits(batchKey.velocityYBits)));
                        refractionShader->SetFloat("distortionScale", fromBits(batchKey.distortionScaleBits));
                        bindTexture(0, sceneColorTex);
                        bindTexture(1, batchKey.distortTex);
                        refractionBatches += drawForwardInstancedOrdered(batchDraws);
                        renderedRefraction += static_cast<int>(batchDraws.size());
                        batchDraws.clear();
                    };

                    bindSampler(0, gSamplerClamp);
                    bindSampler(1, gSamplerRepeat);
                    for (const DrawCmd* srcPtr : sortedRefraction) {
                        if (!srcPtr) continue;
                        const DrawCmd& src = *srcPtr;
                        float distortionScale = src.cachedIntensity0;
                        auto itScale = src.shaderParamsFloat.find("distortionScale");
                        if (itScale != src.shaderParamsFloat.end()) {
                            distortionScale = itScale->second;
                        }
                        RefractionKey key;
                        key.distortTex = src.tex[0] ? toTextureHandle(src.tex[0]) : whiteTex;
                        key.velocityXBits = toBits(src.cachedVelocity.x);
                        key.velocityYBits = toBits(src.cachedVelocity.y);
                        key.distortionScaleBits = toBits(distortionScale);
                        key.cullMode = src.cullMode;
                        if (!hasBatch) {
                            batchKey = key;
                            hasBatch = true;
                        } else if (!(key == batchKey)) {
                            flushRefractionBatch();
                            batchKey = key;
                        }
                        batchDraws.push_back(srcPtr);
                    }
                    flushRefractionBatch();
                }

                frameDrawCalls_ += refractionBatches;
                if (optRenderLOG) NC::LOGGING::Log("[REFRACTION] Rendered: ", renderedRefraction, "/", refractionDraws.size(),
                    " batches=", refractionBatches, " bindless=", useBindlessRefraction ? 1 : 0);

                bindSampler(0, 0);
                bindSampler(1, 0);
                glDisable(GL_CULL_FACE);
                glDisable(GL_BLEND);
                checkGLError("After refraction");
            }
        }
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][3], GL_TIMESTAMP);

        // ==================== WATER PASS ====================
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][4], GL_TIMESTAMP);
        if (!kDisableWaterPass && !waterDraws.empty()) {
            bool useBindlessWater = false;
            auto waterShader = shaderManager->GetShader("water");
            if (TextureServer::sBindlessSupported) {
                auto bShader = shaderManager->GetShader("water_bindless");
                if (bShader) { waterShader = bShader; useBindlessWater = true; }
            }
            if (!waterShader) {
                std::cerr << "    [ERROR] Water shader not found\n";
            } else {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDepthMask(GL_FALSE);
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                glBlendEquation(GL_FUNC_ADD);

                waterShader->Use();
                waterShader->SetMat4("projection", projMatrix);
                waterShader->SetMat4("view", viewMatrix);
                waterShader->SetInt("UseSkinning", 0);
                waterShader->SetVec3("eyePos", camera_.getPosition());
                waterShader->SetInt("DisableViewDependentReflection", kDisableViewDependentReflections ? 1 : 0);
                waterShader->SetInt("UseInstancing", 1);

                if (useBindlessWater) {
                    waterShader->SetFloat("time", particleTime);
                } else {
                    waterShader->SetInt("DiffMap0", 0);
                    waterShader->SetInt("BumpMap0", 1);
                    waterShader->SetInt("EmsvMap0", 2);
                    waterShader->SetInt("CubeMap0", 3);
                }

                std::vector<const DrawCmd*> sortedWater;
                sortedWater.reserve(waterDraws.size());
                for (const auto& src : waterDraws) {
                    if (!src.mesh || src.disabled) continue;
                    if (!PassesFrustumCulling(src, frustum, disableFrustumCulling, true)) continue;
                    sortedWater.push_back(&src);
                }
                SortTransparentDrawsBackToFront(sortedWater, eyePos, viewDir);

                int renderedWater = 0;
                int waterBatches = 0;

                if (useBindlessWater) {
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, waterMaterialSSBO_);
                    waterBatches = drawForwardBindlessMDI(sortedWater, true);
                    renderedWater = static_cast<int>(sortedWater.size());
                } else {
                    struct WaterVariantKey {
                        GLuint diffTex = 0;
                        GLuint bumpTex = 0;
                        GLuint emsvTex = 0;
                        GLuint cubeTex = 0;
                        uint32_t intensityBits = 0;
                        uint32_t emissiveBits = 0;
                        uint32_t specularBits = 0;
                        uint32_t bumpScaleBits = 0;
                        uint32_t uvScaleBits = 0;
                        uint32_t velocityXBits = 0;
                        uint32_t velocityYBits = 0;
                        int hasVelocity = 0;
                        int cullMode = 0;
                        bool operator==(const WaterVariantKey& o) const {
                            return diffTex == o.diffTex && bumpTex == o.bumpTex &&
                                   emsvTex == o.emsvTex && cubeTex == o.cubeTex &&
                                   intensityBits == o.intensityBits && emissiveBits == o.emissiveBits &&
                                   specularBits == o.specularBits && bumpScaleBits == o.bumpScaleBits &&
                                   uvScaleBits == o.uvScaleBits && velocityXBits == o.velocityXBits &&
                                   velocityYBits == o.velocityYBits && hasVelocity == o.hasVelocity &&
                                   cullMode == o.cullMode;
                        }
                    };
                    auto toBits = [](float v) -> uint32_t {
                        uint32_t bits = 0;
                        std::memcpy(&bits, &v, sizeof(bits));
                        return bits;
                    };
                    auto fromBits = [](uint32_t bits) -> float {
                        float v = 0.0f;
                        std::memcpy(&v, &bits, sizeof(v));
                        return v;
                    };

                    std::vector<const DrawCmd*> batchDraws;
                    batchDraws.reserve(sortedWater.size());
                    WaterVariantKey batchKey{};
                    bool hasBatch = false;
                    auto flushWaterBatch = [&]() {
                        if (!hasBatch || batchDraws.empty()) return;
                        if (disableFaceCulling || batchKey.cullMode <= 0) {
                            glDisable(GL_CULL_FACE);
                        } else {
                            glEnable(GL_CULL_FACE);
                            glCullFace(batchKey.cullMode == 1 ? GL_FRONT : GL_BACK);
                        }
                        glm::mat4 uvTransform = glm::mat4(1.0f);
                        if (batchKey.hasVelocity > 0) {
                            const glm::vec2 velocity(fromBits(batchKey.velocityXBits), fromBits(batchKey.velocityYBits));
                            uvTransform = glm::translate(glm::mat4(1.0f), glm::vec3(velocity * particleTime, 0.0f));
                        }
                        waterShader->SetMat4("textureTransform0", uvTransform);
                        waterShader->SetVec2("uvScale", glm::vec2(fromBits(batchKey.uvScaleBits), fromBits(batchKey.uvScaleBits)));
                        waterShader->SetFloat("Intensity0", fromBits(batchKey.intensityBits));
                        waterShader->SetFloat("MatEmissiveIntensity", fromBits(batchKey.emissiveBits));
                        waterShader->SetFloat("MatSpecularIntensity", fromBits(batchKey.specularBits));
                        waterShader->SetFloat("BumpScale", fromBits(batchKey.bumpScaleBits));
                        bindTexture(0, batchKey.diffTex);
                        bindTexture(1, batchKey.bumpTex);
                        bindTexture(2, batchKey.emsvTex);
                        glActiveTexture(GL_TEXTURE3);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, batchKey.cubeTex);
                        waterBatches += drawForwardInstancedOrdered(batchDraws);
                        renderedWater += static_cast<int>(batchDraws.size());
                        batchDraws.clear();
                    };

                    bindSampler(0, gSamplerRepeat);
                    bindSampler(1, gSamplerRepeat);
                    bindSampler(2, gSamplerRepeat);
                    bindSampler(3, 0);
                    for (const DrawCmd* srcPtr : sortedWater) {
                        if (!srcPtr) continue;
                        const DrawCmd& src = *srcPtr;
                        float uvScale = src.cachedScale;
                        if (uvScale <= 0.0f) uvScale = 1.0f;
                        WaterVariantKey key;
                        key.diffTex = src.tex[0] ? toTextureHandle(src.tex[0]) : whiteTex;
                        key.bumpTex = src.tex[2] ? toTextureHandle(src.tex[2]) : normalTex;
                        key.emsvTex = src.tex[3] ? toTextureHandle(src.tex[3]) : blackTex;
                        key.cubeTex = src.tex[9] ? toTextureHandle(src.tex[9]) : blackCubeTex;
                        key.intensityBits = toBits(src.cachedIntensity0);
                        key.emissiveBits = toBits(src.cachedMatEmissiveIntensity);
                        key.specularBits = toBits(src.cachedMatSpecularIntensity);
                        key.bumpScaleBits = toBits(src.cachedBumpScale);
                        key.uvScaleBits = toBits(uvScale);
                        key.velocityXBits = toBits(src.cachedVelocity.x);
                        key.velocityYBits = toBits(src.cachedVelocity.y);
                        key.hasVelocity = src.cachedHasVelocity ? 1 : 0;
                        key.cullMode = src.cachedWaterCullMode;
                        if (!hasBatch) {
                            batchKey = key;
                            hasBatch = true;
                        } else if (!(key == batchKey)) {
                            flushWaterBatch();
                            batchKey = key;
                        }
                        batchDraws.push_back(srcPtr);
                    }
                    flushWaterBatch();
                }

                frameDrawCalls_ += waterBatches;
                if (optRenderLOG) NC::LOGGING::Log("[WATER] Rendered: ", renderedWater, "/", waterDraws.size(),
                    " batches=", waterBatches, " bindless=", useBindlessWater ? 1 : 0);

                glBindVertexArray(0);
                for (int i = 0; i < 4; i++) {
                    if (device_) {
                        device_->BindSampler(nullptr, i);
                    } else {
                        glBindSampler(i, 0);
                    }
                }

                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                glBlendEquation(GL_FUNC_ADD);
                glDisable(GL_CULL_FACE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDepthMask(GL_FALSE);
                checkGLError("After water");
            }
        }
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][5], GL_TIMESTAMP);

        // ==================== POST-ALPHA UNLIT PASS ====================
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][6], GL_TIMESTAMP);
        if (!kDisablePostAlphaUnlitPass && !postAlphaUnlitDraws.empty()) {
            auto postShader = shaderManager->GetShader("postalphaunlit");
            if (!postShader) {
                std::cerr << "    [ERROR] postalphaunlit shader not found\n";
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_FALSE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);

                postShader->Use();
                postShader->SetMat4("projection", projMatrix);
                postShader->SetMat4("view", viewMatrix);
                postShader->SetMat4("textureTransform0", glm::mat4(1.0f));
                postShader->SetInt("UseSkinning", 0);
                postShader->SetInt("DiffMap0", 0);
                postShader->SetInt("EmsvMap0", 1);
                postShader->SetFloat("diffContribution", 1.0f);
                postShader->SetInt("UseInstancing", 1);

                struct PostAlphaKey {
                    GLuint diffTex = 0;
                    GLuint emsvTex = 0;
                    uint32_t matEmissiveBits = 0;
                    uint32_t alphaBlendBits = 0;
                    uint32_t diffContributionBits = 0;
                    int additive = 0;
                    int cullMode = 0;

                    bool operator==(const PostAlphaKey& o) const {
                        return diffTex == o.diffTex &&
                               emsvTex == o.emsvTex &&
                               matEmissiveBits == o.matEmissiveBits &&
                               alphaBlendBits == o.alphaBlendBits &&
                               diffContributionBits == o.diffContributionBits &&
                               additive == o.additive &&
                               cullMode == o.cullMode;
                    }
                };
                auto toBits = [](float v) -> uint32_t {
                    uint32_t bits = 0;
                    std::memcpy(&bits, &v, sizeof(bits));
                    return bits;
                };
                auto fromBits = [](uint32_t bits) -> float {
                    float v = 0.0f;
                    std::memcpy(&v, &bits, sizeof(v));
                    return v;
                };

                std::vector<const DrawCmd*> sortedPostAlpha;
                sortedPostAlpha.reserve(postAlphaUnlitDraws.size());
                for (auto& dc : postAlphaUnlitDraws) {
                    if (!dc.mesh || dc.disabled) continue;
                    if (!PassesFrustumCulling(dc, frustum, disableFrustumCulling, true)) continue;
                    sortedPostAlpha.push_back(&dc);
                }
                SortTransparentDrawsBackToFront(sortedPostAlpha, eyePos, viewDir);

                int renderedPost = 0;
                int postBatches = 0;
                int renderedPostFallback = 0;
                bool loggedPostBinding = false;
                std::vector<const DrawCmd*> batchDraws;
                batchDraws.reserve(sortedPostAlpha.size());
                PostAlphaKey batchKey{};
                bool hasBatch = false;
                auto flushPostBatch = [&]() {
                    if (!hasBatch || batchDraws.empty()) return;
                    if (batchKey.additive > 0) {
                        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
                    } else {
                        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                    }

                    if (batchKey.additive > 0) {
                        glDisable(GL_CULL_FACE);
                    } else if (disableFaceCulling || batchKey.cullMode <= 0) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        glCullFace(batchKey.cullMode == 1 ? GL_FRONT : GL_BACK);
                    }

                    postShader->SetFloat("MatEmissiveIntensity", fromBits(batchKey.matEmissiveBits));
                    postShader->SetFloat("alphaBlendFactor", fromBits(batchKey.alphaBlendBits));
                    postShader->SetFloat("diffContribution", fromBits(batchKey.diffContributionBits));

                    bindTexture(0, batchKey.diffTex);
                    bindTexture(1, batchKey.emsvTex);
                    if (device_ && samplerRepeat_abstracted) {
                        device_->BindSampler(samplerRepeat_abstracted.get(), 0);
                        device_->BindSampler(samplerRepeat_abstracted.get(), 1);
                    } else {
                        glBindSampler(0, gSamplerRepeat);
                        glBindSampler(1, gSamplerRepeat);
                    }

                    postBatches += drawForwardInstancedOrdered(batchDraws);
                    renderedPost += static_cast<int>(batchDraws.size());

                    if (!loggedPostBinding && optRenderLOG) {
                        std::cout << "    [PostAlphaBind] batched DiffMap0=" << batchKey.diffTex
                                  << " EmsvMap0=" << batchKey.emsvTex
                                  << " emissiveIntensity=" << fromBits(batchKey.matEmissiveBits)
                                  << " alphaBlendFactor=" << fromBits(batchKey.alphaBlendBits)
                                  << " cullMode=" << batchKey.cullMode
                                  << " additive=" << batchKey.additive << "\n";
                        loggedPostBinding = true;
                    }
                    batchDraws.clear();
                };

                for (const DrawCmd* dcPtr : sortedPostAlpha) {
                    if (!dcPtr || !dcPtr->mesh) continue;
                    const DrawCmd& dc = *dcPtr;
                    const bool additive = dc.cachedIsAdditive;
                    float matEmissiveIntensity = dc.cachedMatEmissiveIntensity;
                    float alphaBlendFactor = dc.cachedAlphaBlendFactor;

                    std::vector<std::pair<std::string, float>> extraUniforms;
                    bool hasAnimatedMatEmissiveIntensity = false;
                    if (dc.hasShaderVarAnimations) {
                        float dcAnimTime = particleTime;
                        if (dc.instance) {
                            auto itSpawn = scene_.instanceSpawnTimes.find(dc.instance);
                            if (itSpawn != scene_.instanceSpawnTimes.end()) {
                                dcAnimTime = std::max(0.0f, particleTime - static_cast<float>(itSpawn->second));
                            }
                        }

                        auto animParams = DeferredRendererAnimation::SampleShaderVarAnimationsForTarget(dc.nodeName, dcAnimTime, dc.instance);
                        for (const auto& [paramName, value] : animParams) {
                            if (paramName == "MatEmissiveIntensity") {
                                matEmissiveIntensity = value;
                                hasAnimatedMatEmissiveIntensity = true;
                            } else if (paramName == "alphaBlendFactor") {
                                alphaBlendFactor = value;
                            } else {
                                extraUniforms.emplace_back(paramName, value);
                            }
                        }
                    }

                    GLuint diffTex = dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex;
                    GLuint emsvTex = dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex;
                    float diffContribution = 1.0f;
                    if (!dc.cachedHasCustomDiffMap && emsvTex && emsvTex != blackTex) {
                        diffTex = emsvTex;
                        diffContribution = 0.0f;
                    }

                    if (additive &&
                        emsvTex && emsvTex != blackTex &&
                        !hasAnimatedMatEmissiveIntensity &&
                        matEmissiveIntensity <= 0.0f) {
                        matEmissiveIntensity = 1.0f;
                    }

                    if (!extraUniforms.empty()) {
                        flushPostBatch();
                        postShader->SetInt("UseInstancing", 0);

                        if (additive) {
                            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
                            glDisable(GL_CULL_FACE);
                        } else if (disableFaceCulling || dc.cullMode <= 0) {
                            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                            glDisable(GL_CULL_FACE);
                        } else {
                            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                            glEnable(GL_CULL_FACE);
                            glCullFace(dc.cullMode == 1 ? GL_FRONT : GL_BACK);
                        }

                        for (const auto& [paramName, value] : extraUniforms) {
                            postShader->SetFloat(paramName, value);
                        }

                        postShader->SetMat4("model", dc.worldMatrix);
                        postShader->SetFloat("MatEmissiveIntensity", matEmissiveIntensity);
                        postShader->SetFloat("alphaBlendFactor", alphaBlendFactor);
                        postShader->SetFloat("diffContribution", diffContribution);

                        bindTexture(0, diffTex);
                        bindTexture(1, emsvTex);
                        if (device_ && samplerRepeat_abstracted) {
                            device_->BindSampler(samplerRepeat_abstracted.get(), 0);
                            device_->BindSampler(samplerRepeat_abstracted.get(), 1);
                        } else {
                            glBindSampler(0, gSamplerRepeat);
                            glBindSampler(1, gSamplerRepeat);
                        }

                        if (dc.group >= 0 && dc.group < static_cast<int>(dc.mesh->groups.size())) {
                            const auto& g = dc.mesh->groups[dc.group];
                            glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                                reinterpret_cast<void*>(static_cast<uintptr_t>(dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t)));
                        } else {
                            for (const auto& g : dc.mesh->groups) {
                                if (g.indexCount() == 0) continue;
                                glDrawElements(GL_TRIANGLES, g.indexCount(), GL_UNSIGNED_INT,
                                    reinterpret_cast<void*>(static_cast<uintptr_t>(dc.megaIndexOffset + g.firstIndex()) * sizeof(uint32_t)));
                            }
                        }
                        renderedPostFallback++;
                        postShader->SetInt("UseInstancing", 1);
                        continue;
                    }

                    PostAlphaKey key;
                    key.diffTex = diffTex;
                    key.emsvTex = emsvTex;
                    key.matEmissiveBits = toBits(matEmissiveIntensity);
                    key.alphaBlendBits = toBits(alphaBlendFactor);
                    key.diffContributionBits = toBits(diffContribution);
                    key.additive = additive ? 1 : 0;
                    key.cullMode = dc.cullMode;

                    if (!hasBatch) {
                        batchKey = key;
                        hasBatch = true;
                    } else if (!(key == batchKey)) {
                        flushPostBatch();
                        batchKey = key;
                    }
                    batchDraws.push_back(dcPtr);
                }
                flushPostBatch();

                frameDrawCalls_ += postBatches + renderedPostFallback;
                if (optRenderLOG) NC::LOGGING::Log("[POST_ALPHA] Rendered: ", renderedPost + renderedPostFallback, "/", postAlphaUnlitDraws.size(),
                                                   " batches=", postBatches, " fallback=", renderedPostFallback);

                glBindVertexArray(0);
                if (device_) {
                    device_->BindSampler(nullptr, 0);
                    device_->BindSampler(nullptr, 1);
                } else {
                    glBindSampler(0, 0);
                    glBindSampler(1, 0);
                }
                glDisable(GL_CULL_FACE);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                checkGLError("After postalphaunlit");
            }
        }
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][7], GL_TIMESTAMP);

        // ==================== PARTICLE PASS ====================
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][8], GL_TIMESTAMP);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDisable(GL_CULL_FACE);

        if (!kDisableParticlePass && !ParticlesDisabled()) {
            auto& particleServer = Particles::ParticleServer::Instance();
            if (!particleServer.IsOpen()) {
                particleServer.Open();
            }
            auto particleRenderer = particleServer.GetParticleRenderer();
            if (!particleRenderer) {
                std::cerr << "    [ERROR] Particle renderer is NULL!\n";
            } else if (!scene_.particleNodes.empty()) {
                particleRenderer->BeginAttach();
                int updatedCount = 0;
                for (auto& pentry : scene_.particleNodes) {
                    if (!pentry.node) continue;
                    auto inst = pentry.node->GetInstance();
                    if (!inst) continue;
                    if (inst->IsStopped() && inst->GetNumLiveParticles() == 0) continue;

                    if (pentry.dynamicTransform) {
                        glm::mat4 localTransform = pentry.local;
                        if (pentry.sourceNode) {
                            localTransform = DeferredRendererAnimation::ComposeAnimatedHierarchyLocal(pentry.sourceNode, pentry.local, pentry.instance);
                        }

                        glm::mat4 emitterTransform = localTransform;
                        if (pentry.instance) {
                            auto* modelInst = static_cast<ModelInstance*>(pentry.instance);
                            emitterTransform = modelInst->getTransform() * localTransform;
                        }
                        inst->SetTransform(emitterTransform);
                    }
                    inst->SetRenderer(particleRenderer);
                    inst->Update(particleTime);
                    inst->UpdateVertexStreams();
                    updatedCount++;
                }
                particleRenderer->EndAttach();

                glm::vec2 fogDistances(100.0f, 1000.0f);
                glm::vec4 fogColor(0.5f, 0.5f, 0.5f, 1.0f);
                int renderedCount = 0;
                for (auto& pentry : scene_.particleNodes) {
                    if (pentry.node) {
                        auto inst = pentry.node->GetInstance();
                        if (inst) {
                            if (inst->IsStopped() && inst->GetNumLiveParticles() == 0) {
                                continue;
                            }
                            int colorMode = 2;
                            if (pentry.node->GetBlendMode() == Particles::ParticleBlendMode::Additive) {
                                colorMode = 1;
                                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
                            } else {
                                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                            }

                            const int numAnimPhases = pentry.node->GetNumAnimPhases();
                            float animFramesPerSecond = pentry.node->GetAnimFramesPerSecond();
                            if (animFramesPerSecond < 0.0f) {
                                animFramesPerSecond = 0.0f;
                            }

                            inst->Render(viewProj, viewMatrix, invView, eyePos,
                                         fogDistances, fogColor, gPosWS, invViewport,
                                         numAnimPhases, animFramesPerSecond, particleTime, colorMode,
                                         pentry.node->GetIntensity0(), pentry.node->GetEmissiveIntensity());
                            renderedCount++;
                        }
                    }
                }
                frameDrawCalls_ += renderedCount;
                if (optRenderLOG) NC::LOGGING::Log("[PARTICLES] Updated ", updatedCount, ", rendered ", renderedCount, "/", scene_.particleNodes.size(), " nodes");
                checkGLError("After particle rendering");
            }
        }


        glBindVertexArray(0);
        glBindSampler(0, 0);
        glBindSampler(1, 0);
        glActiveTexture(GL_TEXTURE0);

        if (sceneFBO != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
            glDisable(GL_BLEND);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }

        // Restore a predictable baseline so following passes don't inherit
        // particle/water overrides.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        if (optRenderLOG) NC::LOGGING::Log("[FORWARD] Env: ", environmentDraws.size(), " EnvAlpha: ", environmentAlphaDraws.size(),
                         " Refr: ", refractionDraws.size(), " Water: ", waterDraws.size(), " Part: ", scene_.particleNodes.size(), " PostA: ", postAlphaUnlitDraws.size());
        if (gpuQueriesInit_) glQueryCounter(gpuFwdTsDB_[gpuQueryBufWrite_][9], GL_TIMESTAMP);
        frameProfile_.fwdFlushCount = forwardFlushCount;
        if (gEnableGLErrorChecking) {
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) std::cerr << "[FORWARD] Error: 0x" << std::hex << err << std::dec << "\n";
        }
        if (optRenderLOG) NC::LOGGING::Log("[FORWARD] Complete");
    };
    particleGraph->compile();

    if (optRenderLOG) std::cout << "[INIT] Frame graphs compiled successfully\n";

    if (device_) {
        NDEVC::Graphics::RenderStateDesc blitStateDesc;
        blitStateDesc.depth.depthTest = false;
        blitStateDesc.blend.blendEnable = false;
        cachedBlitState_ = device_->CreateRenderState(blitStateDesc);

        NDEVC::Graphics::RenderStateDesc envStateDesc;
        envStateDesc.depth.depthTest = true;
        envStateDesc.depth.depthWrite = true;
        envStateDesc.depth.depthFunc = NDEVC::Graphics::CompareFunc::Less;
        envStateDesc.blend.blendEnable = false;
        envStateDesc.stencil.stencilEnable = true;
        envStateDesc.stencil.stencilFunc = NDEVC::Graphics::CompareFunc::Always;
        envStateDesc.stencil.ref = 1;
        envStateDesc.stencil.readMask = 0xFF;
        envStateDesc.stencil.writeMask = 0xFF;
        envStateDesc.stencil.stencilFailOp = NDEVC::Graphics::StencilOp::Keep;
        envStateDesc.stencil.depthFailOp = NDEVC::Graphics::StencilOp::Keep;
        envStateDesc.stencil.depthPassOp = NDEVC::Graphics::StencilOp::Replace;
        envStateDesc.rasterizer.cullMode = NDEVC::Graphics::CullMode::Back;
        cachedEnvState_ = device_->CreateRenderState(envStateDesc);
        envStateDesc.rasterizer.cullMode = NDEVC::Graphics::CullMode::None;
        cachedEnvStateNoCull_ = device_->CreateRenderState(envStateDesc);

        NDEVC::Graphics::RenderStateDesc geomStateDesc;
        geomStateDesc.stencil.stencilEnable = true;
        geomStateDesc.stencil.stencilFunc = NDEVC::Graphics::CompareFunc::Always;
        geomStateDesc.stencil.ref = 1;
        geomStateDesc.stencil.readMask = 0xFF;
        geomStateDesc.stencil.writeMask = 0xFF;
        geomStateDesc.stencil.stencilFailOp = NDEVC::Graphics::StencilOp::Keep;
        geomStateDesc.stencil.depthFailOp = NDEVC::Graphics::StencilOp::Keep;
        geomStateDesc.stencil.depthPassOp = NDEVC::Graphics::StencilOp::Replace;
        cachedGeomState_ = device_->CreateRenderState(geomStateDesc);
    }

    std::cout << "[LOOP] Starting main render loop with " << gClips.size() << " animation clips\n";
    for (const auto& c : gClips) {
        std::cout << "  Clip: node='" << c.node << "' loop=" << c.loop << " active=" << c.active << "\n";
    }

}

// ---------------------------------------------------------------------------
// renderSingleFrame -- one iteration of the while loop from initLOOP
// ---------------------------------------------------------------------------
void DeferredRenderer::renderSingleFrame()
{
        const double currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (deltaTime < 0.0 || !std::isfinite(deltaTime)) deltaTime = 1.0 / 60.0;
        if (deltaTime > 0.1) deltaTime = 0.1;
        if (ProcessPendingDroppedMapLoad(currentFrame)) {
            return;
        }

        // ── Frame profiler: starts before ALL per-frame work ──────────────
        frameProfile_ = FrameProfile{};
        using ProfileClock = std::chrono::high_resolution_clock;
        const auto profFrameStart = ProfileClock::now();
        auto profLast = profFrameStart;
        auto profElapsed = [&]() -> double {
            auto now = ProfileClock::now();
            double ms = std::chrono::duration<double, std::milli>(now - profLast).count();
            profLast = now;
            return ms;
        };

        bool uiWantsKeyboard = false;
        if (ImGui::GetCurrentContext()) {
            uiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;
        }
        const bool allowViewportKeyboardInput =
            (editorModeEnabled_ && editorViewportInputRouting_) ? IsSceneViewportInputActive() : !uiWantsKeyboard;
        if (allowViewportKeyboardInput) {
            // Top-down pan: WASD moves in world XZ regardless of camera pitch.
            GLFWwindow* win = (GLFWwindow*)window_->GetNativeHandle();
            const float speed = 200.0f * (float)deltaTime;
            const glm::vec3 fwd3 = camera_.getForward();
            glm::vec3 hFwd(fwd3.x, 0.0f, fwd3.z);
            if (glm::length(hFwd) > 0.001f) hFwd = glm::normalize(hFwd);
            else hFwd = glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 hRight = glm::normalize(glm::cross(hFwd, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 pos = camera_.getPosition();
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) pos += hFwd  * speed;
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) pos -= hFwd  * speed;
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) pos += hRight * speed;
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) pos -= hRight * speed;
            camera_.setPosition(pos);
        }
        frameProfile_.inputPoll = profElapsed();

        // ── Mode switch key (F1): toggle Work <-> Play ──────────────────
        {
            GLFWwindow* win = (GLFWwindow*)window_->GetNativeHandle();
            static bool f1WasPressed = false;
            const bool f1Pressed = glfwGetKey(win, GLFW_KEY_F1) == GLFW_PRESS;
            if (f1Pressed && !f1WasPressed) {
                SetRenderMode(renderMode_ == RenderMode::Work ? RenderMode::Play : RenderMode::Work);
            }
            f1WasPressed = f1Pressed;
        }

        // ── Dirty-frame detection ───────────────────────────────────────
        {
            const glm::mat4 curView = camera_.getViewMatrix();
            const glm::mat4 curProj = camera_.getProjectionMatrix();
            if (curView != dirtyLastViewMatrix_ || curProj != dirtyLastProjectionMatrix_) {
                dirtyFlag_ = true;
                dirtyLastViewMatrix_ = curView;
                dirtyLastProjectionMatrix_ = curProj;
            }
            if (width != dirtyLastWidth_ || height != dirtyLastHeight_) {
                dirtyFlag_ = true;
                dirtyLastWidth_ = width;
                dirtyLastHeight_ = height;
            }
            if (selectedIndex != dirtyLastSelectedIndex_) {
                dirtyFlag_ = true;
                dirtyLastSelectedIndex_ = selectedIndex;
            }
        }

        // Populate mode in profiler
        frameProfile_.renderMode = static_cast<int>(renderMode_);

        if (viewportDisabled_) {
            if (!NoPresentWhenViewportDisabled()) {
                RenderImGui();
                window_->SwapBuffers();
            } else {
                static double lastViewportDisabledPresentTime = 0.0;
                constexpr double kViewportDisabledPresentIntervalSec = 1.0 / 120.0;
                RenderImGui();
                const bool shouldPresent =
                    lastViewportDisabledPresentTime <= 0.0 ||
                    (currentFrame - lastViewportDisabledPresentTime) >= kViewportDisabledPresentIntervalSec;
                if (shouldPresent) {
                    window_->SwapBuffers();
                    lastViewportDisabledPresentTime = currentFrame;
                }
            }
            lastFrameDrawCalls_ = 0;
            return;
        }

        if (shaderManager) {
            shaderManager->ProcessPendingReloads();
        }
        frameProfile_.shaderReload = profElapsed();

        // Frame fence wait: bounded wait to avoid long CPU stalls when the GPU spikes.
        // NDEVC_FRAME_FENCE_WAIT_NS controls max wait per frame (default 1ms).
        // Set NDEVC_DISABLE_FRAME_FENCE_WAIT=1 to disable.
        if (FrameFenceWaitEnabled()) {
            const int waitIdx = (frameFenceIdx_ + 1) % (kMaxFramesInFlight + 1);
            if (frameFences_[waitIdx]) {
                auto tFence = ProfileClock::now();
                const uint64_t waitBudgetNs = FrameFenceWaitBudgetNs();
                const GLenum waitResult = glClientWaitSync(frameFences_[waitIdx], 0, waitBudgetNs);
                if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED || waitResult == GL_WAIT_FAILED) {
                    glDeleteSync(frameFences_[waitIdx]);
                    frameFences_[waitIdx] = nullptr;
                }
                frameProfile_.fenceWait = std::chrono::duration<double, std::milli>(
                    ProfileClock::now() - tFence).count();
            }
        }
        profLast = ProfileClock::now(); // reset after fence — sceneTick starts clean

        // Phase 1: TickLifecycle
        scene_.Tick(deltaTime, camera_);
        frameProfile_.sceneTick = profElapsed();

        // Phase 2: ApplyQueuedUpdates (transform propagation is inside Tick)

        // Phase 3: MaterializeValid — draw lists populated
        // Check for scene mutation BEFORE PrepareDrawLists so dirty-skip can fire early.
        const bool drawListsWereDirty = scene_.IsDrawListsDirty();
        if (drawListsWereDirty) dirtyFlag_ = true;

        // ── Dirty-frame skip: skip expensive draw prep and GPU passes when clean ──
        dirtyFrameSkippedLast_ = false;
        if (activePolicy_.dirtyRenderingEnabled && !dirtyFlag_) {
            dirtyFrameSkippedLast_ = true;
            frameProfile_.dirtyFrameSkipped = 1;
            RenderImGui();
            window_->SwapBuffers();
            lastFrameDrawCalls_ = 0;
            frameProfile_.frameTotal = std::chrono::duration<double, std::milli>(
                ProfileClock::now() - profFrameStart).count();
            rsFrame_.Push(static_cast<float>(frameProfile_.frameTotal));
            rsCpu_.Push(0.0f);
            rsSwap_.Push(static_cast<float>(frameProfile_.frameTotal));
            ++profileFrameCounter_;
            return;
        }
        dirtyFlag_ = false;
        frameProfile_.dirtyFrameSkipped = 0;

        scene_.PrepareDrawLists(camera_,
            solidDraws, alphaTestDraws, decalDraws, particleDraws,
            environmentDraws, environmentAlphaDraws, simpleLayerDraws,
            refractionDraws, postAlphaUnlitDraws, waterDraws, animatedDraws);
        frameProfile_.prepareDraws = profElapsed();

        // Capture draw list sizes immediately — valid for entire frame
        frameProfile_.solidCount      = static_cast<int>(solidDraws.size());
        frameProfile_.alphaCount      = static_cast<int>(alphaTestDraws.size());
        frameProfile_.slCount         = static_cast<int>(simpleLayerDraws.size());
        frameProfile_.envCount        = static_cast<int>(environmentDraws.size());
        frameProfile_.envAlphaCount   = static_cast<int>(environmentAlphaDraws.size());
        frameProfile_.decalCount      = static_cast<int>(decalDraws.size());
        frameProfile_.waterCount      = static_cast<int>(waterDraws.size());
        frameProfile_.refractionCount = static_cast<int>(refractionDraws.size());
        frameProfile_.postAlphaCount  = static_cast<int>(postAlphaUnlitDraws.size());
        frameProfile_.particleCount   = static_cast<int>(scene_.particleNodes.size());

        // Phase 4: Rebuild (only when draw lists changed)
        if (drawListsWereDirty) {
            solidShaderVarAnimatedIndicesDirty_ = true;
            InvalidateSelection();
            if (!scene_.WasMegaBufferRebuiltThisTick()) {
                MeshServer::instance().buildMegaBuffer();
                auto setMegaOffsets = [](std::vector<DrawCmd>& draws) {
                    for (auto& dc : draws) {
                        if (!dc.mesh) continue;
                        dc.megaVertexOffset = dc.mesh->megaVertexOffset;
                        dc.megaIndexOffset = dc.mesh->megaIndexOffset;
                    }
                };
                setMegaOffsets(solidDraws);
                setMegaOffsets(simpleLayerDraws);
                setMegaOffsets(alphaTestDraws);
                setMegaOffsets(decalDraws);
                setMegaOffsets(waterDraws);
                setMegaOffsets(refractionDraws);
                setMegaOffsets(environmentDraws);
                setMegaOffsets(environmentAlphaDraws);
                setMegaOffsets(postAlphaUnlitDraws);
                setMegaOffsets(particleDraws);
            }
            ApplyDisabledDrawFlags();
            visGridRevealedAll_ = false; // force visibility re-evaluation after scene change
            BuildVisibilityGrids();
            solidBatchSystem_.reset(true);
            alphaTestBatchSystem_.reset(true);
            solidBatchSystem_.InvalidateCullCache();
            alphaTestBatchSystem_.InvalidateCullCache();
            environmentBatchSystem_.reset(true);
            environmentAlphaBatchSystem_.reset(true);
            decalBatchSystem_.reset(true);
            slGBufCacheValid_ = false;
            slViewProjCacheValid_ = false;
            shadowCastersDirty_ = true;
            solidShadowGeomGroups_.clear();
            materialSSBODirty_ = true;
        }

        if (materialSSBODirty_) {
            const auto tSSBO = ProfileClock::now();
            buildMaterialSSBO();
            frameProfile_.materialSSBO = std::chrono::duration<double, std::milli>(ProfileClock::now() - tSSBO).count();
        }

        if (solidShaderVarAnimatedIndicesDirty_) {
            solidShaderVarAnimatedIndices.clear();
            for (size_t i = 0; i < solidDraws.size(); ++i) {
                if (solidDraws[i].hasShaderVarAnimations)
                    solidShaderVarAnimatedIndices.push_back(i);
            }
            solidShaderVarAnimatedIndicesDirty_ = false;
        }
        frameProfile_.rebuild = profElapsed();

        // ── [PARITY][FRAME] diagnostics (throttled) — measured inline, outside all buckets ──
        {
            const int parityFrame = scene_.GetParityCounters().submittedDrawCommands;
            const auto& re = scene_.GetRuntimeEntities();
            static int parityFrameLogCounter = 0;
            ++parityFrameLogCounter;
            if (drawListsWereDirty || parityFrameLogCounter >= 300) {
                const int nValid = scene_.CountRuntimeEntitiesInState(
                    SceneManager::EntityLifecycleState::Valid);
                const int nPending = scene_.CountRuntimeEntitiesInState(
                    SceneManager::EntityLifecycleState::PendingValid);
                const int nCreated = scene_.CountRuntimeEntitiesInState(
                    SceneManager::EntityLifecycleState::Created);
                const int nActivated = scene_.CountRuntimeEntitiesInState(
                    SceneManager::EntityLifecycleState::Activated);
                const auto& solidMetrics = solidBatchSystem_.lastFlushMetrics();
                const auto& alphaMetrics = alphaTestBatchSystem_.lastFlushMetrics();
                NC::LOGGING::Log("[PARITY][FRAME] seq=", parityFrameLogCounter,
                    " entities=", static_cast<int>(re.size()),
                    " valid=", nValid, " pending=", nPending,
                    " created=", nCreated, " activated=", nActivated,
                    " drawsDirty=", drawListsWereDirty ? 1 : 0,
                    " solid=", solidDraws.size(),
                    " alpha=", alphaTestDraws.size(),
                    " sl=", simpleLayerDraws.size(),
                    " submittedCmds=", parityFrame,
                    " solidBatches=", solidMetrics.batchCount,
                    " solidCmds=", solidMetrics.commandCount,
                    " alphaBatches=", alphaMetrics.batchCount,
                    " alphaCmds=", alphaMetrics.commandCount);
                parityFrameLogCounter = 0;
            }
        }
        profElapsed(); // reset profiler timer after parity to exclude its cost from animationTick

        // Scene mutation is a dirty signal (for next frame)
        if (drawListsWereDirty) {
            dirtyFlag_ = true;
        }

        // Phase 5: Batch/Flush — frustum, animation, render passes
        frameFrustum_ = camera_.extractFrustum(camera_.getProjectionMatrix() * camera_.getViewMatrix());
        const Camera::Frustum& frustum = frameFrustum_;

        const bool hasAnyDraws = !solidDraws.empty() || !alphaTestDraws.empty()
            || !environmentDraws.empty() || !environmentAlphaDraws.empty()
            || !simpleLayerDraws.empty() || !decalDraws.empty()
            || !waterDraws.empty() || !refractionDraws.empty()
            || !postAlphaUnlitDraws.empty() || !animatedDraws.empty();

        if (!AnimationsDisabled() && hasAnyDraws) {
            DeferredRendererAnimation::TickAnimationFrame(deltaTime, animatedDraws, frustum);

            for (auto& animatorInstance : scene_.animatorInstances) {
                if (animatorInstance) {
                    animatorInstance->Animate(currentFrame);
                }
            }
            DeferredRendererAnimation::BeginFrameAnimationCaches();
            DeferredRendererAnimation::ApplyAnimatedDrawWorldTransforms(animatedDraws, frustum);

            auto applyShaderVarAnimationsToDraws = [&](std::vector<DrawCmd>& draws) {
                for (auto& dc : draws) {
                    if (!dc.instance || dc.disabled || !dc.hasShaderVarAnimations || dc.frustumCulled) continue;

                    float dcAnimTime = static_cast<float>(currentFrame);
                    auto itSpawn = scene_.instanceSpawnTimes.find(dc.instance);
                    if (itSpawn != scene_.instanceSpawnTimes.end()) {
                        dcAnimTime = std::max(0.0f, static_cast<float>(currentFrame - itSpawn->second));
                    }

                    const auto animParams = DeferredRendererAnimation::SampleShaderVarAnimationsForTarget(dc.nodeName, dcAnimTime, dc.instance);
                    for (const auto& [paramName, value] : animParams) {
                        dc.shaderParamsFloat[paramName] = value;
                    }
                }
            };

            if (!solidShaderVarAnimatedIndices.empty()) {
                applyShaderVarAnimationsToDraws(solidDraws);
                applyShaderVarAnimationsToDraws(alphaTestDraws);
                applyShaderVarAnimationsToDraws(simpleLayerDraws);
                applyShaderVarAnimationsToDraws(environmentDraws);
                applyShaderVarAnimationsToDraws(environmentAlphaDraws);
                applyShaderVarAnimationsToDraws(decalDraws);
                applyShaderVarAnimationsToDraws(waterDraws);
                applyShaderVarAnimationsToDraws(refractionDraws);
                applyShaderVarAnimationsToDraws(postAlphaUnlitDraws);
            }
        }
        frameProfile_.animatedCount  = static_cast<int>(animatedDraws.size());
        frameProfile_.animationTick  = profElapsed(); // covers: frustum + animation

        if (width <= 0 || height <= 0) {
            return;
        }

        glm::mat4 viewMatrix = camera_.getViewMatrix();

        static int frameCount = 0;
        frameCount++;
        frameDrawCalls_ = 0;

        // NOTE: ApplyDisabledDrawFlags() must NOT be called here every frame.
        // It resets dc.disabled = false for every draw (when the debug disabled set is empty),
        // which wipes out the visibility system's per-frame disabled flags before
        // UpdateVisibilityThisFrame() can run. It is correctly called only when the
        // debug-disabled set actually changes (SetDrawDisabled / ClearDisabledDraws).

        if (hasAnyDraws || !scene_.particleNodes.empty()) {
            // ImGui/backends can leave GL flags changed; normalize baseline for frame graphs.
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_STENCIL_TEST);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            if (FaceCullingDisabled()) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        frameProfile_.prePassSetup = profElapsed(); // GL baseline state reset

        // Invalidate the device render state cache — raw GL calls above bypass it.
        if (device_) device_->InvalidateRenderStateCache();

        if (!useLegacyDeferredInit_) {
            if (hasAnyDraws || !scene_.particleNodes.empty()) {
                // Lazy-init GPU timer queries
                if (!gpuQueriesInit_) {
                    glGenQueries(kGpuQueryCount, gpuQueriesDB_[0]);
                    glGenQueries(kGpuQueryCount, gpuQueriesDB_[1]);
                    glGenQueries(kGpuGeomTimestampQueryCount, gpuGeomTsDB_[0]);
                    glGenQueries(kGpuGeomTimestampQueryCount, gpuGeomTsDB_[1]);
                    glGenQueries(kGpuFwdTimestampQueryCount, gpuFwdTsDB_[0]);
                    glGenQueries(kGpuFwdTimestampQueryCount, gpuFwdTsDB_[1]);
                    gpuQueriesInit_ = true;
                }


                glBeginQuery(GL_TIME_ELAPSED, gpuQueriesDB_[gpuQueryBufWrite_][0]);
                shadowGraph->execute();
                glEndQuery(GL_TIME_ELAPSED);
                frameProfile_.shadowPass = profElapsed();

                glBeginQuery(GL_TIME_ELAPSED, gpuQueriesDB_[gpuQueryBufWrite_][1]);
                geometryGraph->execute();
                glEndQuery(GL_TIME_ELAPSED);
                frameProfile_.geometryPass = profElapsed();
                {
                    const auto& sm = solidBatchSystem_.lastFlushMetrics();
                    const auto& am = alphaTestBatchSystem_.lastFlushMetrics();
                    const auto& em = environmentBatchSystem_.lastFlushMetrics();
                    const auto& dm = decalBatchSystem_.lastFlushMetrics();
                    frameProfile_.solidBatchCount = sm.batchCount;
                    frameProfile_.alphaBatchCount = am.batchCount;
                    frameProfile_.envBatchCount   = em.batchCount;
                    frameProfile_.decalBatchCount = dm.batchCount;
                }

                glBeginQuery(GL_TIME_ELAPSED, gpuQueriesDB_[gpuQueryBufWrite_][2]);
                decalGraph->execute();
                glEndQuery(GL_TIME_ELAPSED);
                frameProfile_.decalPass = profElapsed();

                glBeginQuery(GL_TIME_ELAPSED, gpuQueriesDB_[gpuQueryBufWrite_][3]);
                lightingGraph->execute();
                glEndQuery(GL_TIME_ELAPSED);
                frameProfile_.lightingPass = profElapsed();

                glBeginQuery(GL_TIME_ELAPSED, gpuQueriesDB_[gpuQueryBufWrite_][4]);
                particleGraph->execute();
                glEndQuery(GL_TIME_ELAPSED);
                frameProfile_.forwardPass = profElapsed();

                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                if (device_) {
                    device_->SetViewport({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f});
                    if (cachedBlitState_) device_->ApplyRenderState(cachedBlitState_.get());
                }

                {
                    const auto tBlit = ProfileClock::now();
                    if (editorModeEnabled_) {
                        glDisable(GL_DEPTH_TEST);
                        glDepthMask(GL_FALSE);
                        glDisable(GL_BLEND);
                        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                        glDepthMask(GL_TRUE);
                    } else {
                        auto blitShader = shaderManager->GetShader("blit");
                        if (blitShader) {
                            blitShader->Use();
                            blitShader->SetInt("tex", 0);
                            GLuint finalTex = GetFrameGraphTexture(lightingGraph, "sceneColor");
                            if (optRenderLOG) std::cout << "[MAIN] Final blit - texture=" << finalTex << " screenVAO=" << screenVAO << "\n";
                            bindTexture(0, finalTex);
                            glBindVertexArray(screenVAO);
                            glDrawArrays(GL_TRIANGLES, 0, 3);
                            glBindVertexArray(0);
                            if (gEnableGLErrorChecking) {
                                GLenum err = glGetError();
                                if (err != GL_NO_ERROR) std::cerr << "[MAIN] Blit error: 0x" << std::hex << err << std::dec << "\n";
                            }
                        } else {
                            std::cerr << "[MAIN] ERROR: Blit shader not found!\n";
                        }
                    }
                    frameProfile_.blitCompose = std::chrono::duration<double, std::milli>(ProfileClock::now() - tBlit).count();
                }
            }
        }
        frameProfile_.editorClear = profElapsed();

        if (useLegacyDeferredInit_) {
            DumpGLState("BEFORE GEOMETRY PASS");
            glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
            glViewport(0, 0, width, height);
            checkGLError("After viewport setup");

            GLenum bufs[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
            glDrawBuffers(5, bufs);
            checkGLError("After glDrawBuffers");

            if (optRenderLOG)  {
                std::cout << "  G-Buffer State:\n";
                std::cout << "    FBO: " << gBuffer << "\n";
                std::cout << "    Viewport: " << width << "x" << height << "\n";
                std::cout << "    Attachments: gPositionVS=" << gPositionVS
                          << " gNormalDepth=" << gNormalDepthPacked
                          << " gAlbedoSpec=" << gAlbedoSpec
                          << " gPositionWS=" << gPositionWS
                          << " gDepth=" << gDepth << "\n";

                GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                std::cout << "    FBO Status: 0x" << std::hex << fboStatus << std::dec;
                if (fboStatus == GL_FRAMEBUFFER_COMPLETE) std::cout << " (COMPLETE)";
                else std::cout << " (INCOMPLETE!)";
                std::cout << "\n";

                for (int i = 0; i < 5; i++) {
                    GLint attachment = 0;
                    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachment);
                    std::cout << "    COLOR_ATTACHMENT" << i << " = " << attachment << "\n";
                }

                GLint depthAttachment = 0;
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthAttachment);
                std::cout << "    DEPTH_STENCIL_ATTACHMENT = " << depthAttachment << "\n";
            }

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            constexpr GLfloat c0[4] = { 0,0,0,0 };
            glClearBufferfv(GL_COLOR, 0, c0);
            glClearBufferfv(GL_COLOR, 1, c0);
            glClearBufferfv(GL_COLOR, 2, c0);
            glClearBufferfv(GL_COLOR, 3, c0);
            glClearBufferfv(GL_COLOR, 4, c0);
            glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            checkGLError("After G-buffer clear");

            if (optRenderLOG)  std::cout << "  Cleared G-buffer attachments\n";

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            checkGLError("After GL state setup");

            if (optRenderLOG)  {
                std::cout << "  GL State: DepthTest=ON DepthFunc=LESS Blend=OFF Stencil=ON Cull=BACK\n";
            }

            auto standardShaderProgram = shaderManager->GetShader("standard");
            auto shaderProgram = standardShaderProgram ? standardShaderProgram : shaderManager->GetShader("NDEVCdeferred");
            const char* geometryShaderName = standardShaderProgram ? "standard" : "NDEVCdeferred";
            if(!shaderProgram) {
                std::cerr << "ERROR: Shader standard/NDEVCdeferred not found!\n";
                return;
            }

            GLuint progID = shaderProgram->GetNativeHandle()
                ? *reinterpret_cast<GLuint*>(shaderProgram->GetNativeHandle())
                : 0;
            if (progID == 0) {
                std::cerr << "ERROR: Shader program ID is 0!\n";
                return;
            }

            GLint linkStatus = 0;
            glGetProgramiv(progID, GL_LINK_STATUS, &linkStatus);
            if (linkStatus != GL_TRUE) {
                std::cerr << "ERROR: Shader program not properly linked!\n";
                GLint logLength = 0;
                glGetProgramiv(progID, GL_INFO_LOG_LENGTH, &logLength);
                if (logLength > 0) {
                    std::vector<char> log(logLength);
                    glGetProgramInfoLog(progID, logLength, nullptr, log.data());
                    std::cerr << "Link error: " << log.data() << "\n";
                }
                return;
            }

            if (optRenderLOG)  std::cout << "    " << geometryShaderName << " shader found, using it...\n";
            shaderProgram->Use();
            checkGLError("After shader use");

            shaderProgram->SetMat4("projection", camera_.getProjectionMatrix());
            shaderProgram->SetMat4("view", viewMatrix);
            shaderProgram->SetFloat("MatEmissiveIntensity", 0.0f);
            shaderProgram->SetFloat("MatSpecularIntensity", 0.0f);
            shaderProgram->SetFloat("MatSpecularPower", 32.0f);
            shaderProgram->SetInt("UseSkinning", 0);
            shaderProgram->SetInt("UseInstancing", 1);
            shaderProgram->SetInt("alphaTest", 0);
            shaderProgram->SetFloat("alphaCutoff", 0.5f);
            shaderProgram->SetInt("twoSided", 0);
            shaderProgram->SetInt("isFlatNormal", 0);

            glm::mat4 viewProj = camera_.getProjectionMatrix() * camera_.getViewMatrix();
            const Camera::Frustum& frustum = frameFrustum_;
            UpdateVisibilityThisFrame(frustum);
            alphaTestBatchSystem_.updateStaticVisibility(alphaTestDraws, &frustum);
            environmentBatchSystem_.updateStaticVisibility(environmentDraws, &frustum);
            environmentAlphaBatchSystem_.updateStaticVisibility(environmentAlphaDraws, &frustum);

            shaderProgram->SetInt("DiffMap0", 0);
            shaderProgram->SetInt("SpecMap0", 1);
            shaderProgram->SetInt("BumpMap0", 2);
            shaderProgram->SetInt("EmsvMap0", 3);
            shaderProgram->SetInt("diffMapSampler", 0);
            shaderProgram->SetInt("specMapSampler", 1);
            shaderProgram->SetInt("bumpMapSampler", 2);
            shaderProgram->SetInt("emsvSampler", 3);
            shaderProgram->SetVec4("fogDistances", glm::vec4(180.0f, 520.0f, 0.0f, 0.0f));
            shaderProgram->SetVec4("fogColor", glm::vec4(0.61f, 0.58f, 0.52f, 0.0f));
            shaderProgram->SetVec4("heightFogColor", glm::vec4(0.61f, 0.58f, 0.52f, 100000.0f));
            shaderProgram->SetVec4("pixelSize", glm::vec4(
                1.0f / static_cast<float>(std::max(width, 1)),
                1.0f / static_cast<float>(std::max(height, 1)),
                0.0f,
                0.0f));
            shaderProgram->SetFloat("encodefactor", 1.0f);
            shaderProgram->SetFloat("alphaBlendFactor", 1.0f);
            shaderProgram->SetFloat("mayaAnimableAlpha", 1.0f);
            shaderProgram->SetFloat("AlphaClipRef", 128.0f);
            shaderProgram->SetVec4("customColor2", glm::vec4(0.0f));

            glm::mat4 identityUV = glm::mat4(1.0f);
            shaderProgram->SetMat4("textureTransform0", identityUV);

            if (optRenderLOG)  {
                std::cout << "\n[FRAME " << frameCount << "] Rendering Stats:\n";
                std::cout << "  SolidDraws: " << solidDraws.size() << " objects\n";
                std::cout << "  AlphaTestDraws: " << alphaTestDraws.size() << " objects\n";
                std::cout << "  SimpleLayerDraws: " << simpleLayerDraws.size() << " objects\n";
                std::cout << "  EnvironmentDraws: " << environmentDraws.size() << " objects\n";
                std::cout << "  EnvironmentAlphaDraws: " << environmentAlphaDraws.size() << " objects\n";
                std::cout << "  RefractionDraws: " << refractionDraws.size() << " objects\n";
                std::cout << "  WaterDraws: " << waterDraws.size() << " objects\n";
                std::cout << "  PostAlphaUnlitDraws: " << postAlphaUnlitDraws.size() << " objects\n";
                std::cout << "  scene_.particleNodes: " << scene_.particleNodes.size() << " nodes\n";
            }

            if (optRenderLOG)  std::cout << "\n  --- Solid Draws Pass ---\n";
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            checkGLError("After stencil setup");

            if (!visResolveSkipped_) solidBatchSystem_.MarkStaticVisibilityDirty();
            solidBatchSystem_.cull(solidDraws, frustum);
            checkGLError("After bucket culling");

            bindTexture(4, whiteTex);
            bindSampler(4, gSamplerRepeat);
            solidBatchSystem_.flush(samplerRepeat_abstracted.get(), 4, progID);
            bindTexture(4, 0);
            bindSampler(4, 0);
            checkGLError("After solid draws flush");

            if (optRenderLOG)  {
                GLint currentFBO;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
                std::cout << "  Solid draws completed. FBO=" << currentFBO << "\n";
            }

            if (!alphaTestDraws.empty()) {
                if (optRenderLOG)  std::cout << "\n  --- Alpha Test Draws Pass ---\n";
                if (optRenderLOG)  std::cout << "  Alpha test objects: " << alphaTestDraws.size() << "\n";

                shaderProgram->Use();
                checkGLError("After shader use (alpha test)");

                shaderProgram->SetMat4("projection", camera_.getProjectionMatrix());
                shaderProgram->SetMat4("view", viewMatrix);
                shaderProgram->SetMat4("textureTransform0", identityUV);
                shaderProgram->SetInt("UseSkinning", 0);
                shaderProgram->SetInt("UseInstancing", 0);
                shaderProgram->SetInt("alphaTest", 1);
                shaderProgram->SetInt("twoSided", 0);
                shaderProgram->SetInt("isFlatNormal", 0);
                checkGLError("After shader uniforms (alpha test)");

                shaderProgram->SetInt("DiffMap0", 0);
                shaderProgram->SetInt("SpecMap0", 1);
                shaderProgram->SetInt("BumpMap0", 2);
                shaderProgram->SetInt("EmsvMap0", 3);
                checkGLError("After texture slot setup (alpha test)");

                int renderedCount = 0;
                for (auto& dc : alphaTestDraws) {
                    if (!dc.mesh) continue;

                    glm::vec3 localCenter = (glm::vec3(dc.localBoxMin) +
                                             glm::vec3(dc.localBoxMax)) * 0.5f;
                    const float localRadius = glm::length(glm::vec3(dc.localBoxMax) -
                                                          glm::vec3(dc.localBoxMin)) * 0.5f;
                    const float sx = glm::length(glm::vec3(dc.worldMatrix[0]));
                    const float sy = glm::length(glm::vec3(dc.worldMatrix[1]));
                    const float sz = glm::length(glm::vec3(dc.worldMatrix[2]));
                    float radius = localRadius * std::max(sx, std::max(sy, sz));
                    glm::vec3 worldCenter = glm::vec3(dc.worldMatrix * glm::vec4(localCenter, 1.0f));
                    bool visible = true;
                    for (int i = 0; i < 6; ++i) {
                        float dist = glm::dot(frustum.planes[i].normal, worldCenter) +
                                     frustum.planes[i].d;
                        if (dist < -radius * 1.2f) { visible = false; break; }
                    }
                    if (!visible) continue;

                    shaderProgram->SetMat4("model", dc.worldMatrix);
                    shaderProgram->SetInt("ReceivesDecals", dc.receivesDecals ? 1 : 0);
                    shaderProgram->SetFloat("alphaCutoff", dc.alphaCutoff);

                    bindTexture(0, toTextureHandle(dc.tex[0]));
                    bindTexture(1, toTextureHandle(dc.tex[1]));
                    bindTexture(2, toTextureHandle(dc.tex[2]));
                    bindTexture(3, toTextureHandle(dc.tex[3]));
                    bindTexture(4, dc.tex[8] ? toTextureHandle(dc.tex[8]) : whiteTex);

                    if (dc.group >= 0) dc.mesh->drawGroup(dc.group);
                    else dc.mesh->drawMulti();
                    checkGLError("After alpha test draw call");
                    renderedCount++;
                }
                if (optRenderLOG)  std::cout << "  AlphaTest rendered: " << renderedCount << "/" << alphaTestDraws.size() << "\n";

                glBindVertexArray(0);
                checkGLError("After unbind VAO (alpha test)");
            }


            if (optRenderLOG)  {
                DumpGLState("AFTER GEOMETRY DRAWS");
                std::cout << "\n========== GEOMETRY PASS END ==========\n";
            }

            if (optRenderLOG)  {
            std::cout << "\n========== SHADOW PASS START ==========\n";
        }

        glm::vec3 camPos = camera_.getPosition();
        glm::vec3 camForward = camera_.getForward();

        if (optRenderLOG)  {
            std::cout << "  Camera pos: (" << camPos.x << ", " << camPos.y << ", " << camPos.z << ")\n";
            std::cout << "  Camera forward: (" << camForward.x << ", " << camForward.y << ", " << camForward.z << ")\n";
            std::cout << "  Rendering " << NUM_CASCADES << " shadow cascades\n";
            for (int i = 0; i < NUM_CASCADES; i++) {
                std::cout << "    Cascade " << i << " FBO=" << shadowFBOCascades[i] << " Tex=" << shadowMapCascades[i] << "\n";
            }
        }

       // renderCascadedShadows(camPos, camForward);
        checkGLError("After cascaded shadows");

        if (optRenderLOG)  {
            DumpGLState("AFTER SHADOW PASS");
        }

        if (optRenderLOG)  {
            std::cout << "  Verifying shadow maps after rendering:\n";
            for (int i = 0; i < NUM_CASCADES; i++) {
                glBindTexture(GL_TEXTURE_2D, shadowMapCascades[i]);
                GLint width, height, format;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
                std::cout << "    Cascade " << i << ": " << width << "x" << height
                          << " format=0x" << std::hex << format << std::dec << "\n";
            }
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if (optRenderLOG)  std::cout << "========== SHADOW PASS END ==========\n";

        // ==================== LIGHT ACCUMULATION PASS ====================
        glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);


        auto shaderLight = shaderManager->GetShader("lighting");
        if(!shaderLight) {
            std::cerr << "ERROR: Shader lighting not found!\n";
            return;
        }

        shaderLight->Use();
        shaderLight->SetVec3("CameraPos", camPos);
        glm::vec3 lightDir = kLightDirToSun;
        shaderLight->SetVec3("LightDirWS", lightDir);
        glm::vec3 lightColor = glm::vec3(1.0f, 0.96f, 0.9f);
        shaderLight->SetVec3("LightColor", lightColor);
        glm::vec3 ambientColor = glm::vec3(0.35f, 0.35f, 0.45f);
        shaderLight->SetVec3("AmbientColor", ambientColor);
        glm::vec3 backLightColor = glm::vec3(0.0f, 0.0f, 0.0f);
        shaderLight->SetVec3("BackLightColor", backLightColor);
        shaderLight->SetFloat("BackLightOffset", 0.0f);
        constexpr int kLegacyMaxCascades = 4;
        const int legacyCascadeCount = (NUM_CASCADES < kLegacyMaxCascades) ? NUM_CASCADES : kLegacyMaxCascades;
        shaderLight->SetMat4Array("lightSpaceMatrices", lightSpaceMatrices, legacyCascadeCount);
        for (int i = 0; i < legacyCascadeCount + 1; i++) {
            shaderLight->SetFloat(("cascadeSplits[" + std::to_string(i) + "]").c_str(), cascadeSplits[i]);
        }

        shaderLight->SetMat4("view", cachedViewMatrix);
        this->bindTexture(0, gNormalDepthPacked);
        if (device_) { device_->BindSampler(nullptr, 0); } else { glBindSampler(0, 0); }
        this->bindTexture(1, gPositionWS);
        if (device_) { device_->BindSampler(nullptr, 1); } else { glBindSampler(1, 0); }

        for (int i = 0; i < legacyCascadeCount; i++) {
            this->bindTexture(3 + i, shadowMapCascades[i]);
            if (device_ && samplerShadow_abstracted) {
                device_->BindSampler(samplerShadow_abstracted.get(), 3 + i);
            } else {
                glBindSampler(3 + i, gSamplerShadow);
            }
        }

        shaderLight->SetInt("gNormalDepthPacked", 0);
        shaderLight->SetInt("gPositionWS", 1);
        shaderLight->SetInt("numCascades", legacyCascadeCount);
        shaderLight->SetInt("DisableShadows", kDisableShadows ? 1 : 0);
        shaderLight->SetInt("DisableViewDependentSpecular", kDisableViewDependentSpecular ? 1 : 0);
        if (legacyCascadeCount > 0) shaderLight->SetInt("shadowMapCascade0", 3);
        if (legacyCascadeCount > 1) shaderLight->SetInt("shadowMapCascade1", 4);
        if (legacyCascadeCount > 2) shaderLight->SetInt("shadowMapCascade2", 5);
        if (legacyCascadeCount > 3) shaderLight->SetInt("shadowMapCascade3", 6);

        glBindVertexArray(screenVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        for (int i = 3; i < 3 + legacyCascadeCount; i++) {
            if (device_) { device_->BindSampler(nullptr, i); } else { glBindSampler(i, 0); }
        }
        this->clearError("Legacy::AfterGBufferBinds");


        if (!kDisablePointLightPass && !pointLights.empty()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            auto pointLight = shaderManager->GetShader("pointLight");
            if (pointLight) {
                pointLight->Use();
                pointLight->SetInt("UseInstancedPointLights", 0);
                pointLight->SetVec3("CameraPos", camPos);
                pointLight->SetVec2("screenSize", glm::vec2(static_cast<float>(width), static_cast<float>(height)));

                bindTexture(0, gNormalDepthPacked);
                glBindSampler(0, 0);
                bindTexture(1, gPositionWS);
                glBindSampler(1, 0);
                pointLight->SetInt("gNormalDepthPacked", 0);
                pointLight->SetInt("gPositionWS", 1);

                glBindVertexArray(sphereVAO);
                for (const auto& pl : pointLights) {
                    glm::mat4 sphereModel = glm::translate(glm::mat4(1.0f), pl.position);
                    sphereModel = glm::scale(sphereModel, glm::vec3(pl.range));
                    glm::mat4 mvp = cachedProjectionMatrix * cachedViewMatrix * sphereModel;
                    pointLight->SetMat4("mvp", mvp);
                    pointLight->SetVec4("lightPosRange", glm::vec4(pl.position.x, pl.position.y, pl.position.z, std::max(0.001f, pl.range)));
                    pointLight->SetVec4("lightColorIn", pl.color);
                    glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, nullptr);
                }
                glBindVertexArray(0);
            } else {
                std::cerr << "pointLight shader not found\n";
            }

            glDisable(GL_BLEND);
            glCullFace(GL_BACK);
            clearError("Legacy::AfterPointLights");
        }

        for (int i = 0; i < 8; i++) {
            bindTexture(i, 0);
            bindSampler(i, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        if (kDisableCompositionPass) {
            if (optRenderLOG) std::cout << "[LEGACY] composition disabled, blitting gAlbedoSpec\n";
            auto blit = shaderManager->GetShader("blit");
            if (blit) {
                blit->Use();
                this->bindTexture(0, gAlbedoSpec);
                if (device_) { device_->BindSampler(nullptr, 0); } else { glBindSampler(0, 0); }
                blit->SetInt("tex", 0);
                glBindVertexArray(screenVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                this->bindTexture(0, 0);
            } else {
                std::cerr << "blit shader not found\n";
            }
        } else {
            auto composition = shaderManager->GetShader("lightComposition");
            if (!composition) {
                std::cerr << "composition shader not found/not working\n";
            } else {
            composition->Use();
            glm::vec4 fogCol(0.5f, 0.5f, 0.5f, 0.0f);
            glm::vec4 fogDist(0.0f, 10000.0f, 0.0f, 1.0f);
            composition->SetVec4("fogColor", fogCol);
            composition->SetVec4("fogDistances", fogDist);

            this->bindTexture(0, lightBuffer);
            if (device_) { device_->BindSampler(nullptr, 0); } else { glBindSampler(0, 0); }
            this->bindTexture(1, gAlbedoSpec);
            if (device_) { device_->BindSampler(nullptr, 1); } else { glBindSampler(1, 0); }
            this->bindTexture(2, gEmissive);
            if (device_) { device_->BindSampler(nullptr, 2); } else { glBindSampler(2, 0); }
            this->bindTexture(3, gNormalDepthPacked);
            if (device_) { device_->BindSampler(nullptr, 3); } else { glBindSampler(3, 0); }
            this->bindTexture(4, gDepth);
            if (device_) { device_->BindSampler(nullptr, 4); } else { glBindSampler(4, 0); }

            composition->SetInt("lightBufferTex", 0);
            composition->SetInt("gAlbedoSpec", 1);
            composition->SetInt("gEmissiveTex", 2);
            composition->SetInt("gNormalDepthPacked", 3);
            composition->SetInt("gDepthTex", 4);
            composition->SetMat4("invProjection", glm::inverse(cachedProjectionMatrix));
            composition->SetInt("DisableLighting", kDisableLighting ? 1 : 0);
            composition->SetInt("DisableFog", kDisableFog ? 1 : 0);

            glBindVertexArray(screenVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            this->clearError("Legacy::AfterCompositionDraw");

            for (int i = 0; i < 8; i++) {
                this->bindTexture(i, 0);
                if (device_) { device_->BindSampler(nullptr, i); } else { glBindSampler(i, 0); }
            }
            }
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFBO);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        checkGLError("After depth blit to sceneFBO");

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        if (optRenderLOG)  std::cout << "========== LIGHTING COMPOSITION END ==========\n";
        checkGLError("After composition");

        // ==================== NEBULA COMPOSITION PASS ====================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);

        if (kDisableComposePass) {
            if (optRenderLOG) std::cout << "[LEGACY] compose disabled, blitting sceneColor\n";
            auto blit = shaderManager->GetShader("blit");
            if (blit) {
                blit->Use();
                bindTexture(0, sceneColor);
                glBindSampler(0, 0);
                blit->SetInt("tex", 0);
                glBindVertexArray(screenVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
            } else {
                std::cerr << "blit shader not found\n";
            }
        } else {
            auto compose = shaderManager->GetShader("lightCompose");
            if (compose) {
                compose->Use();
                bindTexture(0, sceneColor);
                glBindSampler(0, 0);
                compose->SetInt("sceneColorTex", 0);
                compose->SetFloat("saturation", 1.0f);
                compose->SetVec4("balance", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                compose->SetFloat("fadeValue", 1.0f);
                compose->SetVec4("luminance", glm::vec4(0.299f, 0.587f, 0.114f, 0.0f));

                glBindVertexArray(screenVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
            } else {
                std::cerr << "lightCompose shader not found\n";
            }
        }
        clearError("Legacy::AfterFinalBlit");

        glEnable(GL_DEPTH_TEST);
        }

        {
            static double lastEventDebugKeyTime = 0.0;
            const double now = glfwGetTime();
            auto pressed = [this, allowViewportKeyboardInput](int key) {
                return allowViewportKeyboardInput && inputSystem_->IsKeyPressed(key);
            };
            auto debounce = [&](bool cond) {
                return cond && (now - lastEventDebugKeyTime) > 0.2;
            };

            const size_t eventsCount = scene_.currentMap ? scene_.currentMap->event_mapping.size() : 0;

            if (debounce(pressed(GLFW_KEY_F5))) {
                filterEventsOnly = !filterEventsOnly;
                lastEventDebugKeyTime = now;
                std::cout << "[EVENT] mode=" << EventModeText(filterEventsOnly)
                          << " bit=" << activeEventFilterIndex << "\n";
                scene_.UpdateIncrementalStreaming(true);
            }
            if (debounce(pressed(GLFW_KEY_F6))) {
                if (eventsCount > 0) {
                    activeEventFilterIndex = (activeEventFilterIndex + 1) % (int)eventsCount;
                } else {
                    activeEventFilterIndex = 0;
                }
                lastEventDebugKeyTime = now;
                std::cout << "[EVENT] bit -> " << activeEventFilterIndex << "\n";
                if (filterEventsOnly) scene_.UpdateIncrementalStreaming(true);
            }
            if (debounce(pressed(GLFW_KEY_F7))) {
                if (eventsCount > 0) {
                    activeEventFilterIndex = (activeEventFilterIndex - 1 + (int)eventsCount) % (int)eventsCount;
                } else {
                    activeEventFilterIndex = 0;
                }
                lastEventDebugKeyTime = now;
                std::cout << "[EVENT] bit -> " << activeEventFilterIndex << "\n";
                if (filterEventsOnly) scene_.UpdateIncrementalStreaming(true);
            }
            if (debounce(pressed(GLFW_KEY_F8))) {
                lastEventDebugKeyTime = now;
                DebugPrintNearestInstances(scene_.currentMap, camera_.getPosition(), 25);
            }
        }

        static bool eKeyWasPressed = false;
        bool eKeyPressed = allowViewportKeyboardInput && inputSystem_->IsKeyPressed(GLFW_KEY_E);
        if (eKeyPressed && !eKeyWasPressed) {
            std::cout << "\n[E] Reloading map from camera position...\n";
            ReloadMapWithCurrentMode();
        }
        eKeyWasPressed = eKeyPressed;

        static bool plusKeyWasPressed = false;
        bool plusKeyPressed = allowViewportKeyboardInput && (inputSystem_->IsKeyPressed(GLFW_KEY_EQUAL) ||
                              inputSystem_->IsKeyPressed(GLFW_KEY_KP_ADD));
        if (plusKeyPressed && !plusKeyWasPressed) {
            maxInstancesPerFrame += 500;
            std::cout << "\n[+] Increasing instance limit to " << maxInstancesPerFrame << "\n";
            scene_.UpdateIncrementalStreaming(true);
        }
        plusKeyWasPressed = plusKeyPressed;

        static bool minusKeyWasPressed = false;
        bool minusKeyPressed = allowViewportKeyboardInput && (inputSystem_->IsKeyPressed(GLFW_KEY_MINUS) ||
                               inputSystem_->IsKeyPressed(GLFW_KEY_KP_SUBTRACT));
        if (minusKeyPressed && !minusKeyWasPressed) {
            maxInstancesPerFrame = std::max(100, maxInstancesPerFrame - 500);
            std::cout << "\n[-] Decreasing instance limit to " << maxInstancesPerFrame << "\n";
            scene_.UpdateIncrementalStreaming(true);
        }
        minusKeyWasPressed = minusKeyPressed;

        static bool f9WasPressed = false;
        bool f9Pressed = allowViewportKeyboardInput && inputSystem_->IsKeyPressed(GLFW_KEY_F9);
        if (f9Pressed && !f9WasPressed) {
            debugShadowView = !debugShadowView;
            std::cout << "\n[F9] Shadow debug view " << (debugShadowView ? "ON" : "OFF") << "\n";
        }
        f9WasPressed = f9Pressed;

        static bool f10WasPressed = false;
        bool f10Pressed = allowViewportKeyboardInput && inputSystem_->IsKeyPressed(GLFW_KEY_F10);
        if (f10Pressed && !f10WasPressed) {
            debugShadowCascade = (debugShadowCascade + 1) % NUM_CASCADES;
            std::cout << "\n[F10] Shadow debug cascade " << debugShadowCascade << "\n";
        }
        f10WasPressed = f10Pressed;

        static bool f11WasPressed = false;
        bool f11Pressed = allowViewportKeyboardInput && inputSystem_->IsKeyPressed(GLFW_KEY_F11);
        f11WasPressed = f11Pressed;

        profElapsed(); // reset timer before RenderImGui
        RenderImGui();
        frameProfile_.imguiRender = profElapsed();

        bool uiWantsMouse = false;
        if (ImGui::GetCurrentContext()) {
            uiWantsMouse = ImGui::GetIO().WantCaptureMouse;
        }
        static bool lmbWasPressed = false;
        const bool lmbPressed = glfwGetMouseButton((GLFWwindow*)window_->GetNativeHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool allowViewportMousePick = false;
        if (editorModeEnabled_ && editorViewportInputRouting_) {
            double cursorX = 0.0;
            double cursorY = 0.0;
            window_->GetCursorPos(cursorX, cursorY);
            allowViewportMousePick = IsSceneViewportPointerInside(cursorX, cursorY);
        } else {
            allowViewportMousePick = !uiWantsMouse;
        }
        if (lmbPressed && !lmbWasPressed && allowViewportMousePick) {
            UpdateLookAtSelection(true);
        }
        lmbWasPressed = lmbPressed;

        checkGLError("Before swap buffers");

        if (optRenderLOG) NC::LOGGING::Log("[FRAME SUMMARY] Solid: ", solidDraws.size(), " AlphaTest: ", alphaTestDraws.size(),
                         " SimpleLayer: ", simpleLayerDraws.size(), " Environment: ", environmentDraws.size(),
                         " EnvAlpha: ", environmentAlphaDraws.size(), " Decal: ", decalDraws.size(), " Water: ", waterDraws.size(),
                         " Refraction: ", refractionDraws.size(), " PostAlpha: ", postAlphaUnlitDraws.size(),
                         " Particles: ", scene_.particleNodes.size());

        if (optRenderLOG) NC::LOGGING::Log("[TOTAL DRAW CALLS] ", frameDrawCalls_);
        lastFrameDrawCalls_ = frameDrawCalls_;

        profElapsed(); // reset timer before SwapBuffers
        window_->SwapBuffers();
        frameProfile_.swapBuffers = profElapsed();

        // ── Frame fence sync: mark this frame's GPU work for optional pacing ──
        if (frameFences_[frameFenceIdx_]) {
            glDeleteSync(frameFences_[frameFenceIdx_]);
            frameFences_[frameFenceIdx_] = nullptr;
        }
        frameFences_[frameFenceIdx_] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        frameFenceIdx_ = (frameFenceIdx_ + 1) % (kMaxFramesInFlight + 1);

        frameProfile_.frameTotal = std::chrono::duration<double, std::milli>(
            ProfileClock::now() - profFrameStart).count();

        // Per-frame budget governor — runs every frame, uses cached gpuTotal + fresh cpuWork
        UpdateBudgetGovernor();

        ++profileFrameCounter_;
        if (profileFrameCounter_ >= kProfileLogInterval) {
            profileFrameCounter_ = 0;

            // Double-buffered GPU query reads — read from the PREVIOUS interval's
            // buffer (1 - gpuQueryBufWrite_) which is definitely GPU-complete.
            // Toggle write buffer so next interval writes to the other side.
            if (gpuQueriesInit_) {
                if (gpuQueryBufsReady_) {
                    const int readBuf = 1 - gpuQueryBufWrite_;

                    for (int qi = 0; qi < kGpuQueryCount; qi++) {
                        GLuint64 ns = 0;
                        glGetQueryObjectui64v(gpuQueriesDB_[readBuf][qi], GL_QUERY_RESULT, &ns);
                        const double ms = static_cast<double>(ns) / 1.0e6;
                        switch (qi) {
                            case 0: frameProfile_.gpuShadow   = ms; break;
                            case 1: frameProfile_.gpuGeometry = ms; break;
                            case 2: frameProfile_.gpuDecal    = ms; break;
                            case 3: frameProfile_.gpuLighting = ms; break;
                            case 4: frameProfile_.gpuForward  = ms; break;
                        }
                    }

                    for (int phase = 0; phase < kGpuGeomPhaseCount; ++phase) {
                        const int beginIdx = phase * 2;
                        const int endIdx   = beginIdx + 1;
                        GLuint64 tsBegin = 0, tsEnd = 0;
                        glGetQueryObjectui64v(gpuGeomTsDB_[readBuf][beginIdx], GL_QUERY_RESULT, &tsBegin);
                        glGetQueryObjectui64v(gpuGeomTsDB_[readBuf][endIdx],   GL_QUERY_RESULT, &tsEnd);
                        const double phaseMs = (tsEnd >= tsBegin)
                            ? static_cast<double>(tsEnd - tsBegin) / 1.0e6 : 0.0;
                        switch (phase) {
                            case 0: frameProfile_.gpuGeomSolid = phaseMs; break;
                            case 1: frameProfile_.gpuGeomAlpha = phaseMs; break;
                            case 2: frameProfile_.gpuGeomSL    = phaseMs; break;
                            case 3: frameProfile_.gpuGeomEnv   = phaseMs; break;
                        }
                    }

                    double* fwdTargets[5] = {
                        &frameProfile_.gpuEnvAlpha, &frameProfile_.gpuRefraction,
                        &frameProfile_.gpuWater, &frameProfile_.gpuPostAlpha, &frameProfile_.gpuParticles
                    };
                    for (int phase = 0; phase < kGpuFwdPhaseCount; ++phase) {
                        const int beginIdx = phase * 2;
                        const int endIdx   = beginIdx + 1;
                        GLuint64 tsBegin = 0, tsEnd = 0;
                        glGetQueryObjectui64v(gpuFwdTsDB_[readBuf][beginIdx], GL_QUERY_RESULT, &tsBegin);
                        glGetQueryObjectui64v(gpuFwdTsDB_[readBuf][endIdx],   GL_QUERY_RESULT, &tsEnd);
                        *fwdTargets[phase] = (tsEnd >= tsBegin)
                            ? static_cast<double>(tsEnd - tsBegin) / 1.0e6 : 0.0;
                    }
                }
                // Swap write buffer; mark both slots valid after the first swap
                gpuQueryBufWrite_  = 1 - gpuQueryBufWrite_;
                gpuQueryBufsReady_ = true;
            }

            // Compute derived fields
            frameProfile_.fps     = frameProfile_.frameTotal > 0.0 ? 1000.0 / frameProfile_.frameTotal : 0.0;
            frameProfile_.cpuWork = frameProfile_.frameTotal - frameProfile_.swapBuffers;
            frameProfile_.gpuTotal = frameProfile_.gpuShadow + frameProfile_.gpuGeometry
                + frameProfile_.gpuDecal + frameProfile_.gpuLighting + frameProfile_.gpuForward;

            // Push into rolling stats for percentile logging
            rsFrame_.Push(static_cast<float>(frameProfile_.frameTotal));
            rsCpu_.Push(static_cast<float>(frameProfile_.cpuWork));
            rsSwap_.Push(static_cast<float>(frameProfile_.swapBuffers));


            char profBuf[768];
            std::snprintf(profBuf, sizeof(profBuf),
                "[PROFILE] fps=%.1f frame=%.2fms cpu=%.2f swap=%.2f fence=%.2f "
                "input=%.3f shdrRld=%.3f tick=%.2f prep=%.2f rebuild=%.2f ssbo=%.3f "
                "anim=%.2f prePass=%.3f "
                "shadow=%.2f geom=%.2f decal=%.2f light=%.2f fwd=%.2f blit=%.2f imgui=%.2f draws=%d "
                "mode=%s dirtySkip=%d qTier=%d "
                "frame_p50=%.2f p95=%.2f p99=%.2f cpu_p50=%.2f p95=%.2f swap_p50=%.2f p95=%.2f",
                frameProfile_.fps, frameProfile_.frameTotal,
                frameProfile_.cpuWork, frameProfile_.swapBuffers, frameProfile_.fenceWait,
                frameProfile_.inputPoll, frameProfile_.shaderReload,
                frameProfile_.sceneTick, frameProfile_.prepareDraws, frameProfile_.rebuild,
                frameProfile_.materialSSBO, frameProfile_.animationTick, frameProfile_.prePassSetup,
                frameProfile_.shadowPass, frameProfile_.geometryPass,
                frameProfile_.decalPass, frameProfile_.lightingPass,
                frameProfile_.forwardPass, frameProfile_.blitCompose, frameProfile_.imguiRender,
                lastFrameDrawCalls_,
                (renderMode_ == RenderMode::Work) ? "Work" : "Play",
                frameProfile_.dirtyFrameSkipped,
                frameProfile_.qualityTier,
                rsFrame_.Percentile(50), rsFrame_.Percentile(95), rsFrame_.Percentile(99),
                rsCpu_.Percentile(50),   rsCpu_.Percentile(95),
                rsSwap_.Percentile(50),  rsSwap_.Percentile(95));
            NC::LOGGING::Log(profBuf);

            char shadowBuf[256];
            std::snprintf(shadowBuf, sizeof(shadowBuf),
                "[PROFILE_SHADOW] cBuild=%.2f grp=%.2f up=%.2f drw=%.2f sl=%.2f sC=%s",
                frameProfile_.shadowCasterBuild, frameProfile_.shadowGroup,
                frameProfile_.shadowUpload, frameProfile_.shadowDraw,
                frameProfile_.shadowSL,
                shadowCasterCacheHit_ ? "HIT" : "MISS");
            NC::LOGGING::Log(shadowBuf);

            char geomBuf[384];
            std::snprintf(geomBuf, sizeof(geomBuf),
                "[PROFILE_GEOM] setup=%.2f sCull=%.2f sFlush=%.2f aCull=%.2f aFlush=%.2f sl=%.2f env=%.2f "
                "sBatch=%d aBatch=%d eBatch=%d dBatch=%d visC=%s",
                frameProfile_.geomSetup,
                frameProfile_.geomSolidCull, frameProfile_.geomSolidFlush,
                frameProfile_.geomAlphaCull, frameProfile_.geomAlphaFlush,
                frameProfile_.geomSL, frameProfile_.geomEnv,
                frameProfile_.solidBatchCount, frameProfile_.alphaBatchCount,
                frameProfile_.envBatchCount, frameProfile_.decalBatchCount,
                visResolveSkipped_ ? "HIT" : "MISS");
            NC::LOGGING::Log(geomBuf);

            char slBuf[384];
            std::snprintf(slBuf, sizeof(slBuf),
                "[PROFILE_SL] vis=%.2f sort=%.2f grp=%.2f upload=%.2f render=%.2f nGrp=%d nVis=%d/%d cache=%s",
                frameProfile_.geomSLVis, frameProfile_.geomSLSort,
                frameProfile_.geomSLGroup, frameProfile_.geomSLUpload,
                frameProfile_.geomSLRender,
                frameProfile_.geomSLGroupCount, frameProfile_.geomSLVisibleCount,
                frameProfile_.slCount,
                frameProfile_.slCacheHit ? "HIT" : "MISS");
            NC::LOGGING::Log(slBuf);

            char gpuBuf[512];
            std::snprintf(gpuBuf, sizeof(gpuBuf),
                "[PROFILE_GPU] total=%.2f shadow=%.2f geom=%.2f decal=%.2f light=%.2f fwd=%.2f fwdFlush=%d "
                "fwdEnvA=%.2f fwdRefr=%.2f fwdWater=%.2f fwdPostA=%.2f fwdPart=%.2f",
                frameProfile_.gpuTotal,
                frameProfile_.gpuShadow, frameProfile_.gpuGeometry,
                frameProfile_.gpuDecal, frameProfile_.gpuLighting,
                frameProfile_.gpuForward, frameProfile_.fwdFlushCount,
                frameProfile_.gpuEnvAlpha, frameProfile_.gpuRefraction,
                frameProfile_.gpuWater, frameProfile_.gpuPostAlpha, frameProfile_.gpuParticles);
            NC::LOGGING::Log(gpuBuf);

            char gpuGeomBuf[320];
            std::snprintf(gpuGeomBuf, sizeof(gpuGeomBuf),
                "[PROFILE_GPU_GEOM] solid=%.2f alpha=%.2f sl=%.2f env=%.2f split=%.2f total=%.2f",
                frameProfile_.gpuGeomSolid, frameProfile_.gpuGeomAlpha,
                frameProfile_.gpuGeomSL, frameProfile_.gpuGeomEnv,
                frameProfile_.gpuGeomSolid + frameProfile_.gpuGeomAlpha +
                frameProfile_.gpuGeomSL + frameProfile_.gpuGeomEnv,
                frameProfile_.gpuGeometry);
            NC::LOGGING::Log(gpuGeomBuf);

            char drawsBuf[512];
            std::snprintf(drawsBuf, sizeof(drawsBuf),
                "[PROFILE_DRAWS] solid=%d alpha=%d sl=%d env=%d envA=%d decal=%d "
                "water=%d refr=%d postA=%d part=%d anim=%d totalDC=%d",
                frameProfile_.solidCount, frameProfile_.alphaCount,
                frameProfile_.slCount, frameProfile_.envCount, frameProfile_.envAlphaCount,
                frameProfile_.decalCount, frameProfile_.waterCount,
                frameProfile_.refractionCount, frameProfile_.postAlphaCount,
                frameProfile_.particleCount, frameProfile_.animatedCount,
                lastFrameDrawCalls_);
            NC::LOGGING::Log(drawsBuf);

            char visBuf[384];
            std::snprintf(visBuf, sizeof(visBuf),
                "[PROFILE_VIS] cellsTotal=%d frustum=%d pvs=%d high=%d low=%d objGate=%d "
                "alphaRed=%d decalRed=%d envARed=%d postARed=%d",
                frameProfile_.visCellsTotal, frameProfile_.visCellsFrustum,
                frameProfile_.visCellsAfterPVS,
                frameProfile_.visCellsHigh, frameProfile_.visCellsLow,
                frameProfile_.visObjectsAfterGate,
                frameProfile_.alphaReduced, frameProfile_.decalReduced,
                frameProfile_.envAlphaReduced, frameProfile_.postAlphaReduced);
            NC::LOGGING::Log(visBuf);

            char qualBuf[256];
            std::snprintf(qualBuf, sizeof(qualBuf),
                "[PROFILE_QUALITY] tier=%d lodBias=%.1f drawDistAdj=%.0f alphaAggr=%.2f "
                "overBudget=%d underBudget=%d pvs=%s",
                static_cast<int>(budgetGovernor_.tier),
                budgetGovernor_.lodBiasAdjust,
                budgetGovernor_.drawDistAdjust,
                budgetGovernor_.alphaAggressiveness,
                budgetGovernor_.overBudgetCount,
                budgetGovernor_.underBudgetCount,
                (pvsProvider_ && pvsProvider_->IsAvailable()) ? "YES" : "NO");
            NC::LOGGING::Log(qualBuf);
        }

        // CPU frame cap: policy-driven only.
        // targetFps <= 0 means strictly uncapped — env var is NOT consulted so
        // Work Mode cannot be accidentally capped by NDEVC_TARGET_FPS.
        {
            const int policyFps = activePolicy_.targetFps;
            if (policyFps > 0) {
                const double targetSec = 1.0 / static_cast<double>(policyFps);
                using Dur = std::chrono::duration<double>;
                const auto deadline = profFrameStart +
                    std::chrono::duration_cast<ProfileClock::duration>(Dur(targetSec - 0.001));
                std::this_thread::sleep_until(deadline);
            }
        }
}

// ---------------------------------------------------------------------------
// BuildPolicy — construct a FramePolicy for the given mode
// ---------------------------------------------------------------------------
FramePolicy BuildPolicy(RenderMode mode) {
    FramePolicy p{};
    switch (mode) {
    case RenderMode::Work:
        p.editorLayoutEnabled      = true;
        p.viewportOnlyUI           = false;
        p.forceFullSceneVisible    = false;
        p.visibilityCullingEnabled = true;
        p.allowPVS                 = true;
        p.dirtyRenderingEnabled    = true;
        p.limitEditorPanelRefresh  = false;
        p.targetFps                = 0;       // uncapped
        p.lodBias                  = 0.0f;
        p.maxDrawDistance           = 0.0f;
        p.alphaReductionNearDist   = 300.0f;
        p.alphaReductionFarDist    = 800.0f;
        p.gpuBudgetMs              = 10.0f;
        p.cpuBudgetMs              = 8.0f;
        p.highDetailRadiusChunks   = 2;
        p.budgetGovernorEnabled    = true;
        break;
    case RenderMode::Play:
        p.editorLayoutEnabled      = true;   // editor stays visible like Unity
        p.viewportOnlyUI           = false;
        p.forceFullSceneVisible    = false;
        p.visibilityCullingEnabled = true;
        p.allowPVS                 = true;
        p.dirtyRenderingEnabled    = false;  // continuous render in Play
        p.limitEditorPanelRefresh  = false;
        p.targetFps                = 0;       // uncapped in Play for maximum throughput
        p.lodBias                  = 0.0f;
        p.maxDrawDistance           = 0.0f;
        p.alphaReductionNearDist   = 200.0f;
        p.alphaReductionFarDist    = 600.0f;
        p.gpuBudgetMs              = 7.5f;
        p.cpuBudgetMs              = 6.0f;
        p.highDetailRadiusChunks   = 3;
        p.budgetGovernorEnabled    = true;
        break;
    }
    return p;
}

// ---------------------------------------------------------------------------
// ApplyPolicy — wire the policy into renderer state atomically
// ---------------------------------------------------------------------------
void DeferredRenderer::ApplyPolicy(const FramePolicy& policy) {
    activePolicy_ = policy;

    // Both Work and Play keep the full editor layout (Unity behaviour).
    // imguiViewportOnly_ is an init-time decision only; never change it at runtime
    // when docking was initialized — stopping docked-window submissions corrupts layout.
    if (imguiDockingAvailable_) {
        editorModeEnabled_ = true;  // always keep editor shell so docking stays intact
    } else {
        imguiViewportOnly_ = policy.viewportOnlyUI;
        editorModeEnabled_ = policy.editorLayoutEnabled;
    }
    enableVisibilityGrid_ = policy.visibilityCullingEnabled;
    visibleRange_         = policy.maxDrawDistance;

    dirtyFlag_ = true;
}

// ---------------------------------------------------------------------------
// LogPolicySnapshot — concise log of active mode + policy
// ---------------------------------------------------------------------------
void DeferredRenderer::LogPolicySnapshot(const char* reason) const {
    const char* modeName = (renderMode_ == RenderMode::Work) ? "Work" : "Play";
    const FramePolicy& p = activePolicy_;
    char buf[768];
    std::snprintf(buf, sizeof(buf),
        "[MODE] %s mode=%s editorLayout=%d viewportOnly=%d visCull=%d "
        "dirtyRendering=%d targetFps=%d(%s) lodBias=%.1f maxDist=%.0f "
        "alphaNear=%.0f alphaFar=%.0f gpuBudget=%.1f cpuBudget=%.1f "
        "hiDetailChunks=%d governor=%d qualityTier=%d",
        reason ? reason : "snapshot",
        modeName,
        p.editorLayoutEnabled ? 1 : 0,
        p.viewportOnlyUI ? 1 : 0,
        p.visibilityCullingEnabled ? 1 : 0,
        p.dirtyRenderingEnabled ? 1 : 0,
        p.targetFps,
        (p.targetFps <= 0) ? "uncapped" : "capped",
        p.lodBias + budgetGovernor_.lodBiasAdjust,
        p.maxDrawDistance,
        p.alphaReductionNearDist,
        p.alphaReductionFarDist,
        p.gpuBudgetMs,
        p.cpuBudgetMs,
        p.highDetailRadiusChunks,
        p.budgetGovernorEnabled ? 1 : 0,
        static_cast<int>(budgetGovernor_.tier));
    NC::LOGGING::Log(buf);
}

// ---------------------------------------------------------------------------
// MarkDirty / ConsumeDirty — dirty-frame tracking
// ---------------------------------------------------------------------------
void DeferredRenderer::MarkDirty() {
    dirtyFlag_ = true;
}

bool DeferredRenderer::ConsumeDirty() {
    if (dirtyFlag_) {
        dirtyFlag_ = false;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// GetRenderMode / SetRenderMode — public API for mode switching
// ---------------------------------------------------------------------------
RenderMode DeferredRenderer::GetRenderMode() const {
    return renderMode_;
}

void DeferredRenderer::SetRenderMode(RenderMode mode) {
    if (mode == renderMode_) return;
    renderMode_ = mode;
    FramePolicy policy = BuildPolicy(mode);
    ApplyPolicy(policy);

    // Invalidate transient caches on mode switch
    visFrustumCacheValid_ = false;
    visGridRevealedAll_ = false;
    slGBufCacheValid_ = false;
    slViewProjCacheValid_ = false;
    shadowGroupCacheValid_ = false;
    shadowCastersDirty_ = true;
    solidBatchSystem_.InvalidateCullCache();
    alphaTestBatchSystem_.InvalidateCullCache();

    // Phase 2: reset budget governor for the new mode
    budgetGovernor_.Reset();
    lastAppliedTier_ = QualityTier::Full;

    LogPolicySnapshot("SWITCH");
}

// ---------------------------------------------------------------------------
// BudgetGovernor::Update — adaptive quality control based on frame timings
// ---------------------------------------------------------------------------
void BudgetGovernor::Update(double gpuMs, double cpuMs, float gpuBudget, float cpuBudget) {
    const bool overBudget = (gpuMs > gpuBudget) || (cpuMs > cpuBudget);
    const bool underBudget = (gpuMs < gpuBudget * 0.7) && (cpuMs < cpuBudget * 0.7);

    if (overBudget) {
        ++overBudgetCount;
        underBudgetCount = 0;
    } else if (underBudget) {
        ++underBudgetCount;
        overBudgetCount = 0;
    } else {
        overBudgetCount = 0;
        underBudgetCount = 0;
    }

    if (overBudgetCount >= kHysteresisFrames) {
        overBudgetCount = 0;
        alphaAggressiveness = std::min(1.0f, alphaAggressiveness + kAlphaStep);
        lodBiasAdjust = std::min(3.0f, lodBiasAdjust + kLodBiasStep);
        drawDistAdjust = std::min(800.0f, drawDistAdjust + kDrawDistStep);
        if (alphaAggressiveness >= 0.5f && tier == QualityTier::Full) {
            tier = QualityTier::Reduced;
        } else if (alphaAggressiveness >= 0.9f && tier == QualityTier::Reduced) {
            tier = QualityTier::Minimum;
        }
    }

    if (underBudgetCount >= kHysteresisFrames) {
        underBudgetCount = 0;
        alphaAggressiveness = std::max(0.0f, alphaAggressiveness - kAlphaStep);
        lodBiasAdjust = std::max(0.0f, lodBiasAdjust - kLodBiasStep);
        drawDistAdjust = std::max(0.0f, drawDistAdjust - kDrawDistStep);
        if (alphaAggressiveness < 0.5f && tier == QualityTier::Reduced) {
            tier = QualityTier::Full;
        } else if (alphaAggressiveness < 0.9f && tier == QualityTier::Minimum) {
            tier = QualityTier::Reduced;
        }
    }
}

void BudgetGovernor::Reset() {
    tier = QualityTier::Full;
    lodBiasAdjust = 0.0f;
    drawDistAdjust = 0.0f;
    alphaAggressiveness = 0.0f;
    overBudgetCount = 0;
    underBudgetCount = 0;
}

// ---------------------------------------------------------------------------
// UpdateBudgetGovernor — call after frame profile is computed
// ---------------------------------------------------------------------------
void DeferredRenderer::UpdateBudgetGovernor() {
    if (!activePolicy_.budgetGovernorEnabled) return;

    const double gpuMs = frameProfile_.gpuTotal;  // last-known GPU time (updated every 1200 frames)
    const double cpuMs = frameProfile_.frameTotal - frameProfile_.swapBuffers;
    budgetGovernor_.Update(gpuMs, cpuMs,
                           activePolicy_.gpuBudgetMs,
                           activePolicy_.cpuBudgetMs);

    frameProfile_.qualityTier       = static_cast<int>(budgetGovernor_.tier);
    frameProfile_.governorLodBias   = budgetGovernor_.lodBiasAdjust;
    frameProfile_.governorAlphaAggr = budgetGovernor_.alphaAggressiveness;
    // Honest panel Hz: we always call RenderImGui every frame
    frameProfile_.panelUpdateHzEffective = (frameProfile_.frameTotal > 0.0)
        ? static_cast<float>(1000.0 / frameProfile_.frameTotal) : 0.0f;

    // Quality tier change is a dirty signal
    if (budgetGovernor_.tier != lastAppliedTier_) {
        lastAppliedTier_ = budgetGovernor_.tier;
        dirtyFlag_ = true;
        visFrustumCacheValid_ = false;
    }
}

// ---------------------------------------------------------------------------
// bindDrawTextures -- promoted from local lambda in initLOOP
// ---------------------------------------------------------------------------
void DeferredRenderer::bindDrawTextures(const DrawCmd& dc)
{
    for (int i = 0; i < 12; i++) {
        bindTexture(i, toTextureHandle(dc.tex[i]));
    }
}

// ---------------------------------------------------------------------------
// renderMeshDraw -- promoted from local lambda in initLOOP
// ---------------------------------------------------------------------------
void DeferredRenderer::renderMeshDraw(const DrawCmd& dc)
{
    if (dc.mesh && dc.group >= 0 && dc.group < (int)dc.mesh->groups.size()) {
        auto& grp = dc.mesh->groups[dc.group];
        // MegaBuffer indices are already rebased in MeshServer::buildMegaBuffer(),
        // so baseVertex must stay 0 here.
        glDrawElements(GL_TRIANGLES, grp.indexCount(), GL_UNSIGNED_INT,
            (void*)(size_t)((dc.megaIndexOffset + grp.firstIndex()) * sizeof(unsigned int)));
    }
}
