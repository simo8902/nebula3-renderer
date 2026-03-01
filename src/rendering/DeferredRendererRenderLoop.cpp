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
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <unordered_set>

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

bool PassesFrustumCulling(const DrawCmd& dc,
                          const Camera::Frustum& frustum,
                          bool disableFrustumCulling,
                          bool keepStaticVisible) {
    if (disableFrustumCulling) return true;
    if (keepStaticVisible && dc.isStatic) return true;

    const glm::vec3 localMin(dc.localBoxMin);
    const glm::vec3 localMax(dc.localBoxMax);
    const glm::vec3 localExtent = localMax - localMin;
    const bool hasBounds =
        std::isfinite(localMin.x) && std::isfinite(localMin.y) && std::isfinite(localMin.z) &&
        std::isfinite(localMax.x) && std::isfinite(localMax.y) && std::isfinite(localMax.z) &&
        (std::abs(localExtent.x) > 1e-5f || std::abs(localExtent.y) > 1e-5f || std::abs(localExtent.z) > 1e-5f);
    if (!hasBounds) return true;

    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const glm::vec3 worldCenter = glm::vec3(dc.worldMatrix * glm::vec4(localCenter, 1.0f));
    if (!std::isfinite(worldCenter.x) || !std::isfinite(worldCenter.y) || !std::isfinite(worldCenter.z)) return true;

    const float localRadius = glm::length(localExtent) * 0.5f;
    const float sx = glm::length(glm::vec3(dc.worldMatrix[0]));
    const float sy = glm::length(glm::vec3(dc.worldMatrix[1]));
    const float sz = glm::length(glm::vec3(dc.worldMatrix[2]));
    const float maxScale = std::max(sx, std::max(sy, sz));
    if (!std::isfinite(localRadius) || !std::isfinite(maxScale)) return true;
    const float radius = std::max(localRadius * maxScale, 0.001f);

    for (int i = 0; i < 6; ++i) {
        const float dist = glm::dot(frustum.planes[i].normal, worldCenter) + frustum.planes[i].d;
        if (dist < -radius * 1.2f) return false;
    }
    return true;
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
static constexpr UniformID U_GPOSITION = NDEVC::Graphics::IShader::MakeUniformID("gPosition");
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
static constexpr bool kDisableShadows = false;
static constexpr bool kDisableLighting = false;
static constexpr bool kDisableShadowPass = false;
static constexpr bool kDisableGeometryPass = false;
static constexpr bool kDisableLightingPass = false;
static constexpr bool kDisablePointLightPass = false;
static constexpr bool kDisableCompositionPass = false;
static constexpr bool kDisableDecalPass = false;
static constexpr bool kDisableForwardPass = false;
static constexpr bool kDisableComposePass = false;
static constexpr bool kDisableEnvironmentAlphaPass = false;
static constexpr bool kDisableWaterPass = false;
static constexpr bool kDisablePostAlphaUnlitPass = false;
static constexpr bool kDisableParticlePass = false;
static constexpr bool kDisableFog = false;
static constexpr bool kDisableViewDependentSpecular = false;
static constexpr bool kDisableViewDependentReflections = false;
static constexpr bool kForceEnvironmentThroughStandardGeometryPath = false;
static constexpr bool kLogGroundDecalReceiveSolidDiffuse = false;
static constexpr bool kParticleEmitterTransformsAreStatic = false;
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

        DrawBatchSystem::instance().init(solidDraws);

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
    // Keep view-space position at 32-bit float for soft-particle depth stability.
    geometryGraph->declareResource("gPositionVS", Format::RGBA32F);
    geometryGraph->declareResource("gNormalDepthPacked", Format::RGBA16F);
    geometryGraph->declareResource("gNormalDepthCompat", Format::RGBA16F);
    geometryGraph->declareResource("gNormalDepthEncoded", Format::RGBA16F);
    geometryGraph->declareResource("gAlbedoSpec", Format::RGBA8_UNORM);
    // World-space position is used directly for shadow lookups; keep full precision
    // to avoid quantization shimmer on large world coordinates.
    geometryGraph->declareResource("gPositionWS", Format::RGBA32F);
    geometryGraph->declareResource("gEmissive", Format::RGBA16F);
    geometryGraph->declareResource("gDepth", Format::D24_UNORM_S8_UINT, true);

    lightingGraph = std::make_unique<FrameGraph>(device_.get(), width, height);
    lightingGraph->declareResource("lightBuffer", Format::RGBA16F);
    lightingGraph->declareResource("sceneColor", Format::RGBA16F);

    particleGraph = std::make_unique<FrameGraph>(device_.get(), width, height);
    decalGraph = std::make_unique<FrameGraph>(device_.get(), width, height);

auto& shadowPass = shadowGraph->addPass("ShadowCascades");
shadowPass.depthTest = true;
shadowPass.depthWrite = true;
shadowPass.cullFace = true;
// Shadow pass clears per cascade inside renderCascadedShadows().
shadowPass.clearDepth = false;
shadowPass.externalFBO = true;
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
    geometryPass.execute = [this]() {
        if (kDisableGeometryPass) {
            if (optRenderLOG) std::cout << "[GEOMETRY] Disabled by kDisableGeometryPass\n";
            return;
        }
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

    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);

    auto shader = shaderManager->GetShader("NDEVCdeferred");
    if (!shader) {
        std::cerr << "[GEOMETRY] ERROR: Shader not found\n";
        return;
    }
    const GLuint deferredProgram = shader->GetNativeHandle()
        ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
        : 0;
    if (optRenderLOG) std::cout << "[GEOMETRY] Shader ID: " << deferredProgram << "\n";

    static GLuint sLastDeferredProgram = 0;
    if (sLastDeferredProgram != deferredProgram) {
        sLastDeferredProgram = deferredProgram;
        deferredShaderCompatibilityLogged_ = false;
    }

    deferredShaderHasNormalMatrixUniform_ = false;
    deferredShaderUsesPackedGBuffer_ = false;
    GLint normalMatrixLoc = -1;
    GLint depthScaleLoc = -1;
    if (deferredProgram != 0) {
        normalMatrixLoc = glGetUniformLocation(deferredProgram, "normalMatrix");
        depthScaleLoc = glGetUniformLocation(deferredProgram, "depthScale");
        deferredShaderHasNormalMatrixUniform_ = normalMatrixLoc >= 0;

        const GLint packedNormalOutLoc = glGetFragDataLocation(deferredProgram, "gNormalDepth");
        const GLint standardNormalOutLoc = glGetFragDataLocation(deferredProgram, "gNormalDepthPacked");
        const GLint positionVSOutLoc = glGetFragDataLocation(deferredProgram, "gPositionVS");
        deferredShaderUsesPackedGBuffer_ =
            (packedNormalOutLoc == 0 && standardNormalOutLoc < 0 && positionVSOutLoc < 0);

        if (!deferredShaderCompatibilityLogged_) {
            if (deferredShaderHasNormalMatrixUniform_) {
                if (optRenderLOG || kLogShaderCompat) {
                    if (kForceNonInstancedNormalMatrixPath) {
                        std::cout << "[GEOMETRY] NDEVCdeferred uses 'normalMatrix'; switching deferred draws to non-instanced per-draw normalMatrix uploads.\n";
                    } else {
                        std::cout << "[GEOMETRY] NDEVCdeferred uses 'normalMatrix'; keeping instanced deferred draws with view-space fallback normalMatrix.\n";
                    }
                }
            }
            if (deferredShaderUsesPackedGBuffer_) {
                if (optRenderLOG || kLogShaderCompat) {
                    std::cout << "[GEOMETRY] NDEVCdeferred writes packed legacy G-buffer outputs; enabling packed-to-standard G-buffer compatibility pass.\n";
                }
            }
            deferredShaderCompatibilityLogged_ = true;
        }
    }
    deferredPackedCompatReady_ = !deferredShaderUsesPackedGBuffer_;

    constexpr GLsizei kGeometryDrawBufferCount = 6;
    constexpr GLenum kStandardGBufferDrawBuffers[kGeometryDrawBufferCount] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5
    };
    constexpr GLenum kPackedDeferredDrawBuffers[kGeometryDrawBufferCount] = {
        GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_NONE, GL_NONE
    };
    auto setGeometryDrawBufferLayout = [&](bool packedDeferredLayout) {
        static GLint cachedMaxDrawBuffers = -1;
        if (cachedMaxDrawBuffers < 0) {
            glGetIntegerv(GL_MAX_DRAW_BUFFERS, &cachedMaxDrawBuffers);
            if (cachedMaxDrawBuffers <= 0) {
                cachedMaxDrawBuffers = 1;
            }
        }

        GLsizei drawBufferCount = static_cast<GLsizei>(cachedMaxDrawBuffers);
        if (drawBufferCount > kGeometryDrawBufferCount) {
            drawBufferCount = kGeometryDrawBufferCount;
        }

        static bool warnedDrawBufferClamp = false;
        if (drawBufferCount < kGeometryDrawBufferCount && !warnedDrawBufferClamp) {
            std::cerr << "[GEOMETRY] GL_MAX_DRAW_BUFFERS=" << cachedMaxDrawBuffers
                      << "; clamping geometry MRT draw buffers to " << drawBufferCount << ".\n";
            warnedDrawBufferClamp = true;
        }

        glDrawBuffers(drawBufferCount, packedDeferredLayout ? kPackedDeferredDrawBuffers : kStandardGBufferDrawBuffers);
    };

    shader->PrecacheUniform(U_PROJECTION, "projection");
    shader->PrecacheUniform(U_VIEW, "view");
    shader->PrecacheUniform(U_MODEL, "model");
    shader->PrecacheUniform(U_TEXTURE_TRANSFORM0, "textureTransform0");
    shader->PrecacheUniform(U_MAT_EMISSIVE_INTENSITY, "MatEmissiveIntensity");
    shader->PrecacheUniform(U_MAT_SPECULAR_INTENSITY, "MatSpecularIntensity");
    shader->PrecacheUniform(U_MAT_SPECULAR_POWER, "MatSpecularPower");
    shader->PrecacheUniform(U_USE_SKINNING, "UseSkinning");
    shader->PrecacheUniform(U_USE_INSTANCING, "UseInstancing");
    shader->PrecacheUniform(U_ALPHA_TEST, "alphaTest");
    shader->PrecacheUniform(U_ALPHA_CUTOFF, "alphaCutoff");
    shader->PrecacheUniform(U_DIFF_MAP0, "DiffMap0");
    shader->PrecacheUniform(U_SPEC_MAP0, "SpecMap0");
    shader->PrecacheUniform(U_BUMP_MAP0, "BumpMap0");
    shader->PrecacheUniform(U_EMSV_MAP0, "EmsvMap0");
    shader->PrecacheUniform(U_TWO_SIDED, "twoSided");
    shader->PrecacheUniform(U_IS_FLAT_NORMAL, "isFlatNormal");
    shader->PrecacheUniform(U_RECEIVES_DECALS, "ReceivesDecals");

    shader->Use();
    shader->SetMat4(U_PROJECTION, camera_.getProjectionMatrix());
    shader->SetMat4(U_VIEW, camera_.getViewMatrix());
    shader->SetMat4(U_TEXTURE_TRANSFORM0, glm::mat4(1.0f));
    shader->SetFloat(U_MAT_EMISSIVE_INTENSITY, 0.0f);
    shader->SetFloat(U_MAT_SPECULAR_INTENSITY, 0.0f);
    shader->SetFloat(U_MAT_SPECULAR_POWER, 32.0f);
    shader->SetInt(U_USE_SKINNING, 0);
    shader->SetInt(U_USE_INSTANCING, 1);
    shader->SetInt(U_ALPHA_TEST, 0);
    shader->SetFloat(U_ALPHA_CUTOFF, 0.5f);
    shader->SetInt(U_DIFF_MAP0, 0);
    shader->SetInt(U_SPEC_MAP0, 1);
    shader->SetInt(U_BUMP_MAP0, 2);
    shader->SetInt(U_EMSV_MAP0, 3);
    shader->SetInt(U_TWO_SIDED, 0);
    shader->SetInt(U_IS_FLAT_NORMAL, 0);
    if (depthScaleLoc >= 0) {
        glUniform1f(depthScaleLoc, 1.0f);
    }
    if (normalMatrixLoc >= 0) {
        const glm::mat3 viewOnlyNormalMatrix = glm::transpose(glm::inverse(glm::mat3(camera_.getViewMatrix())));
        glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, &viewOnlyNormalMatrix[0][0]);
    }

    setGeometryDrawBufferLayout(deferredShaderUsesPackedGBuffer_);

    if (device_) {
        NDEVC::Graphics::RenderStateDesc geomState;
        geomState.stencil.stencilEnable = true;
        geomState.stencil.stencilFunc = NDEVC::Graphics::CompareFunc::Always;
        geomState.stencil.ref = 1;
        geomState.stencil.readMask = 0xFF;
        geomState.stencil.writeMask = 0xFF;
        geomState.stencil.stencilFailOp = NDEVC::Graphics::StencilOp::Keep;
        geomState.stencil.depthFailOp = NDEVC::Graphics::StencilOp::Keep;
        geomState.stencil.depthPassOp = NDEVC::Graphics::StencilOp::Replace;
        auto state = device_->CreateRenderState(geomState);
        if (state) device_->ApplyRenderState(state.get());
    } else {
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    glm::mat4 viewProj = camera_.getProjectionMatrix() * camera_.getViewMatrix();
    Camera::Frustum frustum = camera_.extractFrustum(viewProj);
    const bool disableFrustumCulling = FrustumCullingDisabled();
    const bool disableFaceCulling = FaceCullingDisabled();
    UpdateVisibilityThisFrame(frustum);
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

    if (disableFaceCulling) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }

    if (useNonInstancedNormalMatrixPath) {
        shader->SetInt(U_USE_INSTANCING, 0);
        MegaBuffer::instance().bind();
        int renderedSolid = 0;
        for (const auto& dc : solidDraws) {
            if (drawDeferredCmd(dc, false)) ++renderedSolid;
        }
        frameDrawCalls_ += renderedSolid;
        if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Solid draws (non-instanced normalMatrix path): ", renderedSolid);
    } else {
        DrawBatchSystem::instance().cull(solidDraws, frustum);
        if (optRenderLOG) std::cout << "[GEOMETRY] Solid draws: " << solidDraws.size() << "\n";

        this->clearError("Geometry::PreSolidFlush");
        DrawBatchSystem::instance().flush(samplerRepeat_abstracted.get());

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
        std::unordered_map<std::string, float> animParams = DeferredRendererAnimation::SampleShaderVarAnimations(dc.sourceNode, dcAnimTime);
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

    if (!alphaTestDraws.empty()) {
        if (useNonInstancedNormalMatrixPath) {
            shader->SetInt(U_ALPHA_TEST, 1);
            shader->SetInt(U_USE_INSTANCING, 0);
            MegaBuffer::instance().bind();
            int renderedAlpha = 0;
            for (const auto& dc : alphaTestDraws) {
                if (drawDeferredCmd(dc, true)) ++renderedAlpha;
            }
            frameDrawCalls_ += renderedAlpha;
            if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Alpha test draws (non-instanced normalMatrix path): ", renderedAlpha);
            shader->SetInt(U_ALPHA_TEST, 0);
        } else {
            shader->SetInt(U_ALPHA_TEST, 1);
            shader->SetInt(U_USE_INSTANCING, 1);

            DrawBatchSystem::instance().reset();
            frustum = camera_.extractFrustum(camera_.getProjectionMatrix() * camera_.getViewMatrix());
            DrawBatchSystem::instance().cullGeneric(alphaTestDraws, frustum, 1, 5);

            MegaBuffer::instance().bind();
            DrawBatchSystem::instance().flush(samplerRepeat_abstracted.get(), 5);

            int alphaTestDrawCount = alphaTestDraws.size();
            frameDrawCalls_ += alphaTestDrawCount;
            if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Alpha test draws (batched): ", alphaTestDrawCount);
        }
    }

    // Non-deferred shaders use the standard G-buffer location mapping.
    setGeometryDrawBufferLayout(false);

    if (!simpleLayerDraws.empty()) {
        auto slGBufferShader = shaderManager->GetShader("simplelayer_gbuffer");
        auto slGBufferClipShader = shaderManager->GetShader("simplelayer_gbuffer_clip");
        if (!slGBufferShader || !slGBufferClipShader) {
            std::cerr << "[GEOMETRY] simplelayer gbuffer shader variants not found\n";
        } else {
            // Write gPositionVS (attachment 0) and gNormalDepthPacked (attachment 1)
            constexpr GLenum kSimpleLayerGBufferDrawBuffers[2] = {
                GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1
            };
            glDrawBuffers(2, kSimpleLayerGBufferDrawBuffers);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);

            // Precompute inverse view rotation for view→world normal conversion
            const glm::mat3 slInvViewRot = glm::mat3(glm::inverse(camera_.getViewMatrix()));

            int renderedSimpleLayerGBuffer = 0;
            MegaBuffer::instance().bind();
            for (const auto& dc : simpleLayerDraws) {
                if (!dc.mesh || dc.disabled) continue;
                if (!isDrawVisible(dc, frustum, true)) continue;

                if (dc.cullMode <= 0) {
                    glDisable(GL_CULL_FACE);
                } else {
                    glEnable(GL_CULL_FACE);
                    glCullFace(dc.cullMode == 1 ? GL_FRONT : GL_BACK);
                }

                auto shader = dc.alphaTest ? slGBufferClipShader : slGBufferShader;
                shader->Use();

                const glm::mat4 modelView = camera_.getViewMatrix() * dc.worldMatrix;
                const glm::mat4 mvp = camera_.getProjectionMatrix() * modelView;
                const glm::mat4 imv = glm::inverse(modelView);
                shader->SetMat4("mvp", glm::transpose(mvp));
                shader->SetMat4("modelView", glm::transpose(modelView));
                shader->SetMat4("imv", glm::transpose(imv));
                {
                    const GLuint prog = shader->GetNativeHandle()
                        ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle()) : 0;
                    if (prog) {
                        GLint loc = glGetUniformLocation(prog, "invViewRot");
                        if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_FALSE, &slInvViewRot[0][0]);
                    }
                }

                auto layerTilingIt = dc.shaderParamsFloat.find("layerTiling");
                const float layerTiling = (layerTilingIt != dc.shaderParamsFloat.end())
                    ? layerTilingIt->second
                    : 1.0f;
                shader->SetFloat("layerTiling", layerTiling);

                if (dc.alphaTest) {
                    const float alphaRef = glm::clamp(dc.alphaCutoff * 256.0f, 0.0f, 255.0f);
                    shader->SetFloat("AlphaClipRef", alphaRef);
                    shader->SetInt("diffMapSampler", 0);
                    bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
                    bindSampler(0, gSamplerRepeat);
                }

                if (dc.alphaTest) {
                    shader->SetInt("bumpMapSampler", 1);
                    shader->SetInt("bumpMap1Sampler", 2);
                    shader->SetInt("maskSampler", 3);
                    bindTexture(1, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
                    bindTexture(2, dc.tex[6] ? toTextureHandle(dc.tex[6]) : (dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex));
                    bindTexture(3, dc.tex[7] ? toTextureHandle(dc.tex[7]) : blackTex);
                    bindSampler(1, gSamplerRepeat);
                    bindSampler(2, gSamplerRepeat);
                    bindSampler(3, gSamplerRepeat);
                } else {
                    shader->SetInt("bumpMapSampler", 0);
                    shader->SetInt("bumpMap1Sampler", 1);
                    shader->SetInt("maskSampler", 2);
                    bindTexture(0, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
                    bindTexture(1, dc.tex[6] ? toTextureHandle(dc.tex[6]) : (dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex));
                    bindTexture(2, dc.tex[7] ? toTextureHandle(dc.tex[7]) : blackTex);
                    bindSampler(0, gSamplerRepeat);
                    bindSampler(1, gSamplerRepeat);
                    bindSampler(2, gSamplerRepeat);
                }

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
                renderedSimpleLayerGBuffer++;
            }

            setGeometryDrawBufferLayout(false);
            for (int i = 0; i <= 3; ++i) {
                if (device_) {
                    device_->BindSampler(nullptr, i);
                } else {
                    glBindSampler(i, 0);
                }
            }
            glDisable(GL_CULL_FACE);

            if (optRenderLOG) {
                NC::LOGGING::Log("[GEOMETRY] SimpleLayer gbuffer draws: ", renderedSimpleLayerGBuffer);
            }
        }
    }

        if (!environmentDraws.empty()) {
            if (device_) {
                NDEVC::Graphics::RenderStateDesc envState;
                envState.depth.depthTest = true;
                envState.depth.depthWrite = true;
                envState.depth.depthFunc = NDEVC::Graphics::CompareFunc::Less;
                envState.blend.blendEnable = false;
                envState.stencil.stencilEnable = true;
                envState.stencil.stencilFunc = NDEVC::Graphics::CompareFunc::Always;
                envState.stencil.ref = 1;
                envState.stencil.readMask = 0xFF;
                envState.stencil.writeMask = 0xFF;
                envState.stencil.stencilFailOp = NDEVC::Graphics::StencilOp::Keep;
                envState.stencil.depthFailOp = NDEVC::Graphics::StencilOp::Keep;
                envState.stencil.depthPassOp = NDEVC::Graphics::StencilOp::Replace;
                envState.rasterizer.cullMode = disableFaceCulling ? NDEVC::Graphics::CullMode::None
                                                                  : NDEVC::Graphics::CullMode::Back;
                device_->ApplyRenderState(device_->CreateRenderState(envState).get());
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
                auto stdShader = shaderManager->GetShader("NDEVCdeferred");
                if (!stdShader) {
                    std::cerr << "[GEOMETRY] Shader NDEVCdeferred not found for environment fallback\n";
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
                        Camera::Frustum envFrustum = camera_.extractFrustum(camera_.getProjectionMatrix() * camera_.getViewMatrix());
                        MegaBuffer::instance().bind();
                        int renderedEnv = 0;
                        for (const auto& dc : environmentDraws) {
                            if (!dc.mesh || dc.disabled) continue;
                            if (!isDrawVisible(dc, envFrustum, true)) continue;

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
                        DrawBatchSystem::instance().reset();
                        Camera::Frustum frustum = camera_.extractFrustum(camera_.getProjectionMatrix() * camera_.getViewMatrix());
                        DrawBatchSystem::instance().cullGeneric(environmentDraws, frustum, 3, 4);

                        MegaBuffer::instance().bind();
                        DrawBatchSystem::instance().flush(samplerRepeat_abstracted.get(), 4);

                        frameDrawCalls_ += static_cast<int>(environmentDraws.size());
                        if (optRenderLOG) NC::LOGGING::Log("[GEOMETRY] Environment draws via standard path: ", environmentDraws.size());
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
                    envShader->SetVec3("eyePos", camera_.getPosition());
                    envShader->SetInt("DiffMap0", 0);
                    envShader->SetInt("SpecMap0", 1);
                    envShader->SetInt("BumpMap0", 2);
                    envShader->SetInt("EmsvMap0", 3);
                    envShader->SetInt("CubeMap0", 4);

                    Camera::Frustum frustum = camera_.extractFrustum(camera_.getProjectionMatrix() * camera_.getViewMatrix());
                    int renderedEnv = 0;
                    for (const auto& dc : environmentDraws) {
                        if (!dc.mesh) continue;
                        if (dc.disabled) continue;
                        if (!isDrawVisible(dc, frustum, true)) continue;

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
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, packedNormalDepth);
        glUniform1i(glGetUniformLocation(compatProgram, "gPackedNormalDepth"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gPosWS);
        glUniform1i(glGetUniformLocation(compatProgram, "gPositionWS"), 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, baseNormalDepth);
        glUniform1i(glGetUniformLocation(compatProgram, "gBaseNormalDepth"), 2);

        const glm::mat4 view = camera_.getViewMatrix();
        const glm::mat4 invView = glm::inverse(view);
        const glm::mat4 invProjection = glm::inverse(camera_.getProjectionMatrix());
        const glm::mat3 invViewRot = glm::mat3(glm::inverse(view));
        glUniformMatrix4fv(glGetUniformLocation(compatProgram, "view"), 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(compatProgram, "invView"), 1, GL_FALSE, &invView[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(compatProgram, "invProjection"), 1, GL_FALSE, &invProjection[0][0]);
        glUniformMatrix3fv(glGetUniformLocation(compatProgram, "invViewRot"), 1, GL_FALSE, &invViewRot[0][0]);

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

        if (gDepthDecalFBO == 0) {
            glGenFramebuffers(1, gDepthDecalFBO.put());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, gDepthDecalFBO);

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


        auto decalShader = shaderManager->GetShader("NDEVCdecal_mesh");


        if (!decalShader) {
             std::cerr << "    [ERROR] Decal shader 'NDEVCdecal_mesh' not found! Skipping decals.\n";
        } else {
            decalShader->Use();

            glm::mat4 viewMatrix = camera_.getViewMatrix();
            glm::mat4 projMatrix = camera_.getProjectionMatrix();

            decalShader->SetMat4("projection", projMatrix);
            decalShader->SetMat4("view", viewMatrix);
            decalShader->SetVec2("screenSize", glm::vec2((float)width, (float)height));
            decalShader->SetInt("gPositionWS", 0);
            decalShader->SetInt("gNormalDepthPacked", 1);
            decalShader->SetInt("DiffMap0", 2);
            decalShader->SetInt("EmsvMap0", 3);
            decalShader->SetFloat("DecalScale", 1.0f);
            decalShader->SetInt("DecalDiffuseMode", 0);

            this->bindTexture(0, gPosWS);
            glBindSampler(0, 0);
            this->bindTexture(1, gNormalDepth);
            glBindSampler(1, 0);

            DrawBatchSystem::instance().cullDecals(decalDraws);
            MegaBuffer::instance().bind();
            DrawBatchSystem::instance().flushDecals(samplerRepeat_abstracted.get(), samplerClamp_abstracted.get());

            frameDrawCalls_ += static_cast<int>(decalDraws.size());
            if (optRenderLOG) NC::LOGGING::Log("[DECALS] Submitted: ", static_cast<int>(decalDraws.size()));

            glBindVertexArray(0);
            glBindSampler(0, 0);
            glBindSampler(1, 0);
            glBindSampler(2, 0);
            glBindSampler(3, 0);

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
    lightingPass.execute = [this]() {
        if (kDisableLightingPass) {
            if (optRenderLOG) std::cout << "[LIGHTING] Disabled by kDisableLightingPass\n";
            return;
        }
        if (optRenderLOG) std::cout << "[LIGHTING] Begin lighting pass\n";
        clearError("Lighting::PassStart");
        GLuint gPosVS = GetFrameGraphTexture(geometryGraph, "gPositionVS");
        GLuint gNormalDepth = deferredShaderUsesPackedGBuffer_
            ? GetFrameGraphTexture(geometryGraph, "gNormalDepthCompat")
            : GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
        GLuint gPosWS = GetFrameGraphTexture(geometryGraph, "gPositionWS");
        if (optRenderLOG) std::cout << "[LIGHTING] G-buffer textures: PosVS=" << gPosVS << " Normal=" << gNormalDepth << " PosWS=" << gPosWS << "\n";
        if (gPosVS == 0 || gNormalDepth == 0 || gPosWS == 0) {
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
        shader->PrecacheUniform(U_GPOSITION, "gPosition");
        shader->PrecacheUniform(U_GNORMAL_DEPTH_PACKED, "gNormalDepthPacked");
        shader->PrecacheUniform(U_GPOSITION_WS, "gPositionWS");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE0, "shadowMapCascade0");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE1, "shadowMapCascade1");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE2, "shadowMapCascade2");
        shader->PrecacheUniform(U_SHADOW_MAP_CASCADE3, "shadowMapCascade3");
        shader->PrecacheUniform(U_NUM_CASCADES, "numCascades");
        shader->Use();
        glm::vec3 camPos = camera_.getPosition();
        shader->SetVec3(U_CAMERA_POS, camPos);
        glm::vec3 lightDir = kLightDirToSun;
        shader->SetVec3(U_LIGHT_DIR_WS, lightDir);
        glm::vec3 lightColor = glm::vec3(1.08f, 0.90f, 0.68f);
        shader->SetVec3(U_LIGHT_COLOR, lightColor);
        glm::vec3 ambientColor = glm::vec3(0.18f, 0.21f, 0.24f);
        shader->SetVec3(U_AMBIENT_COLOR, ambientColor);
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
        bindTexture(0, gPosVS);
        bindTexture(1, gNormalDepth);
        bindTexture(2, gPosWS);
        shader->SetInt(U_GPOSITION, 0);
        shader->SetInt(U_GNORMAL_DEPTH_PACKED, 1);
        shader->SetInt(U_GPOSITION_WS, 2);
        shader->SetInt(U_NUM_CASCADES, shadowCascadeCount);
        shader->SetInt("DisableShadows", shadowsEnabled ? 0 : 1);
        shader->SetInt("DisableViewDependentSpecular", kDisableViewDependentSpecular ? 1 : 0);
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
compositionPass.execute = [this]() {
    if (optRenderLOG) std::cout << "[COMPOSITION] Begin composition pass\n";
    const bool forceCompatibilityUnlit = deferredShaderUsesPackedGBuffer_ && !deferredPackedCompatReady_;
    const bool disableLightingForPass = kDisableLighting || forceCompatibilityUnlit;
    GLuint gAlbedo = GetFrameGraphTexture(geometryGraph, "gAlbedoSpec");
    GLuint gPosVS = GetFrameGraphTexture(geometryGraph, "gPositionVS");
    GLuint gNormalDepth = deferredShaderUsesPackedGBuffer_
        ? GetFrameGraphTexture(geometryGraph, "gNormalDepthCompat")
        : GetFrameGraphTexture(geometryGraph, "gNormalDepthPacked");
    GLuint gEmissive = GetFrameGraphTexture(geometryGraph, "gEmissive");
    GLuint lightBuf = GetFrameGraphTexture(lightingGraph, "lightBuffer");
    GLuint sceneCol = GetFrameGraphTexture(lightingGraph, "sceneColor");
    if (optRenderLOG) std::cout << "[COMPOSITION] Writing to sceneColor=" << sceneCol << "\n";
    if (optRenderLOG) std::cout << "[COMPOSITION] Reading: Albedo=" << gAlbedo << " Light=" << lightBuf << " PosVS=" << gPosVS << "\n";

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

    if (forceCompatibilityUnlit || gNormalDepth == 0 || gPosVS == 0 || lightBuf == 0) {
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
    shader->Use();

    bindTexture(0, gAlbedo);
    shader->SetInt(U_GALBEDO_SPEC, 0);
    bindTexture(1, lightBuf);
    shader->SetInt(U_LIGHT_BUFFER_TEX, 1);
    bindTexture(2, gEmissive);
    shader->SetInt(U_GEMISSIVE_TEX, 2);
    bindTexture(3, gNormalDepth);
    shader->SetInt(U_GNORMAL_DEPTH_PACKED, 3);
    shader->SetInt("DisableLighting", disableLightingForPass ? 1 : 0);

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
    forwardPass.execute = [this]() {
        if (kDisableForwardPass) {
            if (optRenderLOG) std::cout << "[FORWARD] Disabled by kDisableForwardPass\n";
            return;
        }
        if (optRenderLOG) std::cout << "[FORWARD] Begin forward/particle pass\n";

        GLuint sceneColorTex = GetFrameGraphTexture(lightingGraph, "sceneColor");
        GLuint sceneDepthTex = GetFrameGraphTexture(geometryGraph, "gDepth");
        GLuint gPosVS = GetFrameGraphTexture(geometryGraph, "gPositionVS");
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
            }
        }

        glViewport(0, 0, width, height);
        MegaBuffer::instance().bind();
        glm::mat4 viewMatrix = camera_.getViewMatrix();
        glm::mat4 projMatrix = camera_.getProjectionMatrix();
        glm::mat4 viewProj = projMatrix * viewMatrix;
        glm::mat4 invView = glm::inverse(viewMatrix);
        glm::vec3 eyePos = camera_.getPosition();
        glm::vec2 invViewport(1.0f / width, 1.0f / height);
        Camera::Frustum frustum = camera_.extractFrustum(viewProj);
        const bool disableFrustumCulling = FrustumCullingDisabled();
        const bool disableFaceCulling = FaceCullingDisabled();
        const float particleTime = static_cast<float>(glfwGetTime());

        // ==================== SIMPLELAYER FORWARD PASS ====================
        if (!simpleLayerDraws.empty()) {
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
        if (!kDisableEnvironmentAlphaPass && !environmentAlphaDraws.empty()) {
            auto envShader = shaderManager->GetShader("environmentAlpha");
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
                envShader->SetInt("DiffMap0", 0);
                envShader->SetInt("SpecMap0", 1);
                envShader->SetInt("BumpMap0", 2);
                envShader->SetInt("EmsvMap0", 3);
                envShader->SetInt("EnvironmentMap", 4);
                envShader->SetVec3("eyePos", camera_.getPosition());
                envShader->SetInt("DisableViewDependentReflection", kDisableViewDependentReflections ? 1 : 0);

                Camera::Frustum envFrustum = camera_.extractFrustum(viewProj);
                MegaBuffer::instance().bind(); // bind once for all envAlpha draws
                int renderedEnvAlpha = 0;
                for (const auto& dc : environmentAlphaDraws) {
                    if (!dc.mesh) continue;
                    if (dc.disabled) continue;
                    if (!PassesFrustumCulling(dc, envFrustum, disableFrustumCulling, true)) continue;

                    envShader->SetMat4("model", dc.worldMatrix);
                    envShader->SetFloat("Reflectivity", dc.cachedIntensity0);
                    envShader->SetInt("twoSided", dc.cachedTwoSided);
                    envShader->SetInt("isFlatNormal", dc.cachedIsFlatNormal);
                    envShader->SetFloat("alphaBlendFactor", dc.cachedAlphaBlendFactor);

                    bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
                    bindTexture(1, dc.tex[1] ? toTextureHandle(dc.tex[1]) : whiteTex);
                    bindTexture(2, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
                    bindTexture(3, dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex);

                    glActiveTexture(GL_TEXTURE4);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, dc.tex[9] ? toTextureHandle(dc.tex[9]) : blackCubeTex);

                    bindSampler(0, gSamplerRepeat);
                    bindSampler(1, gSamplerRepeat);
                    bindSampler(2, gSamplerRepeat);
                    bindSampler(3, gSamplerRepeat);
                    bindSampler(4, 0);

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
                    renderedEnvAlpha++;
                }

                frameDrawCalls_ += renderedEnvAlpha;
                if (optRenderLOG) NC::LOGGING::Log("[ENV_ALPHA] Rendered: ", renderedEnvAlpha, "/", environmentAlphaDraws.size());

                for (int i = 0; i < 5; i++) bindSampler(i, 0);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

                glDisable(GL_BLEND);
                checkGLError("After environment alpha");
            }
        }

        // ==================== WATER PASS ====================
        if (!kDisableWaterPass && !waterDraws.empty()) {
            auto waterShader = shaderManager->GetShader("water");
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
                waterShader->SetInt("DiffMap0", 0);
                waterShader->SetInt("BumpMap0", 1);
                waterShader->SetInt("EmsvMap0", 2);
                waterShader->SetInt("CubeMap0", 3);
                waterShader->SetVec3("eyePos", camera_.getPosition());
                waterShader->SetInt("DisableViewDependentReflection", kDisableViewDependentReflections ? 1 : 0);

                for (int i = 0; i < 3; i++) {
                    if (device_ && samplerRepeat_abstracted) {
                        device_->BindSampler(samplerRepeat_abstracted.get(), i);
                    } else {
                        glBindSampler(i, gSamplerRepeat);
                    }
                }

                MegaBuffer::instance().bind(); // bind once for all water draws
                int renderedWater = 0;
                for (auto& dc : waterDraws) {
                    if (!dc.mesh) continue;
                    if (dc.disabled) continue;

                    const int cullMode = dc.cachedWaterCullMode;
                    if (disableFaceCulling || cullMode <= 0) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        glCullFace(cullMode == 1 ? GL_FRONT : GL_BACK);
                    }
                    if (!PassesFrustumCulling(dc, frustum, disableFrustumCulling, true)) continue;

                    glm::mat4 uvTransform = glm::mat4(1.0f);
                    if (dc.cachedHasVelocity) {
                        uvTransform = glm::translate(glm::mat4(1.0f),
                            glm::vec3(dc.cachedVelocity * particleTime, 0.0f));
                    }

                    float uvScale = dc.cachedScale;
                    if (uvScale <= 0.0f) uvScale = 1.0f;

                    waterShader->SetMat4("model", dc.worldMatrix);
                    waterShader->SetMat4("textureTransform0", uvTransform);
                    waterShader->SetVec2("uvScale", glm::vec2(uvScale, uvScale));
                    waterShader->SetFloat("Intensity0", dc.cachedIntensity0);
                    waterShader->SetFloat("MatEmissiveIntensity", dc.cachedMatEmissiveIntensity);
                    waterShader->SetFloat("MatSpecularIntensity", dc.cachedMatSpecularIntensity);
                    waterShader->SetFloat("BumpScale", dc.cachedBumpScale);

                    bindTexture(0, dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex);
                    bindTexture(1, dc.tex[2] ? toTextureHandle(dc.tex[2]) : normalTex);
                    bindTexture(2, dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex);
                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, dc.tex[9] ? toTextureHandle(dc.tex[9]) : blackCubeTex);
                    bindSampler(3, 0);

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
                    renderedWater++;
                }
                frameDrawCalls_ += renderedWater;
                if (optRenderLOG) NC::LOGGING::Log("[WATER] Rendered: ", renderedWater, "/", waterDraws.size());

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

        // ==================== POST-ALPHA UNLIT PASS ====================
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


                MegaBuffer::instance().bind(); // bind once for all post-alpha draws
                int renderedPost = 0;
                bool loggedPostBinding = false;
                for (auto& dc : postAlphaUnlitDraws) {
                    if (!dc.mesh) continue;
                    if (dc.disabled) continue;
                    if (!PassesFrustumCulling(dc, frustum, disableFrustumCulling, true)) continue;

                    const bool additive = dc.cachedIsAdditive;

                    if (additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
                    else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

                    if (additive) {
                        // Swash ribbons are frequently single-sided in authoring; force two-sided in additive pass.
                        glDisable(GL_CULL_FACE);
                    } else if (disableFaceCulling || dc.cullMode <= 0) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        // Nebula-style cull mode mapping: 1=back, 2=front.
                        glCullFace(dc.cullMode == 2 ? GL_FRONT : GL_BACK);
                    }

                    float matEmissiveIntensity = dc.cachedMatEmissiveIntensity;
                    float alphaBlendFactor = dc.cachedAlphaBlendFactor;

                    float dcAnimTime = particleTime;
                    if (dc.instance) {
                        auto itSpawn = scene_.instanceSpawnTimes.find(dc.instance);
                        if (itSpawn != scene_.instanceSpawnTimes.end()) {
                            dcAnimTime = std::max(0.0f, particleTime - static_cast<float>(itSpawn->second));
                        }
                    }

                    auto animParams = DeferredRendererAnimation::SampleShaderVarAnimationsForTarget(dc.nodeName, dcAnimTime, dc.instance);
                    bool hasAnimatedMatEmissiveIntensity = false;
                    for (const auto& [paramName, value] : animParams) {
                        if (paramName == "MatEmissiveIntensity") {
                            matEmissiveIntensity = value;
                            hasAnimatedMatEmissiveIntensity = true;
                        } else if (paramName == "alphaBlendFactor") {
                            alphaBlendFactor = value;
                        } else {
                            postShader->SetFloat(paramName, value);
                        }
                    }

                    postShader->SetMat4("model", dc.worldMatrix);
                    postShader->SetFloat("MatEmissiveIntensity", matEmissiveIntensity);
                    postShader->SetFloat("alphaBlendFactor", alphaBlendFactor);

                    GLuint diffTex = dc.tex[0] ? toTextureHandle(dc.tex[0]) : whiteTex;
                    GLuint emsvTex = dc.tex[3] ? toTextureHandle(dc.tex[3]) : blackTex;
                    float diffContribution = 1.0f;

                    if (!dc.cachedHasCustomDiffMap && emsvTex && emsvTex != blackTex) {
                        diffTex = emsvTex;
                        diffContribution = 0.0f;
                    }

                    // Some swash/static additive nodes ship with MatEmissiveIntensity=0 and rely on
                    // animation. If no animated value is available, keep them visible.
                    if (additive &&
                        emsvTex && emsvTex != blackTex &&
                        !hasAnimatedMatEmissiveIntensity &&
                        matEmissiveIntensity <= 0.0f) {
                        matEmissiveIntensity = 1.0f;
                    }

                    postShader->SetFloat("MatEmissiveIntensity", matEmissiveIntensity);
                    postShader->SetFloat("diffContribution", diffContribution);

                    if (!loggedPostBinding) {
                        const bool hasAnimatedPose = DeferredRendererAnimation::HasAnimatedPoseForNode(dc.nodeName, dc.instance);
                        if (optRenderLOG) std::cout << "    [PostAlphaBind] node='" << dc.nodeName
                                  << "' type='" << dc.modelNodeType
                                  << "' DiffMap0=" << diffTex
                                  << " EmsvMap0=" << emsvTex
                                  << " emissiveIntensity=" << matEmissiveIntensity
                                  << " alphaBlendFactor=" << alphaBlendFactor
                                  << " cullMode=" << dc.cullMode
                                  << " hasAnimatedPose=" << (hasAnimatedPose ? 1 : 0)
                                  << " hasAnimatedEmsv=" << (hasAnimatedMatEmissiveIntensity ? 1 : 0) << "\n";
                        loggedPostBinding = true;
                    }

                    bindTexture(0, diffTex);
                    if (device_ && samplerRepeat_abstracted) {
                        device_->BindSampler(samplerRepeat_abstracted.get(), 0);
                    } else {
                        glBindSampler(0, gSamplerRepeat);
                    }
                    bindTexture(1, emsvTex);
                    if (device_ && samplerRepeat_abstracted) {
                        device_->BindSampler(samplerRepeat_abstracted.get(), 1);
                    } else {
                        glBindSampler(1, gSamplerRepeat);
                    }

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
                    renderedPost++;
                }
                frameDrawCalls_ += renderedPost;
                if (optRenderLOG) NC::LOGGING::Log("[POST_ALPHA] Rendered: ", renderedPost, "/", postAlphaUnlitDraws.size());

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

        // ==================== PARTICLE PASS ====================
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
                                         fogDistances, fogColor, gPosVS, invViewport,
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
        if (gEnableGLErrorChecking) {
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) std::cerr << "[FORWARD] Error: 0x" << std::hex << err << std::dec << "\n";
        }
        if (optRenderLOG) NC::LOGGING::Log("[FORWARD] Complete");
    };
    particleGraph->compile();

    if (optRenderLOG) std::cout << "[INIT] Frame graphs compiled successfully\n";

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

        scene_.Tick(deltaTime, camera_);
        scene_.PrepareDrawLists(camera_,
            solidDraws, alphaTestDraws, decalDraws, particleDraws,
            environmentDraws, environmentAlphaDraws, simpleLayerDraws,
            refractionDraws, postAlphaUnlitDraws, waterDraws, animatedDraws);

        solidShaderVarAnimatedIndices.clear();
        for (size_t i = 0; i < solidDraws.size(); ++i) {
            if (solidDraws[i].hasShaderVarAnimations)
                solidShaderVarAnimatedIndices.push_back(i);
        }

        const Camera::Frustum frustum = camera_.extractFrustum(camera_.getProjectionMatrix() * camera_.getViewMatrix());

        if (!AnimationsDisabled()) {
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

        if (!useLegacyDeferredInit_) {
            shadowGraph->execute();
            geometryGraph->execute();
            decalGraph->execute();
            lightingGraph->execute();
            particleGraph->execute();

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            if (device_) {
                device_->SetViewport({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f});

                NDEVC::Graphics::RenderStateDesc blitState;
                blitState.depth.depthTest = false;
                blitState.blend.blendEnable = false;
                auto state = device_->CreateRenderState(blitState);
                if (state) device_->ApplyRenderState(state.get());
            }

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
        }

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

            auto shaderProgram = shaderManager->GetShader("NDEVCdeferred");
            if(!shaderProgram) {
                std::cerr << "ERROR: Shader NDEVCdeferred not found!\n";
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

            if (optRenderLOG)  std::cout << "    NDEVCdeferred shader found, using it...\n";
            shaderProgram->Use();
            checkGLError("After shader use");

            shaderProgram->SetMat4("projection", camera_.getProjectionMatrix());
            shaderProgram->SetMat4("view", viewMatrix);
            shaderProgram->SetFloat("MatEmissiveIntensity", 0.0f);
            shaderProgram->SetFloat("MatSpecularIntensity", 0.0f);
            shaderProgram->SetFloat("MatSpecularPower", 0.0f);
            shaderProgram->SetInt("UseSkinning", 0);
            shaderProgram->SetInt("UseInstancing", 1);
            shaderProgram->SetInt("alphaTest", 0);
            shaderProgram->SetFloat("alphaCutoff", 0.5f);
            shaderProgram->SetInt("twoSided", 0);
            shaderProgram->SetInt("isFlatNormal", 0);

            glm::mat4 viewProj = camera_.getProjectionMatrix() * camera_.getViewMatrix();
            Camera::Frustum frustum = camera_.extractFrustum(viewProj);

            shaderProgram->SetInt("DiffMap0", 0);
            shaderProgram->SetInt("SpecMap0", 1);
            shaderProgram->SetInt("BumpMap0", 2);
            shaderProgram->SetInt("EmsvMap0", 3);

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

            DrawBatchSystem::instance().cull(solidDraws, frustum);
            checkGLError("After bucket culling");

            bindTexture(4, whiteTex);
            bindSampler(4, gSamplerRepeat);
            DrawBatchSystem::instance().flush(samplerRepeat_abstracted.get());
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

        this->bindTexture(0, gPositionVS);
        if (device_) { device_->BindSampler(nullptr, 0); } else { glBindSampler(0, 0); }
        this->bindTexture(1, gNormalDepthPacked);
        if (device_) { device_->BindSampler(nullptr, 1); } else { glBindSampler(1, 0); }
        this->bindTexture(2, gPositionWS);
        if (device_) { device_->BindSampler(nullptr, 2); } else { glBindSampler(2, 0); }

        for (int i = 0; i < legacyCascadeCount; i++) {
            this->bindTexture(3 + i, shadowMapCascades[i]);
            if (device_ && samplerShadow_abstracted) {
                device_->BindSampler(samplerShadow_abstracted.get(), 3 + i);
            } else {
                glBindSampler(3 + i, gSamplerShadow);
            }
        }

        shaderLight->SetInt("gPosition", 0);
        shaderLight->SetInt("gNormalDepthPacked", 1);
        shaderLight->SetInt("gPositionWS", 2);
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
            this->bindTexture(2, gPositionVS);
            if (device_) { device_->BindSampler(nullptr, 2); } else { glBindSampler(2, 0); }
            this->bindTexture(3, gEmissive);
            if (device_) { device_->BindSampler(nullptr, 3); } else { glBindSampler(3, 0); }
            this->bindTexture(4, gNormalDepthPacked);
            if (device_) { device_->BindSampler(nullptr, 4); } else { glBindSampler(4, 0); }

            composition->SetInt("lightBufferTex", 0);
            composition->SetInt("gAlbedoSpec", 1);
            composition->SetInt("gPositionVS", 2);
            composition->SetInt("gEmissiveTex", 3);
            composition->SetInt("gNormalDepthPacked", 4);
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

        scene_.UpdateIncrementalStreaming(false);

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

        RenderImGui();

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

        window_->SwapBuffers();
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
