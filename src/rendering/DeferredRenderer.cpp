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
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <filesystem>
#include <system_error>
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
#include "Core/VFS.h"
#include "gtx/norm.hpp"

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
static constexpr UniformID U_BACK_LIGHT_COLOR = NDEVC::Graphics::IShader::MakeUniformID("BackLightColor");
static constexpr UniformID U_BACK_LIGHT_OFFSET = NDEVC::Graphics::IShader::MakeUniformID("BackLightOffset");
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
static constexpr UniformID U_FOG_START = NDEVC::Graphics::IShader::MakeUniformID("fogStart");
static constexpr UniformID U_FOG_END = NDEVC::Graphics::IShader::MakeUniformID("fogEnd");
static constexpr UniformID U_FOG_COLOR = NDEVC::Graphics::IShader::MakeUniformID("fogColor");
static constexpr UniformID U_FOG_DISTANCES = NDEVC::Graphics::IShader::MakeUniformID("fogDistances");
static constexpr UniformID U_GALBEDO_SPEC = NDEVC::Graphics::IShader::MakeUniformID("gAlbedoSpec");
static constexpr UniformID U_LIGHT_BUFFER_TEX = NDEVC::Graphics::IShader::MakeUniformID("lightBufferTex");
static constexpr UniformID U_GPOSITION_VS = NDEVC::Graphics::IShader::MakeUniformID("gPositionVS");
static constexpr UniformID U_GEMISSIVE_TEX = NDEVC::Graphics::IShader::MakeUniformID("gEmissiveTex");
static constexpr UniformID U_USE_INSTANCED_POINT_LIGHTS = NDEVC::Graphics::IShader::MakeUniformID("UseInstancedPointLights");
static constexpr bool kDisableShadows = false;
static constexpr bool kDisableLighting = false;
static constexpr bool kDisableShadowPass = true;
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

std::string ReadEnvString(const char* name) {
    if (!name || !name[0]) return {};
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool MountPackageFromEnvironment() {
    if (NC::VFS::Instance().IsMounted()) {
        NC::LOGGING::Log("[DEFERRED][PACKAGE] VFS mounted, assets served in-memory");
        return true;
    }
    // Dev mode: no package present, assets loaded from disk via env-var paths.
    return false;
}

bool IsMapFilePath(const std::string& path) {
    if (path.empty()) return false;
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == ".map";
}

constexpr const char* StartupBaseMapName() {
    return "a0006_grimford_hub.map";
}

std::string ResolveStartupMapPath() {
    namespace fs = std::filesystem;
    const std::string baseMapName = StartupBaseMapName();

    auto fileExists = [](const fs::path& p) -> bool {
        if (p.empty()) return false;
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_regular_file(p, ec);
    };

    auto baseMapFromDirectory = [&](const fs::path& dirPath) -> std::string {
        if (dirPath.empty()) return {};

        std::error_code ec;
        if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
            return {};
        }

        const fs::path baseMapPath = dirPath / baseMapName;
        return fileExists(baseMapPath) ? baseMapPath.string() : std::string();
    };

    const std::string envPath = ReadEnvString("NDEVC_STARTUP_MAP");
    if (envPath.empty()) {
        NC::LOGGING::Log("[DEFERRED][INIT] Startup map autoload disabled (NDEVC_STARTUP_MAP empty)");
        return {};
    }
    if (!envPath.empty()) {
        const fs::path envFsPath(envPath);
        // Accept the path if it exists on disk OR is served by the in-memory VFS.
        if (fileExists(envFsPath) || NC::VFS::Instance().Exists(envPath)) {
            return envFsPath.string();
        }
        if (const std::string dirChoice = baseMapFromDirectory(envFsPath); !dirChoice.empty()) {
            return dirChoice;
        }
        auto firstMapFromDirectory = [&](const fs::path& dirPath) -> std::string {
            std::error_code ec;
            if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
                return {};
            }
            std::vector<fs::path> maps;
            for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() == ".map") {
                    maps.push_back(entry.path());
                }
            }
            if (maps.empty()) return {};
            std::sort(maps.begin(), maps.end());
            return maps.front().string();
        };
        if (const std::string dirAnyMap = firstMapFromDirectory(envFsPath); !dirAnyMap.empty()) {
            return dirAnyMap;
        }
        const std::array<fs::path, 2> envSubdirs = {
            envFsPath / "maps",
            envFsPath / "bin" / "maps"
        };
        for (const auto& subdir : envSubdirs) {
            if (const std::string dirChoice = baseMapFromDirectory(subdir); !dirChoice.empty()) {
                return dirChoice;
            }
            if (const std::string dirAnyMap = firstMapFromDirectory(subdir); !dirAnyMap.empty()) {
                return dirAnyMap;
            }
        }
        NC::LOGGING::Error("[Init] NDEVC_STARTUP_MAP not found/usable: ", envPath);
        return {};
    }
    return {};
}

bool ParticlesDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_PARTICLES");
    return disabled;
}

bool AnimationsDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ANIMATIONS");
    return disabled;
}

bool MaterialInputDebugEnabled() {
    static const bool enabled = ReadEnvToggle("NDEVC_DEBUG_MATERIAL_INPUTS");
    return enabled;
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

}

static void checkGLError(const char* label) {
    if (!gEnableGLErrorChecking) return;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        NC::LOGGING::Error("[GL] Error at ", (label ? label : "<unknown>"), ": 0x", std::hex, static_cast<unsigned int>(err), std::dec);
        switch (err) {
            case GL_INVALID_ENUM:                  NC::LOGGING::Error("[GL] Reason INVALID_ENUM"); break;
            case GL_INVALID_VALUE:                 NC::LOGGING::Error("[GL] Reason INVALID_VALUE"); break;
            case GL_INVALID_OPERATION:             NC::LOGGING::Error("[GL] Reason INVALID_OPERATION"); break;
            case GL_OUT_OF_MEMORY:                 NC::LOGGING::Error("[GL] Reason OUT_OF_MEMORY"); break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: NC::LOGGING::Error("[GL] Reason INVALID_FRAMEBUFFER_OPERATION"); break;
        }
    }
}

DeferredRenderer::DeferredRenderer() : shadowMapCascades{}, shadowFBOCascades{}
{
    NC::LOGGING::Log("[DEFERRED] ctor");
}

void DeferredRenderer::releaseOwnedGLResources() {
    NC::LOGGING::Log("[DEFERRED] releaseOwnedGLResources begin");
    shadowMatrixSSBO_.reset();
    shadowMatrixSSBOCapacity_ = 0;
    shadowIndirectBuffer_.reset();
    shadowIndirectBufferCapacity_ = 0;

    pointLightInstanceVBO.reset();
    pointLightInstanceCapacity = 0;

    quadVAO.reset();
    quadVBO.reset();
    screenVAO.id = 0;

    sphereVAO.reset();
    sphereIndexCount = 0;

    debugCellVAO_.reset();
    debugCellVBO_.reset();
    debugLineProgram_.reset();

    gDepthCopyReadFBO.reset();
    gDepthDecalFBO.reset();

    gBuffer.reset();
    lightFBO.reset();
    sceneFBO.reset();
    sceneFBO2.reset();

    for (int i = 0; i < NUM_CASCADES; ++i) {
        shadowFBOCascades[i].reset();
        shadowMapCascades[i].id = 0;
    }

    gPositionVS.reset();
    gPositionWS.reset();
    gPosition.reset();
    gNormalDepthPacked.reset();
    gAlbedoSpec.reset();
    gDepth.reset();
    gEmissive.reset();
    gPositionWSRead.reset();

    if (samplerShadow_abstracted) {
        gSamplerShadow.id = 0;
    } else {
        gSamplerShadow.reset();
    }
    NC::LOGGING::Log("[DEFERRED] releaseOwnedGLResources end");
}

DeferredRenderer::~DeferredRenderer() {
    NC::LOGGING::Log("[DEFERRED] dtor");
    Shutdown();
}

void DeferredRenderer::initGLFW() {
    NC::LOGGING::Log("[DEFERRED] initGLFW begin w=", width, " h=", height);
    using GLFWPlatformType = NDEVC::Platform::GLFW::GLFWPlatform;
    platform_ = std::make_unique<GLFWPlatformType>();
    if (platform_->Initialize()) {
        window_ = platform_->CreateApplicationWindow("NDEVC", width, height);
        if (window_) inputSystem_ = platform_->CreateInputSystem();
    }
    if (!window_) {
        NC::LOGGING::Error("[DEFERRED] initGLFW failed: Window creation failed");
        throw std::runtime_error("Window creation failed");
    }
    window_->MakeCurrent();
    if (GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window_->GetNativeHandle())) {
        glfwSetWindowUserPointer(glfwWin, this);
        window_->SetFramebufferSizeCallback([this](int w, int h) {
            if (w <= 0 || h <= 0) return;
            this->Resize(w, h);
        });
        window_->SetScrollCallback([this](double, double scrollY) {
            if (editorModeEnabled_ && editorViewportInputRouting_) {
                double cursorX = 0.0;
                double cursorY = 0.0;
                if (window_) {
                    window_->GetCursorPos(cursorX, cursorY);
                }
                if (!IsSceneViewportPointerInside(cursorX, cursorY)) return;
            } else if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
                return;
            }
            this->camera_.processMouseScroll(static_cast<float>(scrollY));
        });
        glfwSetDropCallback(glfwWin, [](GLFWwindow* w, int count, const char** paths) {
            auto* self = static_cast<DeferredRenderer*>(glfwGetWindowUserPointer(w));
            if (!self || count <= 0 || !paths) return;
            self->pendingDroppedPaths_.reserve(self->pendingDroppedPaths_.size() + static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (paths[i] && paths[i][0] != '\0') {
                    self->pendingDroppedPaths_.emplace_back(paths[i]);
                }
            }
        });
        glfwSetCursorPosCallback(glfwWin, [](GLFWwindow* w, double x, double y) {
            static double lastX = x;
            static double lastY = y;
            static bool first = true;

            auto* self = static_cast<DeferredRenderer*>(glfwGetWindowUserPointer(w));
            if (!self) return;

            if (self->editorModeEnabled_ && self->editorViewportInputRouting_) {
                if (!self->IsSceneViewportPointerInside(x, y)) {
                    lastX = x;
                    lastY = y;
                    first = true;
                    return;
                }
            } else if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
                // Reset camera-look delta state while UI is consuming mouse input.
                lastX = x;
                lastY = y;
                first = true;
                return;
            }

            if (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
                lastX = x;
                lastY = y;
                first = true;
                return;
            }

            if (first) {
                lastX = x;
                lastY = y;
                first = false;
                return;
            }

            const float dx = static_cast<float>(x - lastX);
            const float dy = static_cast<float>(lastY - y);
            lastX = x;
            lastY = y;
            self->camera_.processMouseMovement(dx, dy);
        });
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        NC::LOGGING::Error("[DEFERRED] initGLFW failed: Failed to init GLAD");
        throw std::runtime_error("Failed to init GLAD");
    }

    if (!InitializeImGui()) {
        NC::LOGGING::Error("[DEFERRED] initGLFW failed: Failed to initialize ImGui");
        throw std::runtime_error("Failed to initialize ImGui");
    }

    device_ = std::make_unique<NDEVC::Graphics::OpenGL::OpenGLDevice>();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    window_->GetFramebufferSize(width, height);
    camera_.updateAspectRatio(width, height);
    glViewport(0, 0, width, height);

    if (optRenderLOG) std::cout << "[Init] Window framebuffer: alpha bits requested\n";
    if (optRenderLOG) std::cout << "[Init] Clear color set to opaque black (RGBA: 0,0,0,1)\n";
    if (optRenderLOG) {
        const std::string vsyncEnv = ReadEnvString("NDEVC_VSYNC");
        const int swapInterval = vsyncEnv.empty() ? 0 : std::atoi(vsyncEnv.c_str());
        std::cout << "[Init] VSync swap interval = " << swapInterval << "\n";
    }

    try {
        shaderManager = std::make_unique<NDEVC::Graphics::OpenGL::OpenGLShaderManager>();
        shaderManager->Initialize();
        NC::LOGGING::Log("[DEFERRED] ShaderManager initialized");
    } catch(const std::exception& e) {
        NC::LOGGING::Error("[DEFERRED] ShaderManager init failed: ", e.what());
        throw;
    }

    if (!ParticlesDisabled()) {
        Particles::ParticleServer::Instance().Open(shaderManager.get());
        if (!Particles::ParticleServer::Instance().GetParticleRenderer()) {
            NC::LOGGING::Error("[DEFERRED] ParticleServer failed to provide renderer");
        } else {
            if (optRenderLOG) std::cout << "[Init] ParticleServer renderer ready\n";
            NC::LOGGING::Log("[DEFERRED] ParticleServer renderer ready");
        }
    } else if (optRenderLOG) {
        std::cout << "[Init] Particle system disabled via NDEVC_DISABLE_PARTICLES\n";
    }

    using namespace NDEVC::Graphics;

    unsigned char white[] = {255, 255, 255, 255};
    TextureDesc whiteDesc;
    whiteDesc.type = TextureType::Texture2D;
    whiteDesc.format = Format::RGBA8_UNORM;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteTex_abstracted = device_->CreateTexture(whiteDesc);
    if (whiteTex_abstracted) {
        whiteTex.id = *(GLuint*)whiteTex_abstracted->GetNativeHandle();
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }

    unsigned char black[] = {0, 0, 0, 255};
    TextureDesc blackDesc;
    blackDesc.type = TextureType::Texture2D;
    blackDesc.format = Format::RGBA8_UNORM;
    blackDesc.width = 1;
    blackDesc.height = 1;
    blackTex_abstracted = device_->CreateTexture(blackDesc);
    if (blackTex_abstracted) {
        blackTex.id = *(GLuint*)blackTex_abstracted->GetNativeHandle();
        glBindTexture(GL_TEXTURE_2D, blackTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    }

    // Normal maps in this renderer decode XY from A/G channels.
    // Keep both AG and RGB fallbacks neutral to avoid skewed reflections when BumpMap is missing.
    unsigned char normal[] = {128, 128, 255, 128};
    TextureDesc normalDesc;
    normalDesc.type = TextureType::Texture2D;
    normalDesc.format = Format::RGBA8_UNORM;
    normalDesc.width = 1;
    normalDesc.height = 1;
    normalTex_abstracted = device_->CreateTexture(normalDesc);
    if (normalTex_abstracted) {
        normalTex.id = *(GLuint*)normalTex_abstracted->GetNativeHandle();
        glBindTexture(GL_TEXTURE_2D, normalTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, normal);
    }

    if (!blackCubeTex) {
        TextureDesc cubemapDesc;
        cubemapDesc.type = TextureType::TextureCube;
        cubemapDesc.format = Format::RGBA8_UNORM;
        cubemapDesc.width = 1;
        cubemapDesc.height = 1;
        cubemapDesc.isCubemap = true;
        blackCubeTex_abstracted = device_->CreateTexture(cubemapDesc);
        if (blackCubeTex_abstracted) {
            blackCubeTex.id = *(GLuint*)blackCubeTex_abstracted->GetNativeHandle();
            glBindTexture(GL_TEXTURE_CUBE_MAP, blackCubeTex);
            for (int face = 0; face < 6; ++face) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, 1, 1, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, black);
            }
        }
    }

    SamplerDesc samplerRepeatDesc;
    samplerRepeatDesc.minFilter = SamplerFilter::LinearMipmapLinear;
    samplerRepeatDesc.magFilter = SamplerFilter::Linear;
    samplerRepeatDesc.wrapS = SamplerWrap::Repeat;
    samplerRepeatDesc.wrapT = SamplerWrap::Repeat;
    samplerRepeat_abstracted = device_->CreateSampler(samplerRepeatDesc);
    if (samplerRepeat_abstracted) {
        gSamplerRepeat.id = *(GLuint*)samplerRepeat_abstracted->GetNativeHandle();
    }

    SamplerDesc samplerClampDesc;
    samplerClampDesc.minFilter = SamplerFilter::LinearMipmapLinear;
    samplerClampDesc.magFilter = SamplerFilter::Linear;
    samplerClampDesc.wrapS = SamplerWrap::ClampToEdge;
    samplerClampDesc.wrapT = SamplerWrap::ClampToEdge;
    samplerClamp_abstracted = device_->CreateSampler(samplerClampDesc);
    if (samplerClamp_abstracted) {
        gSamplerClamp.id = *(GLuint*)samplerClamp_abstracted->GetNativeHandle();
    }

    if (TextureServer::sBindlessSupported) {
        if (whiteTex.id) {
            fallbackWhiteHandle_ = glGetTextureHandleARB(whiteTex.id);
            if (fallbackWhiteHandle_) glMakeTextureHandleResidentARB(fallbackWhiteHandle_);
        }
        if (blackTex.id) {
            fallbackBlackHandle_ = glGetTextureHandleARB(blackTex.id);
            if (fallbackBlackHandle_) glMakeTextureHandleResidentARB(fallbackBlackHandle_);
        }
        if (normalTex.id) {
            fallbackNormalHandle_ = glGetTextureHandleARB(normalTex.id);
            if (fallbackNormalHandle_) glMakeTextureHandleResidentARB(fallbackNormalHandle_);
        }
        if (blackCubeTex.id) {
            fallbackBlackCubeHandle_ = glGetTextureHandleARB(blackCubeTex.id);
            if (fallbackBlackCubeHandle_) glMakeTextureHandleResidentARB(fallbackBlackCubeHandle_);
        }
    }

    NC::LOGGING::Log("[DEFERRED] initGLFW end framebuffer=", width, "x", height,
                     " shaderManager=", (shaderManager ? 1 : 0),
                     " particlesDisabled=", (ParticlesDisabled() ? 1 : 0));
}

void DeferredRenderer::initCascadedShadowMaps() {
    using namespace NDEVC::Graphics;

    for (int i = 0; i < NUM_CASCADES; i++) {
        TextureDesc depthDesc;
        depthDesc.type = TextureType::Texture2D;
        depthDesc.format = Format::D32_FLOAT_S8_UINT;
        depthDesc.width = SHADOW_WIDTH;
        depthDesc.height = SHADOW_HEIGHT;

        shadowMapTextures[i] = device_->CreateTexture(depthDesc);
        shadowMapCascades[i].id = *(GLuint*)shadowMapTextures[i]->GetNativeHandle();

        glGenFramebuffers(1, shadowFBOCascades[i].put());
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOCascades[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D, shadowMapCascades[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            NC::LOGGING::Error("[CSM] Cascade ", i, " FBO incomplete: 0x", std::hex, status, std::dec);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (optRenderLOG) std::cout << "[CSM] Shadow Map Details:\n";
    for (int i = 0; i < NUM_CASCADES; i++) {
        if (optRenderLOG) std::cout << "  Cascade " << i << ": FBO=" << shadowFBOCascades[i]
                  << " DepthTex=" << shadowMapCascades[i] << " Size=" << SHADOW_WIDTH << "x" << SHADOW_HEIGHT << "\n";
    }
    if (optRenderLOG) std::cout << "[CSM] Initialized " << NUM_CASCADES << " cascades\n";

}

void DeferredRenderer::initDeferred() {
    glGenFramebuffers(1, gBuffer.put());
    if (optRenderLOG) std::cout << "[InitDeferred] Created gBuffer FBO " << gBuffer << "\n";
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

    auto createLegacyTex2D = [&](NDEVC::GL::GLTexHandle& tex, GLint intFmt, GLenum fmt, GLenum type) {
        glGenTextures(1, tex.put());
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, intFmt, width, height, 0, fmt, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    createLegacyTex2D(gPositionVS, GL_RGBA32F, GL_RGBA, GL_FLOAT);
    createLegacyTex2D(gNormalDepthPacked, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    createLegacyTex2D(gAlbedoSpec, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    createLegacyTex2D(gPositionWS, GL_RGBA32F, GL_RGBA, GL_FLOAT);
    createLegacyTex2D(gEmissive, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    createLegacyTex2D(gDepth, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPositionVS, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormalDepthPacked, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoSpec, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gPositionWS, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, gEmissive, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);

    const GLenum gBufferDrawBuffers[5] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
        GL_COLOR_ATTACHMENT4
    };
    glDrawBuffers(5, gBufferDrawBuffers);
    const GLenum gBufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (gBufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        NC::LOGGING::Error("[GBuffer ERROR] Status: 0x", std::hex, gBufferStatus, std::dec);
    }

    using namespace NDEVC::Graphics;

    TextureDesc lightBufferDesc;
    lightBufferDesc.type = TextureType::Texture2D;
    lightBufferDesc.format = Format::RGBA16F;
    lightBufferDesc.width = width;
    lightBufferDesc.height = height;
    lightBufferTex = device_->CreateTexture(lightBufferDesc);
    lightBuffer.id = *(GLuint*)lightBufferTex->GetNativeHandle();

    glGenFramebuffers(1, lightFBO.put());
    glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lightBuffer, 0);
    GLenum lightStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (lightStatus != GL_FRAMEBUFFER_COMPLETE)
        NC::LOGGING::Error("[LightFBO ERROR] Status: 0x", std::hex, lightStatus, std::dec);

    TextureDesc sceneColorDesc;
    sceneColorDesc.type = TextureType::Texture2D;
    sceneColorDesc.format = Format::RGBA16F;
    sceneColorDesc.width = width;
    sceneColorDesc.height = height;
    sceneColorTex = device_->CreateTexture(sceneColorDesc);
    sceneColor.id = *(GLuint*)sceneColorTex->GetNativeHandle();

    glGenFramebuffers(1, sceneFBO.put());
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);

    TextureDesc depthStencilDesc;
    depthStencilDesc.type = TextureType::Texture2D;
    depthStencilDesc.format = Format::D24_UNORM_S8_UINT;
    depthStencilDesc.width = width;
    depthStencilDesc.height = height;
    sceneDepthStencilTex = device_->CreateTexture(depthStencilDesc);
    sceneDepthStencil.id = *(GLuint*)sceneDepthStencilTex->GetNativeHandle();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, sceneDepthStencil, 0);

    GLenum sceneStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (sceneStatus != GL_FRAMEBUFFER_COMPLETE)
        NC::LOGGING::Error("[SceneFBO ERROR] Status: 0x", std::hex, sceneStatus, std::dec);

    TextureDesc sceneColor2Desc;
    sceneColor2Desc.type = TextureType::Texture2D;
    sceneColor2Desc.format = Format::RGBA16F;
    sceneColor2Desc.width = width;
    sceneColor2Desc.height = height;
    sceneColor2Tex = device_->CreateTexture(sceneColor2Desc);
    sceneColor2.id = *(GLuint*)sceneColor2Tex->GetNativeHandle();

    glGenFramebuffers(1, sceneFBO2.put());
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor2, 0);
    GLenum scene2Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (scene2Status != GL_FRAMEBUFFER_COMPLETE)
        NC::LOGGING::Error("[SceneFBO2 ERROR] Status: 0x", std::hex, scene2Status, std::dec);

    glGenTextures(1, gPositionWSRead.put());
    glBindTexture(GL_TEXTURE_2D, gPositionWSRead);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DeferredRenderer::resizeFramebuffers(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    width = newWidth;
    height = newHeight;

    if (shadowGraph) {
        shadowGraph->setDimensions(SHADOW_WIDTH, SHADOW_HEIGHT);
        shadowGraph->compile();
    }
    if (geometryGraph) {
        geometryGraph->setDimensions(width, height);
        geometryGraph->compile();
    }
    if (decalGraph) {
        decalGraph->setDimensions(width, height);
        decalGraph->compile();
    }
    if (lightingGraph) {
        lightingGraph->setDimensions(width, height);
        lightingGraph->compile();
    }
    if (particleGraph) {
        particleGraph->setDimensions(width, height);
        particleGraph->compile();
    }

    auto resizeTex2D = [&](GLuint tex, GLint intFmt, GLenum fmt, GLenum type) {
        if (tex == 0) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, intFmt, width, height, 0, fmt, type, nullptr);
    };

    resizeTex2D(lightBuffer,        GL_RGBA16F, GL_RGBA, GL_FLOAT);
    resizeTex2D(sceneColor,         GL_RGBA16F, GL_RGBA, GL_FLOAT);
    resizeTex2D(sceneColor2,        GL_RGBA16F, GL_RGBA, GL_FLOAT);
    resizeTex2D(sceneDepthStencil,  GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
    resizeTex2D(gPositionVS,        GL_RGBA32F, GL_RGBA, GL_FLOAT);
    resizeTex2D(gNormalDepthPacked, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    resizeTex2D(gAlbedoSpec,        GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    resizeTex2D(gPositionWS,        GL_RGBA32F, GL_RGBA, GL_FLOAT);
    resizeTex2D(gDepth,             GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
    resizeTex2D(gEmissive,          GL_RGBA16F, GL_RGBA, GL_FLOAT);
    resizeTex2D(gPositionWSRead,    GL_RGBA32F, GL_RGBA, GL_FLOAT);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (device_) {
        device_->SetViewport({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f});
    }

    camera_.updateAspectRatio(width, height);
}

void DeferredRenderer::generateSphereMesh() {
    using namespace NDEVC::Graphics;

    const int latSegments = 12;
    const int lonSegments = 24;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (int lat = 0; lat <= latSegments; lat++) {
        float theta = static_cast<float>(lat) * 3.14159265358979f / static_cast<float>(latSegments);
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);
        for (int lon = 0; lon <= lonSegments; lon++) {
            float phi = static_cast<float>(lon) * 2.0f * 3.14159265358979f / static_cast<float>(lonSegments);
            float x = std::cos(phi) * sinTheta;
            float y = cosTheta;
            float z = std::sin(phi) * sinTheta;
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    for (int lat = 0; lat < latSegments; lat++) {
        for (int lon = 0; lon < lonSegments; lon++) {
            int current = lat * (lonSegments + 1) + lon;
            int next = current + lonSegments + 1;
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    sphereIndexCount = static_cast<int>(indices.size());

    BufferDesc vboDesc;
    vboDesc.type = BufferType::Vertex;
    vboDesc.size = vertices.size() * sizeof(float);
    vboDesc.initialData = vertices.data();
    sphereVBO_abstracted = device_->CreateBuffer(vboDesc);
    if (sphereVBO_abstracted) {
        sphereVBO.id = *(GLuint*)sphereVBO_abstracted->GetNativeHandle();
    }

    BufferDesc eboDesc;
    eboDesc.type = BufferType::Index;
    eboDesc.size = indices.size() * sizeof(unsigned int);
    eboDesc.initialData = indices.data();
    sphereEBO_abstracted = device_->CreateBuffer(eboDesc);
    if (sphereEBO_abstracted) {
        sphereEBO.id = *(GLuint*)sphereEBO_abstracted->GetNativeHandle();
    }

    glGenVertexArrays(1, sphereVAO.put());
    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    if (!pointLightInstanceVBO) {
        glGenBuffers(1, pointLightInstanceVBO.put());
    }
    glBindBuffer(GL_ARRAY_BUFFER, pointLightInstanceVBO);
    pointLightInstanceCapacity = 1;
    glBufferData(GL_ARRAY_BUFFER, pointLightInstanceCapacity * sizeof(glm::vec4) * 2, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4) * 2, (void*)0);
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4) * 2, (void*)sizeof(glm::vec4));
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);
}

void DeferredRenderer::renderCascadedShadows(const glm::vec3& camPos, const glm::vec3& camForward)
{
    frameProfile_.shadowGroup  = 0.0;
    frameProfile_.shadowUpload = 0.0;
    frameProfile_.shadowDraw   = 0.0;
    frameProfile_.shadowSL     = 0.0;
    frameProfile_.shadowCasterBuild = 0.0;
    static int frameCount = 0;
    static bool logShadows = (frameCount++ == 5);
    glm::vec3 lightDirWorld = -kLightDirToSun;

    auto tCasterStart = std::chrono::steady_clock::now();
    const glm::mat4 viewMatrix = camera_.getViewMatrix();

    if (shadowCastersDirty_) {
        shadowCasters_.clear();
        shadowCasters_.reserve(solidDraws.size());
        simpleLayerShadowCasters_.clear();
        simpleLayerShadowCasters_.reserve(simpleLayerDraws.size());

        for (const auto& obj : solidDraws) {
            if (!obj.mesh || obj.shadowFiltered) continue;

            glm::vec3 wc;
            float r;
            if (obj.cullBoundsValid) {
                wc = obj.cullWorldCenter;
                r = obj.cullWorldRadius;
            } else {
                const glm::vec3 localCenter = (glm::vec3(obj.localBoxMin) + glm::vec3(obj.localBoxMax)) * 0.5f;
                const float localRadius = glm::length(glm::vec3(obj.localBoxMax) - glm::vec3(obj.localBoxMin)) * 0.5f;
                const float sx = glm::length(glm::vec3(obj.worldMatrix[0]));
                const float sy = glm::length(glm::vec3(obj.worldMatrix[1]));
                const float sz = glm::length(glm::vec3(obj.worldMatrix[2]));
                r = localRadius * std::max(sx, std::max(sy, sz));
                wc = glm::vec3(obj.worldMatrix * glm::vec4(localCenter, 1.0f));
            }

            const float vd = -(viewMatrix[0][2]*wc.x + viewMatrix[1][2]*wc.y + viewMatrix[2][2]*wc.z + viewMatrix[3][2]);
            shadowCasters_.push_back(ShadowCaster{&obj, r, vd, wc});
        }

        // Pre-group solid casters by geomKey once — reused across cascades and frames
        {
            std::unordered_map<uint64_t, size_t> keyToIdx;
            solidShadowGeomGroups_.clear();
            for (uint32_t ci = 0; ci < static_cast<uint32_t>(shadowCasters_.size()); ++ci) {
                const DrawCmd& obj = *shadowCasters_[ci].draw;
                auto addGrp = [&](const Nvx2Group& g) {
                    if (g.indexCount() == 0) return;
                    const uint32_t cnt = g.indexCount();
                    const uint32_t fIdx = obj.megaIndexOffset + g.firstIndex();
                    const uint64_t gk = (static_cast<uint64_t>(cnt) << 32) | fIdx;
                    auto [it, ins] = keyToIdx.try_emplace(gk, solidShadowGeomGroups_.size());
                    if (ins) solidShadowGeomGroups_.push_back({cnt, fIdx, {}});
                    solidShadowGeomGroups_[it->second].casterIndices.push_back(ci);
                };
                if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
                    addGrp(obj.mesh->groups[obj.group]);
                } else {
                    for (const auto& g : obj.mesh->groups) addGrp(g);
                }
            }
        }

        for (const auto& obj : simpleLayerDraws) {
            if (!obj.mesh) continue;

            glm::vec3 wc;
            float r;
            if (obj.cullBoundsValid) {
                wc = obj.cullWorldCenter;
                r = obj.cullWorldRadius;
            } else {
                const glm::vec3 localCenter = (glm::vec3(obj.localBoxMin) + glm::vec3(obj.localBoxMax)) * 0.5f;
                const float localRadius = glm::length(glm::vec3(obj.localBoxMax) - glm::vec3(obj.localBoxMin)) * 0.5f;
                const float sx = glm::length(glm::vec3(obj.worldMatrix[0]));
                const float sy = glm::length(glm::vec3(obj.worldMatrix[1]));
                const float sz = glm::length(glm::vec3(obj.worldMatrix[2]));
                r = localRadius * std::max(sx, std::max(sy, sz));
                wc = glm::vec3(obj.worldMatrix * glm::vec4(localCenter, 1.0f));
            }

            const float vd = -(viewMatrix[0][2]*wc.x + viewMatrix[1][2]*wc.y + viewMatrix[2][2]*wc.z + viewMatrix[3][2]);
            simpleLayerShadowCasters_.push_back(ShadowCaster{&obj, r, vd, wc});
        }

        shadowCastersDirty_ = false;
        shadowCasterCacheHit_ = false;
        shadowGroupCacheValid_ = false;
    } else {
        shadowCasterCacheHit_ = true;
        const float vr0 = viewMatrix[0][2], vr1 = viewMatrix[1][2],
                    vr2 = viewMatrix[2][2], vr3 = viewMatrix[3][2];
        for (auto& c : shadowCasters_)
            c.viewDepth = -(vr0*c.worldCenter.x + vr1*c.worldCenter.y + vr2*c.worldCenter.z + vr3);
        for (auto& c : simpleLayerShadowCasters_)
            c.viewDepth = -(vr0*c.worldCenter.x + vr1*c.worldCenter.y + vr2*c.worldCenter.z + vr3);
    }

    frameProfile_.shadowCasterBuild = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tCasterStart).count();

    if (logShadows) {
        std::cout << "  Computing light space matrices for " << NUM_CASCADES << " cascades\n";
        std::cout << "  Light direction: (" << lightDirWorld.x << ", " << lightDirWorld.y << ", " << lightDirWorld.z << ")\n";
        std::cout << "  Shadow casters considered: " << shadowCasters_.size() << "/" << solidDraws.size() << "\n";
        std::cout << "  SimpleLayer shadow casters considered: " << simpleLayerShadowCasters_.size() << "/" << simpleLayerDraws.size() << "\n";
    }

    const float fovY = glm::radians(camera_.getFov());
    const float tanHalfFovY = std::tan(fovY * 0.5f);
    const float aspect = std::max(0.001f, camera_.getAspectRatio());
    const glm::mat4 invView = glm::inverse(viewMatrix);

    for (int i = 0; i < NUM_CASCADES; i++) {
        const float cascadeNear = cascadeSplits[i];
        const float cascadeFar = cascadeSplits[i + 1];

        const float nearHalfH = cascadeNear * tanHalfFovY;
        const float nearHalfW = nearHalfH * aspect;
        const float farHalfH = cascadeFar * tanHalfFovY;
        const float farHalfW = farHalfH * aspect;

        const glm::vec3 cornersVS[8] = {
            {-nearHalfW,  nearHalfH, -cascadeNear},
            { nearHalfW,  nearHalfH, -cascadeNear},
            { nearHalfW, -nearHalfH, -cascadeNear},
            {-nearHalfW, -nearHalfH, -cascadeNear},
            {-farHalfW,   farHalfH,  -cascadeFar},
            { farHalfW,   farHalfH,  -cascadeFar},
            { farHalfW,  -farHalfH,  -cascadeFar},
            {-farHalfW,  -farHalfH,  -cascadeFar}
        };

        glm::vec3 cornersWS[8];
        glm::vec3 frustumCenterWS(0.0f);
        for (int c = 0; c < 8; ++c) {
            cornersWS[c] = glm::vec3(invView * glm::vec4(cornersVS[c], 1.0f));
            frustumCenterWS += cornersWS[c];
        }
        frustumCenterWS /= 8.0f;

        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(up, lightDirWorld)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        const float lightDistance = cascadeFar + 500.0f;
        // Keep orientation fixed from light direction, but anchor translation to
        // the current cascade center to avoid large-coordinate precision drift.
        const glm::vec3 lightEye = frustumCenterWS - lightDirWorld * lightDistance;
        const glm::mat4 lightView = glm::lookAt(lightEye, frustumCenterWS, up);

        glm::vec3 minLS(std::numeric_limits<float>::max());
        glm::vec3 maxLS(std::numeric_limits<float>::lowest());
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 cornerLS = glm::vec3(lightView * glm::vec4(cornersWS[c], 1.0f));
            minLS = glm::min(minLS, cornerLS);
            maxLS = glm::max(maxLS, cornerLS);
        }

        const float cascadeDepthPadding = 500.0f;
        minLS.z -= cascadeDepthPadding;
        maxLS.z += cascadeDepthPadding;

        float radius = 0.0f;
        for (int c = 0; c < 8; ++c) {
            radius = std::max(radius, glm::length(cornersWS[c] - frustumCenterWS));
        }

        glm::vec2 centerLS = glm::vec2(lightView * glm::vec4(frustumCenterWS, 1.0f));
        if (radius > 0.0f) {
            const float texelSize = (radius * 2.0f) / static_cast<float>(SHADOW_WIDTH);
            centerLS.x = std::round(centerLS.x / texelSize) * texelSize;
            centerLS.y = std::round(centerLS.y / texelSize) * texelSize;
        }

        const float minX = centerLS.x - radius;
        const float maxX = centerLS.x + radius;
        const float minY = centerLS.y - radius;
        const float maxY = centerLS.y + radius;

        // GLM orthographic near/far are distances (positive), while points in front
        // of the light camera are negative z in light-view space.
        const float zNear = std::max(0.1f, -maxLS.z);
        const float zFar = std::max(zNear + 1.0f, -minLS.z);
        const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, zNear, zFar);
        lightSpaceMatrices[i] = lightProjection * lightView;

        if (logShadows) {
            std::cout << "    Cascade " << i << ": range [" << cascadeNear << " - " << cascadeFar
                      << "]m, radius=" << radius << "\n";
        }
    }

    if (logShadows) std::cout << "  Rendering shadow maps for each cascade\n";

    for (int cascade = 0; cascade < NUM_CASCADES; cascade++) {
        const float cascadeNear = cascadeSplits[cascade];
        const float cascadeFar = cascadeSplits[cascade + 1];

        if (logShadows) {
            std::cout << "\n    === Cascade " << cascade << " ===\n";
            std::cout << "    FBO=" << shadowFBOCascades[cascade]
                      << " Tex=" << shadowMapCascades[cascade]
                      << " Size=" << SHADOW_WIDTH << "x" << SHADOW_HEIGHT
                      << " Range=[" << cascadeNear << " - " << cascadeFar << "]\n";
        }

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOCascades[cascade]);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        checkGLError("After bind shadow FBO");

        if (logShadows) {
            GLint currentFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            std::cout << "    Bound FBO: " << currentFBO << " Status: 0x" << std::hex << fboStatus << std::dec;
            if (fboStatus == GL_FRAMEBUFFER_COMPLETE) std::cout << " (COMPLETE)";
            else std::cout << " (INCOMPLETE!)";
            std::cout << "\n";

            GLint depthAttachment = 0;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthAttachment);
            std::cout << "    Depth attachment: " << depthAttachment << "\n";
        }

        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glClear(GL_DEPTH_BUFFER_BIT);
        checkGLError("After clear shadow depth");

        if (device_) {
            NDEVC::Graphics::RenderStateDesc shadowState;
            shadowState.depth.depthTest = true;
            shadowState.depth.depthWrite = true;
            shadowState.depth.depthFunc = NDEVC::Graphics::CompareFunc::Less;
            device_->ApplyRenderState(device_->CreateRenderState(shadowState).get());
        } else {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
        }
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        checkGLError("After shadow state setup");

        if (logShadows) {
            GLboolean depthTest, depthMask;
            GLint depthFunc;
            glGetBooleanv(GL_DEPTH_TEST, &depthTest);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
            std::cout << "    Shadow GL State: DepthTest=" << (int)depthTest
                      << " DepthMask=" << (int)depthMask
                      << " DepthFunc=0x" << std::hex << depthFunc << std::dec << "\n";
        }

        auto shadowShaders = shaderManager->GetShader("lightShadows");
        if (!shadowShaders) {
            NC::LOGGING::Error("[SHADOW] light not found/not working");
        } else {
            shadowShaders->Use();
            checkGLError("After use shadow shader");

            if (logShadows) {
                GLint currentProgram = shadowShaders->GetNativeHandle()
                    ? static_cast<GLint>(*reinterpret_cast<GLuint*>(shadowShaders->GetNativeHandle()))
                    : 0;
                std::cout << "    Shadow shader program: " << currentProgram << "\n";
            }

            shadowShaders->SetMat4("lightSpaceMatrix", lightSpaceMatrices[cascade]);
            checkGLError("After set lightSpaceMatrix");
        }

        if (shadowShaders) {
            auto tGroupStart = std::chrono::steady_clock::now();
            shadowMatrices_.clear();
            shadowCommands_.clear();

            const bool groupCacheHit = shadowGroupCacheValid_ &&
                                       shadowCasterCacheHit_ &&
                                       shadowGroupCacheViewMatrix_ == viewMatrix;
            int triangleCount = 0;
            if (groupCacheHit) {
                shadowMatrices_ = shadowGroupCachedMatrices_[cascade];
                shadowCommands_ = shadowGroupCachedCommands_[cascade];
            } else {
            shadowMatrices_.reserve(shadowCasters_.size());

            for (const auto& grp : solidShadowGeomGroups_) {
                const uint32_t firstMat = static_cast<uint32_t>(shadowMatrices_.size());
                uint32_t inRange = 0;
                for (uint32_t ci : grp.casterIndices) {
                    const ShadowCaster& caster = shadowCasters_[ci];
                    if (caster.draw->disabled) continue;
                    if (caster.viewDepth + caster.radius < cascadeNear) continue;
                    if (caster.viewDepth - caster.radius > cascadeFar) continue;
                    shadowMatrices_.push_back(caster.draw->worldMatrix);
                    ++inRange;
                }
                if (inRange > 0) {
                    DrawCommand cmd{};
                    cmd.count = grp.indexCount;
                    cmd.instanceCount = inRange;
                    cmd.firstIndex = grp.firstIndex;
                    cmd.baseVertex = 0;
                    cmd.baseInstance = firstMat;
                    shadowCommands_.push_back(cmd);
                    triangleCount += static_cast<int>(grp.indexCount / 3) * inRange;
                }
            }
            shadowGroupCachedMatrices_[cascade] = shadowMatrices_;
            shadowGroupCachedCommands_[cascade] = shadowCommands_;
            } // end !groupCacheHit
            frameProfile_.shadowGroup += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - tGroupStart).count();

            if (!shadowCommands_.empty()) {
                auto tUploadStart = std::chrono::steady_clock::now();
                // Upload model matrices to SSBO at binding point 1
                if (!shadowMatrixSSBO_) glGenBuffers(1, shadowMatrixSSBO_.put());
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, shadowMatrixSSBO_);
                const size_t matrixBytes = shadowMatrices_.size() * sizeof(glm::mat4);
                if (matrixBytes > shadowMatrixSSBOCapacity_) {
                    shadowMatrixSSBOCapacity_ = matrixBytes * 2;
                }
                // Orphan previous storage to avoid sync stall with in-flight GPU reads
                glBufferData(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLsizeiptr>(shadowMatrixSSBOCapacity_),
                             nullptr, GL_STREAM_DRAW);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                static_cast<GLsizeiptr>(matrixBytes),
                                shadowMatrices_.data());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, shadowMatrixSSBO_);

                // Upload indirect commands
                if (!shadowIndirectBuffer_) glGenBuffers(1, shadowIndirectBuffer_.put());
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, shadowIndirectBuffer_);
                const size_t cmdBytes = shadowCommands_.size() * sizeof(DrawCommand);
                if (cmdBytes > shadowIndirectBufferCapacity_) {
                    shadowIndirectBufferCapacity_ = cmdBytes * 2;
                }
                glBufferData(GL_DRAW_INDIRECT_BUFFER,
                             static_cast<GLsizeiptr>(shadowIndirectBufferCapacity_),
                             nullptr, GL_STREAM_DRAW);
                glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                                static_cast<GLsizeiptr>(cmdBytes),
                                shadowCommands_.data());
                frameProfile_.shadowUpload += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tUploadStart).count();

                // Single indirect draw for the entire cascade
                auto tDrawStart = std::chrono::steady_clock::now();
                MegaBuffer::instance().bind();
                glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                            nullptr,
                                            static_cast<GLsizei>(shadowCommands_.size()),
                                            0);
                glBindVertexArray(0);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                frameProfile_.shadowDraw += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tDrawStart).count();
            }
            checkGLError("After shadow draw calls");

            if (logShadows) {
                std::cout << "    Drew " << shadowCommands_.size() << " indirect commands, "
                          << triangleCount << " triangles\n";
            }
        }

        if (!simpleLayerShadowCasters_.empty()) {
            auto tSLStart = std::chrono::steady_clock::now();
            auto simpleLayerShadowShader = shaderManager->GetShader("simplelayer_shadow");
            auto simpleLayerDepthShader  = shaderManager->GetShader("simplelayer_depth");
            if (!simpleLayerShadowShader || !simpleLayerDepthShader) {
                NC::LOGGING::Error("[SHADOW] simplelayer shadow/depth shader variants missing");
            } else {
                // Fix #4: batch simpleLayer shadow draws with instanced indirect rendering.
                // Non-alphaTest (depth-only): group by geomKey, one glMultiDrawElementsIndirect.
                // AlphaTest: group by (tex[0], cullMode), one instanced draw per unique group.

                // ─── non-alphaTest depth pass ───────────────────────────────────────────
                static thread_local std::unordered_map<uint64_t, ShadowInstancedGroup> slDepthGroups;
                slDepthGroups.clear();
                static thread_local std::vector<glm::mat4>   slDepthMats;
                static thread_local std::vector<DrawCommand> slDepthCmds;
                slDepthMats.clear();
                slDepthCmds.clear();

                // alphaTest groups keyed by (tex[0] ptr ^ cullMode bits)
                struct SlAlphaGroup {
                    NDEVC::Graphics::ITexture* tex0 = nullptr;
                    int cullMode = 0;
                    std::vector<glm::mat4>   mats;
                    std::vector<DrawCommand> cmds;
                    std::unordered_map<uint64_t, ShadowInstancedGroup> geomGroups;
                };
                static thread_local std::unordered_map<uint64_t, SlAlphaGroup> slAlphaGroups;
                slAlphaGroups.clear();

                for (const auto& caster : simpleLayerShadowCasters_) {
                    const DrawCmd& obj = *caster.draw;
                    if (obj.disabled) continue;
                    if (caster.viewDepth + caster.radius < cascadeNear) continue;
                    if (caster.viewDepth - caster.radius > cascadeFar) continue;
                    if (!obj.mesh) continue;

                    auto addToGeomGroup = [&](std::unordered_map<uint64_t, ShadowInstancedGroup>& groups,
                                              const Nvx2Group& g) {
                        if (g.indexCount() == 0) return;
                        const uint32_t count      = g.indexCount();
                        const uint32_t firstIndex = obj.megaIndexOffset + g.firstIndex();
                        const uint64_t geomKey    = (static_cast<uint64_t>(count) << 32) | firstIndex;
                        auto& grp = groups[geomKey];
                        if (grp.count == 0) { grp.count = count; grp.firstIndex = firstIndex; }
                        grp.matrices.push_back(obj.worldMatrix);
                    };

                    if (!obj.alphaTest) {
                        if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
                            addToGeomGroup(slDepthGroups, obj.mesh->groups[obj.group]);
                        } else {
                            for (const auto& g : obj.mesh->groups) addToGeomGroup(slDepthGroups, g);
                        }
                    } else {
                        const uint64_t ak = (reinterpret_cast<uintptr_t>(obj.tex[0]) * 0x9e3779b97f4a7c15ull)
                                          ^ static_cast<uint64_t>(static_cast<uint32_t>(obj.cullMode));
                        auto& ag = slAlphaGroups[ak];
                        ag.tex0     = obj.tex[0];
                        ag.cullMode = obj.cullMode;
                        if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
                            addToGeomGroup(ag.geomGroups, obj.mesh->groups[obj.group]);
                        } else {
                            for (const auto& g : obj.mesh->groups) addToGeomGroup(ag.geomGroups, g);
                        }
                    }
                }

                // Build flat arrays for depth pass
                for (auto& [key, grp] : slDepthGroups) {
                    (void)key;
                    if (grp.matrices.empty()) continue;
                    DrawCommand cmd{};
                    cmd.count         = grp.count;
                    cmd.instanceCount = static_cast<uint32_t>(grp.matrices.size());
                    cmd.firstIndex    = grp.firstIndex;
                    cmd.baseVertex    = 0;
                    cmd.baseInstance  = static_cast<uint32_t>(slDepthMats.size());
                    slDepthMats.insert(slDepthMats.end(), grp.matrices.begin(), grp.matrices.end());
                    slDepthCmds.push_back(cmd);
                }

                MegaBuffer::instance().bind();

                // Draw depth-only batch
                if (!slDepthCmds.empty()) {
                    if (!slShadowMatrixSSBO_) glGenBuffers(1, slShadowMatrixSSBO_.put());
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, slShadowMatrixSSBO_);
                    const size_t mb = slDepthMats.size() * sizeof(glm::mat4);
                    if (mb > slShadowMatrixSSBOCapacity_) {
                        slShadowMatrixSSBOCapacity_ = mb * 2;
                    }
                    glBufferData(GL_SHADER_STORAGE_BUFFER,
                                 static_cast<GLsizeiptr>(slShadowMatrixSSBOCapacity_),
                                 nullptr, GL_STREAM_DRAW);
                    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                    static_cast<GLsizeiptr>(mb), slDepthMats.data());
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slShadowMatrixSSBO_);

                    if (!slShadowIndirectBuffer_) glGenBuffers(1, slShadowIndirectBuffer_.put());
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, slShadowIndirectBuffer_);
                    const size_t cb = slDepthCmds.size() * sizeof(DrawCommand);
                    if (cb > slShadowIndirectBufferCapacity_) {
                        slShadowIndirectBufferCapacity_ = cb * 2;
                    }
                    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                                 static_cast<GLsizeiptr>(slShadowIndirectBufferCapacity_),
                                 nullptr, GL_STREAM_DRAW);
                    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                                    static_cast<GLsizeiptr>(cb), slDepthCmds.data());

                    glDisable(GL_CULL_FACE);
                    simpleLayerDepthShader->Use();
                    simpleLayerDepthShader->SetInt("UseInstancing", 1);
                    simpleLayerDepthShader->SetMat4("lightSpaceMatrix", lightSpaceMatrices[cascade]);

                    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                                nullptr,
                                                static_cast<GLsizei>(slDepthCmds.size()), 0);
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                }

                // Draw alphaTest batches (one per unique tex0/cullMode group)
                if (!slAlphaGroups.empty()) {
                    // Reuse the slShadowMatrixSSBO_ for each alphaTest group sequentially
                    simpleLayerShadowShader->Use();
                    simpleLayerShadowShader->SetInt("UseInstancing", 1);
                    simpleLayerShadowShader->SetMat4("lightSpaceMatrix", lightSpaceMatrices[cascade]);
                    simpleLayerShadowShader->SetInt("diffMapSampler", 0);

                    static thread_local std::vector<glm::mat4>   agMats;
                    static thread_local std::vector<DrawCommand> agCmds;

                    for (auto& [key, ag] : slAlphaGroups) {
                        (void)key;
                        agMats.clear();
                        agCmds.clear();

                        for (auto& [gk, grp] : ag.geomGroups) {
                            (void)gk;
                            if (grp.matrices.empty()) continue;
                            DrawCommand cmd{};
                            cmd.count         = grp.count;
                            cmd.instanceCount = static_cast<uint32_t>(grp.matrices.size());
                            cmd.firstIndex    = grp.firstIndex;
                            cmd.baseVertex    = 0;
                            cmd.baseInstance  = static_cast<uint32_t>(agMats.size());
                            agMats.insert(agMats.end(), grp.matrices.begin(), grp.matrices.end());
                            agCmds.push_back(cmd);
                        }
                        if (agCmds.empty()) continue;

                        if (ag.cullMode <= 0) glDisable(GL_CULL_FACE);
                        else { glEnable(GL_CULL_FACE); glCullFace(ag.cullMode == 1 ? GL_FRONT : GL_BACK); }

                        bindTexture(0, ag.tex0 ? toTextureHandle(ag.tex0) : whiteTex);
                        bindSampler(0, gSamplerRepeat);

                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, slShadowMatrixSSBO_);
                        const size_t mb = agMats.size() * sizeof(glm::mat4);
                        if (mb > slShadowMatrixSSBOCapacity_) {
                            slShadowMatrixSSBOCapacity_ = mb * 2;
                        }
                        glBufferData(GL_SHADER_STORAGE_BUFFER,
                                     static_cast<GLsizeiptr>(slShadowMatrixSSBOCapacity_),
                                     nullptr, GL_STREAM_DRAW);
                        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                        static_cast<GLsizeiptr>(mb), agMats.data());
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slShadowMatrixSSBO_);

                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, slShadowIndirectBuffer_);
                        const size_t cb = agCmds.size() * sizeof(DrawCommand);
                        if (cb > slShadowIndirectBufferCapacity_) {
                            slShadowIndirectBufferCapacity_ = cb * 2;
                        }
                        glBufferData(GL_DRAW_INDIRECT_BUFFER,
                                     static_cast<GLsizeiptr>(slShadowIndirectBufferCapacity_),
                                     nullptr, GL_STREAM_DRAW);
                        glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                                        static_cast<GLsizeiptr>(cb), agCmds.data());

                        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                                    nullptr,
                                                    static_cast<GLsizei>(agCmds.size()), 0);
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                    }
                }

                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
                bindSampler(0, 0);
                glDisable(GL_CULL_FACE);

                if (logShadows) {
                    std::cout << "    SimpleLayer shadow depth cmds: " << slDepthCmds.size()
                              << " alphaTest groups: " << slAlphaGroups.size() << "\n";
                }
            }
            frameProfile_.shadowSL += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - tSLStart).count();
        }
    }

    // Update shadow group cache for next frame if any cascade computed new grouping
    shadowGroupCacheViewMatrix_ = viewMatrix;
    shadowGroupCacheValid_      = true;

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    checkGLError("After shadow pass cleanup");

    if (logShadows) std::cout << "  Shadow rendering complete, viewport restored\n";
}


void DeferredRenderer::Initialize() {
    const bool traceInit = ReadEnvToggle("NDEVC_INIT_TRACE");
    NC::LOGGING::Log("[DEFERRED] Initialize begin trace=", (traceInit ? 1 : 0));
    auto logInitStep = [&](const char* step) {
        NC::LOGGING::Log("[DEFERRED][INIT] ", (step ? step : "<null>"));
        if (traceInit && step) {
            std::cerr << "[INIT TRACE] " << step << "\n";
        }
        if (window_) {
            window_->PollEvents();
        }
    };

    shutdownComplete_ = false;
    viewportDisabled_ = ReadEnvToggle("NDEVC_DISABLE_VIEWPORT");
    logInitStep("start");
    MountPackageFromEnvironment();
    logInitStep("MountPackageFromEnvironment");
    InstallNax3Provider();
    logInitStep("InstallNax3Provider");
    initGLFW();
    logInitStep("initGLFW");
    TextureServer::sBindlessSupported = (GLAD_GL_ARB_bindless_texture != 0);
    NC::LOGGING::Log("[DEFERRED][INIT] GL_ARB_bindless_texture=",
        TextureServer::sBindlessSupported ? "YES" : "NO");
    initCascadedShadowMaps();
    logInitStep("initCascadedShadowMaps");
    InitScreenQuad();
    logInitStep("InitScreenQuad");
    InitShadowSampler();
    logInitStep("InitShadowSampler");
    generateSphereMesh();
    logInitStep("generateSphereMesh");
    ClearDisabledDraws();
    logInitStep("ClearDisabledDraws");
    scene_.Initialize(device_.get(), shaderManager.get());
    logInitStep("SceneManager::Initialize");

    if (!viewportDisabled_) {
        const std::string startupMapPath = ResolveStartupMapPath();
        if (startupMapPath.empty()) {
            NC::LOGGING::Log("[DEFERRED][INIT] Starting with empty scene (no startup map)");
        } else if (optRenderLOG) {
            std::cout << "[Init] Startup map: " << startupMapPath << "\n";
        }
        NC::LOGGING::Log("[DEFERRED][INIT] StartupMapPath=", startupMapPath);

        MapLoader loader;
        std::unique_ptr<MapData> startupMapOwned;
        MapData* startupMap = nullptr;
        if (!startupMapPath.empty()) {
            startupMapOwned = loader.load_map(startupMapPath);
            startupMap = startupMapOwned.get();
        }
        logInitStep("MapLoader::load_map");
        if (!startupMap) {
            if (!startupMapPath.empty()) {
                NC::LOGGING::Error("[DEFERRED][INIT] map load FAILED: ", startupMapPath);
            }
        } else {
            if (optRenderLOG) std::cout << "Map loaded: " << startupMap->instances.size() << " instances\n";
            NC::LOGGING::Log("[DEFERRED][INIT] Map loaded instances=", startupMap->instances.size());
            scene_.LoadMap(startupMap, startupMapPath);
            logInitStep("SceneManager::LoadMap");
        }

        MeshServer::instance().buildMegaBuffer();
        logInitStep("buildMegaBuffer");

        scene_.PrepareDrawLists(camera_,
            solidDraws, alphaTestDraws, decalDraws, particleDraws,
            environmentDraws, environmentAlphaDraws, simpleLayerDraws,
            refractionDraws, postAlphaUnlitDraws, waterDraws, animatedDraws);
        logInitStep("SceneManager::PrepareDrawLists");
        ApplyDisabledDrawFlags();
        logInitStep("ApplyDisabledDrawFlags");

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

        solidBatchSystem_.init(solidDraws);
        alphaTestBatchSystem_.init(alphaTestDraws);
        environmentBatchSystem_.init(environmentDraws);
        environmentAlphaBatchSystem_.init(environmentAlphaDraws);
        decalBatchSystem_.init(decalDraws);
        logInitStep("DrawBatchSystem::init");

        if (TextureServer::sBindlessSupported) {
            NC::LOGGING::Log("[DEFERRED][INIT] BindlessTextures resident=",
                TextureServer::instance().residentTextureCount());
        }

        buildMaterialSSBO();
        logInitStep("buildMaterialSSBO");

        buildDecalMaterialSSBO();
        logInitStep("buildDecalMaterialSSBO");

        buildWaterMaterialSSBO();
        logInitStep("buildWaterMaterialSSBO");

        buildRefractionMaterialSSBO();
        logInitStep("buildRefractionMaterialSSBO");

        buildEnvAlphaMaterialSSBO();
        logInitStep("buildEnvAlphaMaterialSSBO");

        BuildVisibilityGrids();
        logInitStep("BuildVisibilityGrids");

        // Center the top-down camera above the map.
        if (startupMap) {
            const MapInfo& info = startupMap->info;
            const float halfDiag = glm::length(glm::vec2(info.extents.x, info.extents.z));
            const float elevation = std::max(halfDiag * 1.2f, 200.0f);
            camera_.setPosition(glm::vec3(info.center.x,
                                          info.center.y + elevation,
                                          info.center.z + elevation * 0.7f));
        } else {
            camera_.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
            camera_.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        }

        rebuildAnimatedDrawLists();
        logInitStep("rebuildAnimatedDrawLists");
        decalBatchDirty = true;
    }
    scenePrepared = true;
    useLegacyDeferredInit_ = ReadEnvToggle("NDEVC_USE_LEGACY_DEFERRED_INIT");
    NC::LOGGING::Log("[DEFERRED][INIT] LegacyDeferredInit=", (useLegacyDeferredInit_ ? 1 : 0));

    if (useLegacyDeferredInit_) {
        initDeferred();
        logInitStep("initDeferred");
    } else {
        setupRenderPasses();
        logInitStep("setupRenderPasses");
    }
    WriteWebSnapshot("initialize");
    logInitStep("WriteWebSnapshot");

    // ── Initial Work/Play mode policy ────────────────────────────────────
    {
        RenderMode initialMode = RenderMode::Work;
#if defined(_WIN32)
        char* modeVal = nullptr;
        size_t modeLen = 0;
        if (_dupenv_s(&modeVal, &modeLen, "NDEVC_RENDER_MODE") == 0 && modeVal) {
            if (std::string(modeVal) == "play" || std::string(modeVal) == "Play") {
                initialMode = RenderMode::Play;
            }
            std::free(modeVal);
        }
#else
        const char* modeVal = std::getenv("NDEVC_RENDER_MODE");
        if (modeVal && (std::string(modeVal) == "play" || std::string(modeVal) == "Play")) {
            initialMode = RenderMode::Play;
        }
#endif
        renderMode_ = initialMode;
        FramePolicy policy = BuildPolicy(initialMode);
        // Preserve ImGui init-time editor config — docking/theme can't change post-init.
        policy.editorLayoutEnabled = editorModeEnabled_;
        policy.viewportOnlyUI      = imguiViewportOnly_;
        ApplyPolicy(policy);
        LogPolicySnapshot("STARTUP");
    }
    logInitStep("ApplyPolicy");

    NC::LOGGING::Log("[DEFERRED] Initialize end");
}

void DeferredRenderer::buildMaterialSSBO() {
    if (!TextureServer::sBindlessSupported) return;

    const size_t solidCount = solidDraws.size();
    const size_t alphaCount = alphaTestDraws.size();
    const size_t totalCount = solidCount + alphaCount;
    if (totalCount == 0) return;

    auto texHandle = [](const NDEVC::Graphics::ITexture* t, uint64_t fallback) -> uint64_t {
        if (!t) return fallback;
        uint64_t h = t->GetBindlessHandle();
        return h ? h : fallback;
    };

    std::vector<MaterialGPU> materials(totalCount);

    auto fillMaterial = [&](MaterialGPU& m, const DrawCmd& dc) {
        m.diffuseHandle  = texHandle(dc.tex[0], fallbackWhiteHandle_);
        m.specularHandle = texHandle(dc.tex[1], fallbackBlackHandle_);
        m.normalHandle   = texHandle(dc.tex[2], fallbackNormalHandle_);
        m.emissiveHandle = texHandle(dc.tex[3], fallbackBlackHandle_);
        m.emissiveIntensity = dc.cachedMatEmissiveIntensity;
        m.specularIntensity = dc.cachedMatSpecularIntensity;
        m.specularPower     = dc.cachedMatSpecularPower;
        m.alphaCutoff       = dc.alphaCutoff;
        m.flags = 0;
        if (dc.alphaTest)       m.flags |= MATFLAG_ALPHA_TEST;
        if (dc.cachedTwoSided)  m.flags |= MATFLAG_TWO_SIDED;
        if (dc.cachedIsFlatNormal) m.flags |= MATFLAG_FLAT_NORMAL;
        if (dc.receivesDecals)  m.flags |= MATFLAG_RECEIVES_DECALS;
        if (dc.cachedIsAdditive) m.flags |= MATFLAG_ADDITIVE;
        if (dc.cachedHasSpecMap) m.flags |= MATFLAG_HAS_SPEC_MAP;
        m.bumpScale         = dc.cachedBumpScale;
        m.intensity0        = dc.cachedIntensity0;
        m.alphaBlendFactor  = dc.cachedAlphaBlendFactor;
        m.diffMap1Handle = texHandle(dc.tex[4], 0);
        m.specMap1Handle = texHandle(dc.tex[5], 0);
        m.bumpMap1Handle = texHandle(dc.tex[6], 0);
        m.maskMapHandle  = texHandle(dc.tex[7], 0);
        m.alphaMapHandle = texHandle(dc.tex[8], 0);
        m.cubeMapHandle  = texHandle(dc.tex[9], fallbackBlackCubeHandle_);
        m.velocityX = dc.cachedVelocity.x;
        m.velocityY = dc.cachedVelocity.y;
        m.scale     = dc.cachedScale;
        m.pad0      = 0.0f;
    };

    for (size_t i = 0; i < solidCount; ++i) {
        solidDraws[i].gpuMaterialIndex = static_cast<uint32_t>(i);
        fillMaterial(materials[i], solidDraws[i]);
    }
    for (size_t i = 0; i < alphaCount; ++i) {
        const uint32_t idx = static_cast<uint32_t>(solidCount + i);
        alphaTestDraws[i].gpuMaterialIndex = idx;
        fillMaterial(materials[idx], alphaTestDraws[i]);
    }

    const size_t bytes = totalCount * sizeof(MaterialGPU);
    if (!materialSSBO_.valid()) glGenBuffers(1, materialSSBO_.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO_);
    if (bytes > materialSSBOCapacity_) {
        materialSSBOCapacity_ = bytes;
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(materialSSBOCapacity_),
            nullptr, GL_STATIC_DRAW);
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(bytes), materials.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    materialSSBOCount_ = totalCount;
    materialSSBODirty_ = false;

    NC::LOGGING::Log("[DEFERRED] MaterialSSBO built count=", totalCount,
        " solid=", solidCount, " alpha=", alphaCount,
        " bytes=", bytes);
}

void DeferredRenderer::buildDecalMaterialSSBO() {
    if (!TextureServer::sBindlessSupported) return;
    if (decalDraws.empty()) return;

    auto texHandle = [](const NDEVC::Graphics::ITexture* t, uint64_t fallback) -> uint64_t {
        if (!t) return fallback;
        uint64_t h = t->GetBindlessHandle();
        return h ? h : fallback;
    };

    const size_t count = decalDraws.size();
    std::vector<DecalMaterialGPU> materials(count);
    for (size_t i = 0; i < count; ++i) {
        DrawCmd& dc = decalDraws[i];
        DecalMaterialGPU& m = materials[i];
        m.diffuseHandle  = texHandle(dc.tex[0], fallbackWhiteHandle_);
        m.emissiveHandle = texHandle(dc.tex[3], fallbackBlackHandle_);
        m.decalScale = dc.decalScale;
        auto it = dc.shaderParamsInt.find("DecalDiffuseMode");
        m.decalDiffuseMode = (it != dc.shaderParamsInt.end()) ? static_cast<uint32_t>(it->second) : 0u;
        m.pad0 = 0.0f;
        m.pad1 = 0.0f;
        dc.gpuMaterialIndex = static_cast<uint32_t>(i);
    }

    const size_t bytes = count * sizeof(DecalMaterialGPU);
    if (!decalMaterialSSBO_.valid()) glGenBuffers(1, decalMaterialSSBO_.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, decalMaterialSSBO_);
    if (bytes > decalMaterialSSBOCapacity_) {
        decalMaterialSSBOCapacity_ = bytes;
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(decalMaterialSSBOCapacity_),
            nullptr, GL_STATIC_DRAW);
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(bytes), materials.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    decalMaterialSSBOCount_ = count;

    NC::LOGGING::Log("[DEFERRED] DecalMaterialSSBO built count=", count, " bytes=", bytes);
}

void DeferredRenderer::buildWaterMaterialSSBO() {
    if (!TextureServer::sBindlessSupported) return;
    if (waterDraws.empty()) return;

    auto texHandle = [](const NDEVC::Graphics::ITexture* t, uint64_t fallback) -> uint64_t {
        if (!t) return fallback;
        uint64_t h = t->GetBindlessHandle();
        return h ? h : fallback;
    };

    const size_t count = waterDraws.size();
    std::vector<WaterMaterialGPU> materials(count);
    for (size_t i = 0; i < count; ++i) {
        DrawCmd& dc = waterDraws[i];
        WaterMaterialGPU& m = materials[i];
        m.diffuseHandle     = texHandle(dc.tex[0], fallbackWhiteHandle_);
        m.bumpHandle        = texHandle(dc.tex[2], fallbackNormalHandle_);
        m.emissiveHandle    = texHandle(dc.tex[3], fallbackBlackHandle_);
        m.cubeHandle        = texHandle(dc.tex[9], fallbackBlackCubeHandle_);
        m.intensity0        = dc.cachedIntensity0;
        m.emissiveIntensity = dc.cachedMatEmissiveIntensity;
        m.specularIntensity = dc.cachedMatSpecularIntensity;
        m.bumpScale         = dc.cachedBumpScale;
        float uvs = dc.cachedScale;
        if (uvs <= 0.0f) uvs = 1.0f;
        m.uvScale           = uvs;
        m.velocityX         = dc.cachedVelocity.x;
        m.velocityY         = dc.cachedVelocity.y;
        m.flags = 0;
        if (dc.cachedHasVelocity) m.flags |= WATER_FLAG_HAS_VELOCITY;
        dc.gpuMaterialIndex = static_cast<uint32_t>(i);
    }

    const size_t bytes = count * sizeof(WaterMaterialGPU);
    if (!waterMaterialSSBO_.valid()) glGenBuffers(1, waterMaterialSSBO_.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, waterMaterialSSBO_);
    if (bytes > waterMaterialSSBOCapacity_) {
        waterMaterialSSBOCapacity_ = bytes;
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(waterMaterialSSBOCapacity_),
            nullptr, GL_STATIC_DRAW);
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(bytes), materials.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    waterMaterialSSBOCount_ = count;
    NC::LOGGING::Log("[DEFERRED] WaterMaterialSSBO built count=", count, " bytes=", bytes);
}

void DeferredRenderer::buildRefractionMaterialSSBO() {
    if (!TextureServer::sBindlessSupported) return;
    if (refractionDraws.empty()) return;

    auto texHandle = [](const NDEVC::Graphics::ITexture* t, uint64_t fallback) -> uint64_t {
        if (!t) return fallback;
        uint64_t h = t->GetBindlessHandle();
        return h ? h : fallback;
    };

    const size_t count = refractionDraws.size();
    std::vector<RefractionMaterialGPU> materials(count);
    for (size_t i = 0; i < count; ++i) {
        DrawCmd& dc = refractionDraws[i];
        RefractionMaterialGPU& m = materials[i];
        m.distortHandle = texHandle(dc.tex[0], fallbackWhiteHandle_);
        m.velocityX = dc.cachedVelocity.x;
        m.velocityY = dc.cachedVelocity.y;
        float distScale = dc.cachedIntensity0;
        auto it = dc.shaderParamsFloat.find("distortionScale");
        if (it != dc.shaderParamsFloat.end()) distScale = it->second;
        m.distortionScale = distScale;
        m.pad0 = 0.0f;
        m.pad1 = 0.0f;
        m.pad2 = 0.0f;
        dc.gpuMaterialIndex = static_cast<uint32_t>(i);
    }

    const size_t bytes = count * sizeof(RefractionMaterialGPU);
    if (!refractionMaterialSSBO_.valid()) glGenBuffers(1, refractionMaterialSSBO_.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, refractionMaterialSSBO_);
    if (bytes > refractionMaterialSSBOCapacity_) {
        refractionMaterialSSBOCapacity_ = bytes;
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(refractionMaterialSSBOCapacity_),
            nullptr, GL_STATIC_DRAW);
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(bytes), materials.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    refractionMaterialSSBOCount_ = count;
    NC::LOGGING::Log("[DEFERRED] RefractionMaterialSSBO built count=", count, " bytes=", bytes);
}

void DeferredRenderer::buildEnvAlphaMaterialSSBO() {
    if (!TextureServer::sBindlessSupported) return;
    if (environmentAlphaDraws.empty()) return;

    auto texHandle = [](const NDEVC::Graphics::ITexture* t, uint64_t fallback) -> uint64_t {
        if (!t) return fallback;
        uint64_t h = t->GetBindlessHandle();
        return h ? h : fallback;
    };

    const size_t count = environmentAlphaDraws.size();
    std::vector<EnvAlphaMaterialGPU> materials(count);
    for (size_t i = 0; i < count; ++i) {
        DrawCmd& dc = environmentAlphaDraws[i];
        EnvAlphaMaterialGPU& m = materials[i];
        m.diffuseHandle     = texHandle(dc.tex[0], fallbackWhiteHandle_);
        m.specHandle        = texHandle(dc.tex[1], fallbackWhiteHandle_);
        m.bumpHandle        = texHandle(dc.tex[2], fallbackNormalHandle_);
        m.emsvHandle        = texHandle(dc.tex[3], fallbackBlackHandle_);
        m.envCubeHandle     = texHandle(dc.tex[9], fallbackBlackCubeHandle_);
        m.reflectivity      = dc.cachedIntensity0;
        m.specularIntensity = dc.cachedMatSpecularIntensity;
        m.alphaBlendFactor  = dc.cachedAlphaBlendFactor;
        m.flags = 0;
        if (dc.cachedTwoSided)    m.flags |= ENVALPHA_FLAG_TWO_SIDED;
        if (dc.cachedIsFlatNormal) m.flags |= ENVALPHA_FLAG_FLAT_NORMAL;
        m.pad0 = 0.0f;
        m.pad1 = 0.0f;
        dc.gpuMaterialIndex = static_cast<uint32_t>(i);
    }

    const size_t bytes = count * sizeof(EnvAlphaMaterialGPU);
    if (!envAlphaMaterialSSBO_.valid()) glGenBuffers(1, envAlphaMaterialSSBO_.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, envAlphaMaterialSSBO_);
    if (bytes > envAlphaMaterialSSBOCapacity_) {
        envAlphaMaterialSSBOCapacity_ = bytes;
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(envAlphaMaterialSSBOCapacity_),
            nullptr, GL_STATIC_DRAW);
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(bytes), materials.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    envAlphaMaterialSSBOCount_ = count;
    NC::LOGGING::Log("[DEFERRED] EnvAlphaMaterialSSBO built count=", count, " bytes=", bytes);
}

void DeferredRenderer::Shutdown() {
    NC::LOGGING::Log("[DEFERRED] Shutdown begin");
    if (shutdownComplete_) return;
    shutdownComplete_ = true;

    if (window_) {
        window_->MakeCurrent();
    }

    ShutdownImGui();
    ClearDisabledDraws();
    Particles::ParticleServer::Instance().Close();

    solidDraws.clear();
    decalDraws.clear();
    alphaTestDraws.clear();
    simpleLayerDraws.clear();
    environmentDraws.clear();
    environmentAlphaDraws.clear();
    refractionDraws.clear();
    waterDraws.clear();
    postAlphaUnlitDraws.clear();
    particleDraws.clear();

    scene_.Clear();
    animatedDraws.clear();
    pointLights.clear();
    gClips.clear();
    gAnimPose.clear();
    ClearAnimationOwnerData();
    selectedObject = nullptr;
    selectedIndex = -1;
    cachedObj = DrawCmd{};
    cachedIndex = -1;
    visibleCells_.clear();
    lastVisibleCells_.clear();
    visibilityStage_.Reset();
    visibilityStageFrameIndex_ = 0;
    scenePrepared = false;

    shadowGraph.reset();
    geometryGraph.reset();
    decalGraph.reset();
    lightingGraph.reset();
    particleGraph.reset();
    solidBatchSystem_.reset(true);
    alphaTestBatchSystem_.reset(true);
    environmentBatchSystem_.reset(true);
    environmentAlphaBatchSystem_.reset(true);
    decalBatchSystem_.reset(true);

    bool hasGLContext = false;
    if (window_) {
        window_->MakeCurrent();
        hasGLContext = true;
    } else if (glfwGetCurrentContext() != nullptr) {
        hasGLContext = true;
    }

    if (hasGLContext) {
        // Clean up frame fence sync objects
        for (int i = 0; i < kMaxFramesInFlight + 1; ++i) {
            if (frameFences_[i]) { glDeleteSync(frameFences_[i]); frameFences_[i] = nullptr; }
        }
        solidBatchSystem_.shutdownGL();
        alphaTestBatchSystem_.shutdownGL();
        environmentBatchSystem_.shutdownGL();
        environmentAlphaBatchSystem_.shutdownGL();
        decalBatchSystem_.shutdownGL();
        if (fallbackWhiteHandle_) { glMakeTextureHandleNonResidentARB(fallbackWhiteHandle_); fallbackWhiteHandle_ = 0; }
        if (fallbackBlackHandle_) { glMakeTextureHandleNonResidentARB(fallbackBlackHandle_); fallbackBlackHandle_ = 0; }
        if (fallbackNormalHandle_) { glMakeTextureHandleNonResidentARB(fallbackNormalHandle_); fallbackNormalHandle_ = 0; }
        if (fallbackBlackCubeHandle_) { glMakeTextureHandleNonResidentARB(fallbackBlackCubeHandle_); fallbackBlackCubeHandle_ = 0; }
        materialSSBO_.reset();
        materialSSBOCapacity_ = 0;
        materialSSBOCount_ = 0;
        TextureServer::instance().clearCache(true);
        releaseOwnedGLResources();
    } else {
        const bool hasLeakedGLState =
            solidBatchSystem_.hasGLResources() ||
            alphaTestBatchSystem_.hasGLResources() ||
            environmentBatchSystem_.hasGLResources() ||
            environmentAlphaBatchSystem_.hasGLResources() ||
            decalBatchSystem_.hasGLResources() ||
            MegaBuffer::instance().hasGLResources() ||
            TextureServer::instance().hasCachedTextures() ||
            shadowMatrixSSBO_ || shadowIndirectBuffer_ || pointLightInstanceVBO ||
            materialSSBO_.valid() ||
            quadVAO || quadVBO || sphereVAO || debugCellVAO_ || debugCellVBO_ ||
            debugLineProgram_ || gDepthCopyReadFBO || gDepthDecalFBO ||
            gBuffer || lightFBO || sceneFBO || sceneFBO2 ||
            gPositionVS || gPositionWS || gPosition || gNormalDepthPacked ||
            gAlbedoSpec || gDepth || gEmissive || gPositionWSRead ||
            shadowFBOCascades[0] || shadowFBOCascades[1];
        if (hasLeakedGLState) {
            NC::LOGGING::Error("[DEFERRED][RAII] Shutdown without active GL context while GL resources are still live");
        }
        TextureServer::instance().clearCache(false);
    }
    MeshServer::instance().clearCache();
    ModelServer::instance().clearCache();

    for (auto& tex : shadowMapTextures) tex.reset();
    whiteTex_abstracted.reset();
    blackTex_abstracted.reset();
    normalTex_abstracted.reset();
    blackCubeTex_abstracted.reset();
    lightBufferTex.reset();
    sceneColorTex.reset();
    sceneDepthStencilTex.reset();
    sceneColor2Tex.reset();

    samplerRepeat_abstracted.reset();
    samplerClamp_abstracted.reset();
    samplerShadow_abstracted.reset();
    sphereVBO_abstracted.reset();
    sphereEBO_abstracted.reset();

    whiteTex.id = 0;
    blackTex.id = 0;
    normalTex.id = 0;
    blackCubeTex.id = 0;
    gSamplerRepeat.id = 0;
    gSamplerClamp.id = 0;
    gPositionVS.id = 0;
    gPositionWS.id = 0;
    gPosition.id = 0;
    gNormalDepthPacked.id = 0;
    gAlbedoSpec.id = 0;
    gDepth.id = 0;
    gEmissive.id = 0;
    gDepthDecal.id = 0;
    sceneDepthStencil.id = 0;
    sceneColor.id = 0;
    sceneColor2.id = 0;
    lightBuffer.id = 0;

    if (shaderManager) shaderManager->Shutdown();
    shaderManager.reset();

    device_.reset();

    window_.reset();
    inputSystem_.reset();
    platform_.reset();
    NC::LOGGING::Log("[DEFERRED] Shutdown end");
}

void DeferredRenderer::RenderFrame() {
    NC::LOGGING::Log("[DEFERRED] RenderFrame loop begin");
    while (!ShouldClose()) {
        PollEvents();
        RenderSingleFrame();
    }
    NC::LOGGING::Log("[DEFERRED] RenderFrame loop end");
}

void DeferredRenderer::PollEvents() {
    if (window_) {
        window_->PollEvents();
    }
    HandleDroppedPaths();
    if (inputSystem_) {
        inputSystem_->Update();
    }
}

void DeferredRenderer::HandleDroppedPaths() {
    if (pendingDroppedPaths_.empty()) {
        return;
    }

    std::vector<std::string> droppedPaths;
    droppedPaths.swap(pendingDroppedPaths_);

    std::vector<std::string> shaderDropPaths;
    std::string mapDropPath;
    for (const std::string& path : droppedPaths) {
        if (IsMapFilePath(path)) {
            if (mapDropPath.empty()) {
                mapDropPath = path;
            }
        } else {
            shaderDropPaths.push_back(path);
        }
    }

    if (!shaderDropPaths.empty() && shaderManager) {
        shaderManager->HandleFileDrop(shaderDropPaths);
    }

    if (mapDropPath.empty()) {
        return;
    }

    QueueDroppedMapLoad(mapDropPath);
}

void DeferredRenderer::QueueDroppedMapLoad(const std::string& mapDropPath) {
    mapDropLoadStage_ = MapDropLoadStage::Queued;
    mapDropLoadPath_ = mapDropPath;
    mapDropLoadedMap_.reset();
    mapDropLoadStartSec_ = glfwGetTime();
    mapDropLoadFileSec_ = 0.0;
    mapDropLoadApplySec_ = 0.0;
    mapDropLoadTotalSec_ = 0.0;
    mapDropLoadDisplayUntilSec_ = 0.0;
    mapDropLoadProgress_ = 0.02f;
    mapDropLoadStatus_ = std::string("Loading map: ") + mapDropPath;
    MarkDirty();
}

bool DeferredRenderer::ProcessPendingDroppedMapLoad(double currentFrame) {
    if (mapDropLoadStage_ == MapDropLoadStage::Idle) {
        return false;
    }

    auto presentProgressFrame = [&]() {
        if (window_) {
            RenderImGui();
            window_->SwapBuffers();
        }
        lastFrameDrawCalls_ = 0;
    };

    if (mapDropLoadStage_ == MapDropLoadStage::Queued) {
        mapDropLoadProgress_ = 0.08f;
        mapDropLoadStatus_ = "Preparing map load...";
        mapDropLoadStage_ = MapDropLoadStage::LoadFile;
        presentProgressFrame();
        return true;
    }

    if (mapDropLoadStage_ == MapDropLoadStage::LoadFile) {
        mapDropLoadStatus_ = "Reading map file...";
        const double t0 = glfwGetTime();
        MapLoader loader;
        mapDropLoadedMap_ = loader.load_map(mapDropLoadPath_);
        mapDropLoadFileSec_ = glfwGetTime() - t0;

        if (!mapDropLoadedMap_) {
            mapDropLoadTotalSec_ = glfwGetTime() - mapDropLoadStartSec_;
            mapDropLoadProgress_ = 0.0f;
            char failedMsg[96] = {};
            std::snprintf(failedMsg, sizeof(failedMsg), "Map load failed (%.2fs)", mapDropLoadTotalSec_);
            mapDropLoadStatus_ = failedMsg;
            mapDropLoadDisplayUntilSec_ = currentFrame + 2.5;
            mapDropLoadStage_ = MapDropLoadStage::Failed;
            NC::LOGGING::Error("[SCENE] Dropped map load failed path=", mapDropLoadPath_);
            presentProgressFrame();
            return true;
        }

        mapDropLoadProgress_ = 0.58f;
        mapDropLoadStatus_ = "Building scene data...";
        mapDropLoadStage_ = MapDropLoadStage::ApplyScene;
        presentProgressFrame();
        return true;
    }

    if (mapDropLoadStage_ == MapDropLoadStage::ApplyScene) {
        const double t0 = glfwGetTime();
        const MapData* droppedMapPtr = mapDropLoadedMap_.get();
        scene_.ImportMapAsEditableScene(droppedMapPtr, mapDropLoadPath_);

        ClearDisabledDraws();
        InvalidateSelection();
        visFrustumCacheValid_ = false;
        visGridRevealedAll_ = false;
        visibilityStage_.Reset();
        visibilityStageFrameIndex_ = 0;
        visibleCells_.clear();
        lastVisibleCells_.clear();
        slGBufCacheValid_ = false;
        slViewProjCacheValid_ = false;
        shadowGroupCacheValid_ = false;
        shadowCastersDirty_ = true;

        if (droppedMapPtr && !droppedMapPtr->instances.empty()) {
            glm::vec3 minPos(std::numeric_limits<float>::max());
            glm::vec3 maxPos(std::numeric_limits<float>::lowest());
            for (const auto& inst : droppedMapPtr->instances) {
                minPos.x = std::min(minPos.x, inst.pos.x);
                minPos.y = std::min(minPos.y, inst.pos.y);
                minPos.z = std::min(minPos.z, inst.pos.z);
                maxPos.x = std::max(maxPos.x, inst.pos.x);
                maxPos.y = std::max(maxPos.y, inst.pos.y);
                maxPos.z = std::max(maxPos.z, inst.pos.z);
            }
            const glm::vec3 center = 0.5f * (minPos + maxPos);
            const float halfDiag = 0.5f * glm::length(glm::vec2(maxPos.x - minPos.x, maxPos.z - minPos.z));
            const float elevation = std::max(halfDiag * 1.6f, 200.0f);
            camera_.setPosition(glm::vec3(center.x, center.y + elevation, center.z + elevation * 0.7f));
            camera_.lookAt(center);
        } else if (droppedMapPtr) {
            const MapInfo& info = droppedMapPtr->info;
            const float halfDiag = glm::length(glm::vec2(info.extents.x, info.extents.z));
            const float elevation = std::max(halfDiag * 1.2f, 200.0f);
            const glm::vec3 center(info.center.x, info.center.y, info.center.z);
            camera_.setPosition(glm::vec3(center.x, center.y + elevation, center.z + elevation * 0.7f));
            camera_.lookAt(center);
        }

        mapDropLoadApplySec_ = glfwGetTime() - t0;
        mapDropLoadTotalSec_ = glfwGetTime() - mapDropLoadStartSec_;
        mapDropLoadProgress_ = 1.0f;
        char loadedMsg[128] = {};
        std::snprintf(loadedMsg, sizeof(loadedMsg), "Map loaded %.2fs (file %.2fs + scene %.2fs)",
                      mapDropLoadTotalSec_, mapDropLoadFileSec_, mapDropLoadApplySec_);
        mapDropLoadStatus_ = loadedMsg;
        mapDropLoadDisplayUntilSec_ = currentFrame + 2.0;
        mapDropLoadStage_ = MapDropLoadStage::Complete;
        MarkDirty();
        NC::LOGGING::Log("[SCENE] Dropped map loaded path=", mapDropLoadPath_,
                         " instances=", droppedMapPtr ? droppedMapPtr->instances.size() : 0,
                         " loadSec=", mapDropLoadTotalSec_,
                         " fileSec=", mapDropLoadFileSec_,
                         " applySec=", mapDropLoadApplySec_);
        mapDropLoadedMap_.reset();
        presentProgressFrame();
        return true;
    }

    if (mapDropLoadStage_ == MapDropLoadStage::Complete ||
        mapDropLoadStage_ == MapDropLoadStage::Failed) {
        if (currentFrame >= mapDropLoadDisplayUntilSec_) {
            mapDropLoadStage_ = MapDropLoadStage::Idle;
            mapDropLoadPath_.clear();
            mapDropLoadStatus_.clear();
            mapDropLoadProgress_ = 0.0f;
            return false;
        }
        presentProgressFrame();
        return true;
    }

    return false;
}

void DeferredRenderer::RenderSingleFrame() {
    if (!window_ || window_->ShouldClose()) {
        NC::LOGGING::Warning("[DEFERRED] RenderSingleFrame skipped window=", (window_ ? 1 : 0));
        return;
    }
    renderSingleFrame();
}

bool DeferredRenderer::ShouldClose() const {
    return !window_ || window_->ShouldClose();
}

void DeferredRenderer::Resize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    NC::LOGGING::Log("[DEFERRED] Resize old=", width, "x", height, " new=", newWidth, "x", newHeight);
    width = newWidth;
    height = newHeight;
    resizeFramebuffers(newWidth, newHeight);
}

void DeferredRenderer::AppendModel(const std::string& path, const glm::vec3& pos,
    const glm::quat& rot, const glm::vec3& scale) {
    scene_.AppendModel(path, pos, rot, scale);
}

void DeferredRenderer::LoadMap(const MapData* map) {
    NC::LOGGING::Log("[DEFERRED] LoadMap ptr=", (map ? 1 : 0));
    scene_.LoadMap(map);
}

void DeferredRenderer::ReloadMapWithCurrentMode() {
    NC::LOGGING::Log("[DEFERRED] ReloadMapWithCurrentMode");
    scene_.ReloadMap();
}

void DeferredRenderer::SetCheckGLErrors(bool enabled) {
    NC::LOGGING::Log("[DEFERRED] SetCheckGLErrors ", (enabled ? 1 : 0));
    optCheckGLErrors = enabled;
    SetGLErrorChecking(enabled);
}

bool DeferredRenderer::GetCheckGLErrors() const {
    return optCheckGLErrors;
}

void DeferredRenderer::SetRenderLog(bool enabled) {
    optRenderLOG = enabled;
}

SceneManager& DeferredRenderer::GetScene() {
    return scene_;
}

DrawCmd* DeferredRenderer::GetSelectedObject() {
    ValidateSelectionPointer();
    return selectedObject;
}

int DeferredRenderer::GetSelectedIndex() const {
    return selectedIndex;
}

Camera& DeferredRenderer::GetCamera() {
    return camera_;
}

const Camera& DeferredRenderer::GetCamera() const {
    return camera_;
}
