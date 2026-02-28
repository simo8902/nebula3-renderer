// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "DeferredRenderer.h"
#include "DeferredRendererAnimation.h"
#include "SelectionRaycaster.h"
#include "Parser.h"
#include "GLStateDebug.h"
#include "AnimationSystem.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "rendering/opengl/OpenGLDevice.h"
#include "rendering/opengl/GLFWPlatform.h"
#include "rendering/opengl/OpenGLShaderManager.h"
#include <algorithm>
#include <array>
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
#include "DrawBatchSystem.h"
#include "MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"
#include "Model/ModelServer.h"
#include "Servers/MeshServer.h"
#include "Servers/TextureServer.h"
#include "Map/MapHeader.h"
#include "ParticleData/ParticleServer.h"
#include "NC.Logger.h"
#include "gtx/norm.hpp"

static const glm::vec3 kLightDirToSun = glm::normalize(glm::vec3(0.0f, 0.5f, -0.8f));

extern bool gEnableGLErrorChecking;

using UniformID = NDEVC::Graphics::Shader::UniformID;
static constexpr UniformID U_PROJECTION = NDEVC::Graphics::Shader::MakeUniformID("projection");
static constexpr UniformID U_VIEW = NDEVC::Graphics::Shader::MakeUniformID("view");
static constexpr UniformID U_MODEL = NDEVC::Graphics::Shader::MakeUniformID("model");
static constexpr UniformID U_TEXTURE_TRANSFORM0 = NDEVC::Graphics::Shader::MakeUniformID("textureTransform0");
static constexpr UniformID U_MAT_EMISSIVE_INTENSITY = NDEVC::Graphics::Shader::MakeUniformID("MatEmissiveIntensity");
static constexpr UniformID U_MAT_SPECULAR_INTENSITY = NDEVC::Graphics::Shader::MakeUniformID("MatSpecularIntensity");
static constexpr UniformID U_MAT_SPECULAR_POWER = NDEVC::Graphics::Shader::MakeUniformID("MatSpecularPower");
static constexpr UniformID U_USE_SKINNING = NDEVC::Graphics::Shader::MakeUniformID("UseSkinning");
static constexpr UniformID U_USE_INSTANCING = NDEVC::Graphics::Shader::MakeUniformID("UseInstancing");
static constexpr UniformID U_ALPHA_TEST = NDEVC::Graphics::Shader::MakeUniformID("alphaTest");
static constexpr UniformID U_ALPHA_CUTOFF = NDEVC::Graphics::Shader::MakeUniformID("alphaCutoff");
static constexpr UniformID U_DIFF_MAP0 = NDEVC::Graphics::Shader::MakeUniformID("DiffMap0");
static constexpr UniformID U_SPEC_MAP0 = NDEVC::Graphics::Shader::MakeUniformID("SpecMap0");
static constexpr UniformID U_BUMP_MAP0 = NDEVC::Graphics::Shader::MakeUniformID("BumpMap0");
static constexpr UniformID U_EMSV_MAP0 = NDEVC::Graphics::Shader::MakeUniformID("EmsvMap0");
static constexpr UniformID U_TWO_SIDED = NDEVC::Graphics::Shader::MakeUniformID("twoSided");
static constexpr UniformID U_IS_FLAT_NORMAL = NDEVC::Graphics::Shader::MakeUniformID("isFlatNormal");
static constexpr UniformID U_RECEIVES_DECALS = NDEVC::Graphics::Shader::MakeUniformID("ReceivesDecals");
static constexpr UniformID U_CAMERA_POS = NDEVC::Graphics::Shader::MakeUniformID("CameraPos");
static constexpr UniformID U_LIGHT_DIR_WS = NDEVC::Graphics::Shader::MakeUniformID("LightDirWS");
static constexpr UniformID U_LIGHT_COLOR = NDEVC::Graphics::Shader::MakeUniformID("LightColor");
static constexpr UniformID U_AMBIENT_COLOR = NDEVC::Graphics::Shader::MakeUniformID("AmbientColor");
static constexpr UniformID U_BACK_LIGHT_COLOR = NDEVC::Graphics::Shader::MakeUniformID("BackLightColor");
static constexpr UniformID U_BACK_LIGHT_OFFSET = NDEVC::Graphics::Shader::MakeUniformID("BackLightOffset");
static constexpr UniformID U_GPOSITION = NDEVC::Graphics::Shader::MakeUniformID("gPosition");
static constexpr UniformID U_GNORMAL_DEPTH_PACKED = NDEVC::Graphics::Shader::MakeUniformID("gNormalDepthPacked");
static constexpr UniformID U_GPOSITION_WS = NDEVC::Graphics::Shader::MakeUniformID("gPositionWS");
static constexpr UniformID U_SHADOW_MAP_CASCADE0 = NDEVC::Graphics::Shader::MakeUniformID("shadowMapCascade0");
static constexpr UniformID U_SHADOW_MAP_CASCADE1 = NDEVC::Graphics::Shader::MakeUniformID("shadowMapCascade1");
static constexpr UniformID U_SHADOW_MAP_CASCADE2 = NDEVC::Graphics::Shader::MakeUniformID("shadowMapCascade2");
static constexpr UniformID U_SHADOW_MAP_CASCADE3 = NDEVC::Graphics::Shader::MakeUniformID("shadowMapCascade3");
static constexpr UniformID U_NUM_CASCADES = NDEVC::Graphics::Shader::MakeUniformID("numCascades");
static constexpr UniformID U_SCREEN_SIZE = NDEVC::Graphics::Shader::MakeUniformID("screenSize");
static constexpr UniformID U_MVP = NDEVC::Graphics::Shader::MakeUniformID("mvp");
static constexpr UniformID U_LIGHT_POS_RANGE = NDEVC::Graphics::Shader::MakeUniformID("lightPosRange");
static constexpr UniformID U_LIGHT_COLOR_IN = NDEVC::Graphics::Shader::MakeUniformID("lightColorIn");
static constexpr UniformID U_FOG_START = NDEVC::Graphics::Shader::MakeUniformID("fogStart");
static constexpr UniformID U_FOG_END = NDEVC::Graphics::Shader::MakeUniformID("fogEnd");
static constexpr UniformID U_FOG_COLOR = NDEVC::Graphics::Shader::MakeUniformID("fogColor");
static constexpr UniformID U_FOG_DISTANCES = NDEVC::Graphics::Shader::MakeUniformID("fogDistances");
static constexpr UniformID U_GALBEDO_SPEC = NDEVC::Graphics::Shader::MakeUniformID("gAlbedoSpec");
static constexpr UniformID U_LIGHT_BUFFER_TEX = NDEVC::Graphics::Shader::MakeUniformID("lightBufferTex");
static constexpr UniformID U_GPOSITION_VS = NDEVC::Graphics::Shader::MakeUniformID("gPositionVS");
static constexpr UniformID U_GEMISSIVE_TEX = NDEVC::Graphics::Shader::MakeUniformID("gEmissiveTex");
static constexpr UniformID U_USE_INSTANCED_POINT_LIGHTS = NDEVC::Graphics::Shader::MakeUniformID("UseInstancedPointLights");
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
static constexpr bool kForceEnvironmentThroughStandardGeometryPath = true;
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

std::string ResolveStartupMapPath() {
    namespace fs = std::filesystem;
    static constexpr const char* kBaseMapName = "a0006_grimford_hub.map";

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

        const fs::path baseMapPath = dirPath / kBaseMapName;
        return fileExists(baseMapPath) ? baseMapPath.string() : std::string();
    };

    const std::string envPath = ReadEnvString("NDEVC_STARTUP_MAP");
    if (!envPath.empty()) {
        const fs::path envFsPath(envPath);
        if (fileExists(envFsPath)) {
            if (envFsPath.filename() == kBaseMapName) {
                return envFsPath.string();
            }
            std::cerr << "[Init] NDEVC_STARTUP_MAP must point to " << kBaseMapName
                      << " (or a directory containing it): " << envPath << "\n";
        }
        if (const std::string dirChoice = baseMapFromDirectory(envFsPath); !dirChoice.empty()) {
            return dirChoice;
        }
        const std::array<fs::path, 2> envSubdirs = {
            envFsPath / "maps",
            envFsPath / "bin" / "maps"
        };
        for (const auto& subdir : envSubdirs) {
            if (const std::string dirChoice = baseMapFromDirectory(subdir); !dirChoice.empty()) {
                return dirChoice;
            }
        }
        std::cerr << "[Init] NDEVC_STARTUP_MAP not found/usable for " << kBaseMapName
                  << ": " << envPath << "\n";
    }

    const std::array<fs::path, 7> fileCandidates = {
        fs::path(SOURCE_DIR) / "bin" / kBaseMapName,
        fs::path(SOURCE_DIR) / "bin" / "maps" / kBaseMapName,
        fs::path(SOURCE_DIR) / "maps" / kBaseMapName,
        fs::path("bin") / kBaseMapName,
        fs::path("bin") / "maps" / kBaseMapName,
        fs::path("maps") / kBaseMapName,
        fs::path(MAP_ROOT) / kBaseMapName
    };
    for (const auto& p : fileCandidates) {
        if (fileExists(p)) return p.string();
    }

    const std::array<fs::path, 5> dirCandidates = {
        fs::path(SOURCE_DIR) / "bin" / "maps",
        fs::path(SOURCE_DIR) / "maps",
        fs::path("bin") / "maps",
        fs::path("maps"),
        fs::path(MAP_ROOT)
    };
    for (const auto& d : dirCandidates) {
        if (const std::string chosen = baseMapFromDirectory(d); !chosen.empty()) {
            return chosen;
        }
    }

    std::cerr << "[Init] Required startup base map not found: " << kBaseMapName << "\n";
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
        std::cerr << "OpenGL Error at " << label << ": 0x" << std::hex << err << std::dec;
        switch (err) {
            case GL_INVALID_ENUM:                  std::cerr << " (INVALID_ENUM)"; break;
            case GL_INVALID_VALUE:                 std::cerr << " (INVALID_VALUE)"; break;
            case GL_INVALID_OPERATION:             std::cerr << " (INVALID_OPERATION)"; break;
            case GL_OUT_OF_MEMORY:                 std::cerr << " (OUT_OF_MEMORY)"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: std::cerr << " (INVALID_FRAMEBUFFER_OPERATION)"; break;
        }
        std::cerr << "\n";
    }
}

DeferredRenderer::DeferredRenderer() : shadowMapCascades{}, shadowFBOCascades{}
{
}

void DeferredRenderer::releaseOwnedGLResources() {
    if (shadowMatrixSSBO_) {
        glDeleteBuffers(1, &shadowMatrixSSBO_);
        shadowMatrixSSBO_ = 0;
        shadowMatrixSSBOCapacity_ = 0;
    }
    if (shadowIndirectBuffer_) {
        glDeleteBuffers(1, &shadowIndirectBuffer_);
        shadowIndirectBuffer_ = 0;
        shadowIndirectBufferCapacity_ = 0;
    }

    if (pointLightInstanceVBO) {
        glDeleteBuffers(1, &pointLightInstanceVBO);
        pointLightInstanceVBO = 0;
        pointLightInstanceCapacity = 0;
    }

    if (quadVAO) {
        glDeleteVertexArrays(1, &quadVAO);
        quadVAO = 0;
    }
    if (quadVBO) {
        glDeleteBuffers(1, &quadVBO);
        quadVBO = 0;
    }
    screenVAO = 0;

    if (sphereVAO) {
        glDeleteVertexArrays(1, &sphereVAO);
        sphereVAO = 0;
    }
    sphereIndexCount = 0;

    if (debugCellVAO_) {
        glDeleteVertexArrays(1, &debugCellVAO_);
        debugCellVAO_ = 0;
    }
    if (debugCellVBO_) {
        glDeleteBuffers(1, &debugCellVBO_);
        debugCellVBO_ = 0;
    }
    if (debugLineProgram_) {
        glDeleteProgram(debugLineProgram_);
        debugLineProgram_ = 0;
    }

    if (gDepthCopyReadFBO) {
        glDeleteFramebuffers(1, &gDepthCopyReadFBO);
        gDepthCopyReadFBO = 0;
    }
    if (gDepthDecalFBO) {
        glDeleteFramebuffers(1, &gDepthDecalFBO);
        gDepthDecalFBO = 0;
    }

    if (gBuffer) {
        glDeleteFramebuffers(1, &gBuffer);
        gBuffer = 0;
    }
    if (lightFBO) {
        glDeleteFramebuffers(1, &lightFBO);
        lightFBO = 0;
    }
    if (sceneFBO) {
        glDeleteFramebuffers(1, &sceneFBO);
        sceneFBO = 0;
    }
    if (sceneFBO2) {
        glDeleteFramebuffers(1, &sceneFBO2);
        sceneFBO2 = 0;
    }

    for (int i = 0; i < NUM_CASCADES; ++i) {
        if (shadowFBOCascades[i]) {
            glDeleteFramebuffers(1, &shadowFBOCascades[i]);
            shadowFBOCascades[i] = 0;
        }
        shadowMapCascades[i] = 0;
    }

    if (gPositionWSRead) {
        glDeleteTextures(1, &gPositionWSRead);
        gPositionWSRead = 0;
    }

    // gSamplerShadow is usually owned by samplerShadow_abstracted.
    if (gSamplerShadow && !samplerShadow_abstracted) {
        glDeleteSamplers(1, &gSamplerShadow);
    }
    gSamplerShadow = 0;
}

DeferredRenderer::~DeferredRenderer() {
    Shutdown();
}

void DeferredRenderer::initGLFW() {
    using GLFWPlatformType = NDEVC::Platform::GLFW::GLFWPlatform;
    platform_ = std::make_unique<GLFWPlatformType>();
    if (platform_->Initialize()) {
        window_ = platform_->CreateApplicationWindow("NDEVC", width, height);
        if (window_) inputSystem_ = platform_->CreateInputSystem();
    }
    if (!window_) throw std::runtime_error("Window creation failed");
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

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        throw std::runtime_error("Failed to init GLAD");

    if (!InitializeImGui())
        throw std::runtime_error("Failed to initialize ImGui");

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
    if (optRenderLOG) std::cout << "[Init] VSync enabled (swap interval = 1)\n";

    try {
        shaderManager = std::make_unique<NDEVC::Graphics::OpenGL::OpenGLShaderManager>();
        shaderManager->Initialize();
    } catch(const std::exception& e) {
        std::cerr << "ShaderManager init failed: " << e.what() << "\n";
        throw;
    }

    if (!ParticlesDisabled()) {
        Particles::ParticleServer::Instance().Open(shaderManager.get());
        if (!Particles::ParticleServer::Instance().GetParticleRenderer()) {
            std::cerr << "[Init] ParticleServer failed to provide renderer\n";
        } else {
            if (optRenderLOG) std::cout << "[Init] ParticleServer renderer ready\n";
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
        whiteTex = *(GLuint*)whiteTex_abstracted->GetNativeHandle();
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
        blackTex = *(GLuint*)blackTex_abstracted->GetNativeHandle();
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
        normalTex = *(GLuint*)normalTex_abstracted->GetNativeHandle();
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
            blackCubeTex = *(GLuint*)blackCubeTex_abstracted->GetNativeHandle();
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
        gSamplerRepeat = *(GLuint*)samplerRepeat_abstracted->GetNativeHandle();
    }

    SamplerDesc samplerClampDesc;
    samplerClampDesc.minFilter = SamplerFilter::LinearMipmapLinear;
    samplerClampDesc.magFilter = SamplerFilter::Linear;
    samplerClampDesc.wrapS = SamplerWrap::ClampToEdge;
    samplerClampDesc.wrapT = SamplerWrap::ClampToEdge;
    samplerClamp_abstracted = device_->CreateSampler(samplerClampDesc);
    if (samplerClamp_abstracted) {
        gSamplerClamp = *(GLuint*)samplerClamp_abstracted->GetNativeHandle();
    }

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
        shadowMapCascades[i] = *(GLuint*)shadowMapTextures[i]->GetNativeHandle();

        glGenFramebuffers(1, &shadowFBOCascades[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOCascades[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D, shadowMapCascades[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[CSM] Cascade " << i << " FBO incomplete: 0x" << std::hex << status << std::dec << "\n";
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
    glGenFramebuffers(1, &gBuffer);
    if (optRenderLOG) std::cout << "[InitDeferred] Created gBuffer FBO " << gBuffer << "\n";

    using namespace NDEVC::Graphics;

    TextureDesc lightBufferDesc;
    lightBufferDesc.type = TextureType::Texture2D;
    lightBufferDesc.format = Format::RGBA16F;
    lightBufferDesc.width = width;
    lightBufferDesc.height = height;
    lightBufferTex = device_->CreateTexture(lightBufferDesc);
    lightBuffer = *(GLuint*)lightBufferTex->GetNativeHandle();

    glGenFramebuffers(1, &lightFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lightBuffer, 0);
    GLenum lightStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (lightStatus != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[LightFBO ERROR] Status: 0x" << std::hex << lightStatus << std::dec << "\n";

    TextureDesc sceneColorDesc;
    sceneColorDesc.type = TextureType::Texture2D;
    sceneColorDesc.format = Format::RGBA16F;
    sceneColorDesc.width = width;
    sceneColorDesc.height = height;
    sceneColorTex = device_->CreateTexture(sceneColorDesc);
    sceneColor = *(GLuint*)sceneColorTex->GetNativeHandle();

    glGenFramebuffers(1, &sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);

    TextureDesc depthStencilDesc;
    depthStencilDesc.type = TextureType::Texture2D;
    depthStencilDesc.format = Format::D24_UNORM_S8_UINT;
    depthStencilDesc.width = width;
    depthStencilDesc.height = height;
    sceneDepthStencilTex = device_->CreateTexture(depthStencilDesc);
    sceneDepthStencil = *(GLuint*)sceneDepthStencilTex->GetNativeHandle();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, sceneDepthStencil, 0);

    GLenum sceneStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (sceneStatus != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[SceneFBO ERROR] Status: 0x" << std::hex << sceneStatus << std::dec << "\n";

    TextureDesc sceneColor2Desc;
    sceneColor2Desc.type = TextureType::Texture2D;
    sceneColor2Desc.format = Format::RGBA16F;
    sceneColor2Desc.width = width;
    sceneColor2Desc.height = height;
    sceneColor2Tex = device_->CreateTexture(sceneColor2Desc);
    sceneColor2 = *(GLuint*)sceneColor2Tex->GetNativeHandle();

    glGenFramebuffers(1, &sceneFBO2);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor2, 0);
    GLenum scene2Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (scene2Status != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[SceneFBO2 ERROR] Status: 0x" << std::hex << scene2Status << std::dec << "\n";

    glGenTextures(1, &gPositionWSRead);
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
        sphereVBO = *(GLuint*)sphereVBO_abstracted->GetNativeHandle();
    }

    BufferDesc eboDesc;
    eboDesc.type = BufferType::Index;
    eboDesc.size = indices.size() * sizeof(unsigned int);
    eboDesc.initialData = indices.data();
    sphereEBO_abstracted = device_->CreateBuffer(eboDesc);
    if (sphereEBO_abstracted) {
        sphereEBO = *(GLuint*)sphereEBO_abstracted->GetNativeHandle();
    }

    glGenVertexArrays(1, &sphereVAO);
    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    if (!pointLightInstanceVBO) {
        glGenBuffers(1, &pointLightInstanceVBO);
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
    static int frameCount = 0;
    static bool logShadows = (frameCount++ == 5);
    glm::vec3 lightDirWorld = -kLightDirToSun;

    struct ShadowCaster {
        const DrawCmd* draw = nullptr;
        float radius = 0.0f;
        float viewDepth = 0.0f;
    };
    std::vector<ShadowCaster> shadowCasters;
    shadowCasters.reserve(solidDraws.size());

    const glm::mat4 viewMatrix = camera_.getViewMatrix();

    auto isFilteredShadowShader = [](const DrawCmd& obj) {
        return obj.shdr == "shd:water" || obj.shdr == "shd:decal" ||
               obj.shdr == "shd:simplelayer" ||
               obj.shdr == "shd:uvanimated";
    };

    for (const auto& obj : solidDraws) {
        if (!obj.mesh || isFilteredShadowShader(obj)) continue;
        if (obj.disabled) continue;

        const glm::vec3 localCenter = (glm::vec3(obj.localBoxMin) + glm::vec3(obj.localBoxMax)) * 0.5f;
        const float localRadius = glm::length(glm::vec3(obj.localBoxMax) - glm::vec3(obj.localBoxMin)) * 0.5f;
        const float sx = glm::length(glm::vec3(obj.worldMatrix[0]));
        const float sy = glm::length(glm::vec3(obj.worldMatrix[1]));
        const float sz = glm::length(glm::vec3(obj.worldMatrix[2]));
        const float radius = localRadius * std::max(sx, std::max(sy, sz));
        const glm::vec3 worldCenter = glm::vec3(obj.worldMatrix * glm::vec4(localCenter, 1.0f));

        const float viewDepth = -glm::vec3(viewMatrix * glm::vec4(worldCenter, 1.0f)).z;
        shadowCasters.push_back(ShadowCaster{&obj, radius, viewDepth});
    }

    if (logShadows) {
        std::cout << "  Computing light space matrices for " << NUM_CASCADES << " cascades\n";
        std::cout << "  Light direction: (" << lightDirWorld.x << ", " << lightDirWorld.y << ", " << lightDirWorld.z << ")\n";
        std::cout << "  Shadow casters considered: " << shadowCasters.size() << "/" << solidDraws.size() << "\n";
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
            std::cerr << "shadow light not found/not working\n";
        } else {
            shadowShaders->Use();
            checkGLError("After use shadow shader");

            if (logShadows) {
                GLint currentProgram = (GLint)(uintptr_t)shadowShaders->GetNativeHandle();
                std::cout << "    Shadow shader program: " << currentProgram << "\n";
            }

            shadowShaders->SetMat4("lightSpaceMatrix", lightSpaceMatrices[cascade]);
            checkGLError("After set lightSpaceMatrix");
        }

        if (shadowShaders) {
            // Build per-cascade matrix list and indirect commands
            std::vector<glm::mat4> shadowMatrices;
            std::vector<DrawCommand> shadowCommands;
            shadowMatrices.reserve(shadowCasters.size());
            shadowCommands.reserve(shadowCasters.size() * 2);
            int triangleCount = 0;

            for (const auto& caster : shadowCasters) {
                const DrawCmd& obj = *caster.draw;
                if (caster.viewDepth + caster.radius < cascadeNear) continue;
                if (caster.viewDepth - caster.radius > cascadeFar) continue;
                if (!obj.mesh) continue;

                const uint32_t baseInstance = static_cast<uint32_t>(shadowMatrices.size());
                bool matrixAdded = false;
                auto ensureMatrix = [&]() {
                    if (!matrixAdded) {
                        shadowMatrices.push_back(obj.worldMatrix);
                        matrixAdded = true;
                    }
                };

                if (obj.group >= 0 && obj.group < (int)obj.mesh->groups.size()) {
                    const auto& g = obj.mesh->groups[obj.group];
                    if (g.indexCount() == 0) continue;
                    ensureMatrix();
                    DrawCommand cmd{};
                    cmd.count = g.indexCount();
                    cmd.instanceCount = 1;
                    cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                    cmd.baseVertex = 0;
                    cmd.baseInstance = baseInstance;
                    shadowCommands.push_back(cmd);
                    triangleCount += g.indexCount() / 3;
                } else {
                    for (const auto& g : obj.mesh->groups) {
                        if (g.indexCount() == 0) continue;
                        ensureMatrix();
                        DrawCommand cmd{};
                        cmd.count = g.indexCount();
                        cmd.instanceCount = 1;
                        cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                        cmd.baseVertex = 0;
                        cmd.baseInstance = baseInstance;
                        shadowCommands.push_back(cmd);
                        triangleCount += g.indexCount() / 3;
                    }
                }
            }

            if (!shadowCommands.empty()) {
                // Upload model matrices to SSBO at binding point 1
                if (!shadowMatrixSSBO_) glGenBuffers(1, &shadowMatrixSSBO_);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, shadowMatrixSSBO_);
                const size_t matrixBytes = shadowMatrices.size() * sizeof(glm::mat4);
                if (matrixBytes > shadowMatrixSSBOCapacity_) {
                    glBufferData(GL_SHADER_STORAGE_BUFFER,
                                 static_cast<GLsizeiptr>(matrixBytes * 2),
                                 nullptr, GL_STREAM_DRAW);
                    shadowMatrixSSBOCapacity_ = matrixBytes * 2;
                }
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                static_cast<GLsizeiptr>(matrixBytes),
                                shadowMatrices.data());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, shadowMatrixSSBO_);

                // Upload indirect commands
                if (!shadowIndirectBuffer_) glGenBuffers(1, &shadowIndirectBuffer_);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, shadowIndirectBuffer_);
                const size_t cmdBytes = shadowCommands.size() * sizeof(DrawCommand);
                if (cmdBytes > shadowIndirectBufferCapacity_) {
                    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                                 static_cast<GLsizeiptr>(cmdBytes * 2),
                                 nullptr, GL_STREAM_DRAW);
                    shadowIndirectBufferCapacity_ = cmdBytes * 2;
                }
                glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                                static_cast<GLsizeiptr>(cmdBytes),
                                shadowCommands.data());

                // Single indirect draw for the entire cascade
                MegaBuffer::instance().bind();
                glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                            nullptr,
                                            static_cast<GLsizei>(shadowCommands.size()),
                                            0);
                glBindVertexArray(0);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            }
            checkGLError("After shadow draw calls");

            if (logShadows) {
                std::cout << "    Drew " << shadowCommands.size() << " indirect commands, "
                          << triangleCount << " triangles\n";
            }
        }
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    checkGLError("After shadow pass cleanup");

    if (logShadows) std::cout << "  Shadow rendering complete, viewport restored\n";
}


void DeferredRenderer::Initialize() {
    const bool traceInit = ReadEnvToggle("NDEVC_INIT_TRACE");
    auto logInitStep = [&](const char* step) {
        if (traceInit && step) {
            std::cerr << "[INIT TRACE] " << step << "\n";
        }
        if (window_) {
            window_->PollEvents();
        }
    };

    shutdownComplete_ = false;
    logInitStep("start");
    InstallNax3Provider();
    logInitStep("InstallNax3Provider");
    initGLFW();
    logInitStep("initGLFW");
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
    ResetStreamingState();
    logInitStep("ResetStreamingState");

    const std::string startupMapPath = ResolveStartupMapPath();
    currentMapSourcePath_ = startupMapPath;
    if (startupMapPath.empty()) {
        std::cerr << "[Init] No startup map found. Required map: a0002_start_hub.map.\n";
    } else if (optRenderLOG) {
        std::cout << "[Init] Startup map: " << startupMapPath << "\n";
    }

    MapLoader loader;
    ownedCurrentMap_.reset();
    currentMap = nullptr;
    if (!startupMapPath.empty()) {
        ownedCurrentMap_.reset(loader.load_map(startupMapPath));
        currentMap = ownedCurrentMap_.get();
    }
    logInitStep("MapLoader::load_map");
    if (!currentMap) {
        if (!startupMapPath.empty()) {
            std::cerr << "map load FAILED: " << startupMapPath << "\n";
        }
    } else {
        if (optRenderLOG) std::cout << "Map loaded: " << currentMap->instances.size() << " instances\n";
        LoadMapInstances(currentMap);
        logInitStep("LoadMapInstances");
    }

    MeshServer::instance().buildMegaBuffer();
    logInitStep("buildMegaBuffer");

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
    logInitStep("DrawBatchSystem::init");

    BuildVisibilityGrids();
    logInitStep("BuildVisibilityGrids");

    // Center the top-down camera above the map.
    if (currentMap) {
        const MapInfo& info = currentMap->info;
        const float halfDiag = glm::length(glm::vec2(info.extents.x, info.extents.z));
        const float elevation = std::max(halfDiag * 1.2f, 200.0f);
        camera_.setPosition(glm::vec3(info.center.x,
                                      info.center.y + elevation,
                                      info.center.z + elevation * 0.7f));
    }

    rebuildAnimatedDrawLists();
    logInitStep("rebuildAnimatedDrawLists");
    decalBatchDirty = true;
    scenePrepared = true;

    setupRenderPasses();
    logInitStep("setupRenderPasses");
    WriteWebSnapshot("initialize");
    logInitStep("WriteWebSnapshot");
}

void DeferredRenderer::Shutdown() {
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

    animatorInstances.clear();
    animatedDraws.clear();
    pointLights.clear();
    particleNodes.clear();
    instances.clear();
    instanceSpawnTimes.clear();
    instanceModelPathByOwner_.clear();
    instanceMeshResourceByOwner_.clear();
    loadedModelRefCountByPath_.clear();
    loadedMeshByModelPath_.clear();
    ResetStreamingState();
    nodeMap.clear();
    gClips.clear();
    gAnimPose.clear();
    ClearAnimationOwnerData();
    selectedObject = nullptr;
    selectedIndex = -1;
    cachedObj = DrawCmd{};
    cachedIndex = -1;
    visibleCells_.clear();
    lastVisibleCells_.clear();
    scenePrepared = false;

    shadowGraph.reset();
    geometryGraph.reset();
    decalGraph.reset();
    lightingGraph.reset();
    particleGraph.reset();
    DrawBatchSystem::instance().reset(true);

    bool hasGLContext = false;
    if (window_) {
        window_->MakeCurrent();
        hasGLContext = true;
    } else if (glfwGetCurrentContext() != nullptr) {
        hasGLContext = true;
    }

    if (hasGLContext) {
        DrawBatchSystem::instance().shutdownGL();
        MegaBuffer::instance().shutdownGL();
        TextureServer::instance().clearCache(true);
        releaseOwnedGLResources();
    } else {
        const bool hasLeakedGLState =
            DrawBatchSystem::instance().hasGLResources() ||
            MegaBuffer::instance().hasGLResources() ||
            TextureServer::instance().hasCachedTextures() ||
            shadowMatrixSSBO_ || shadowIndirectBuffer_ || pointLightInstanceVBO ||
            quadVAO || quadVBO || sphereVAO || debugCellVAO_ || debugCellVBO_ ||
            debugLineProgram_ || gDepthCopyReadFBO || gDepthDecalFBO ||
            gBuffer || lightFBO || sceneFBO || sceneFBO2 || gPositionWSRead ||
            shadowFBOCascades[0] || shadowFBOCascades[1];
        if (hasLeakedGLState) {
            std::cerr << "[RAII][ERROR] Shutdown without active GL context while GL resources are still live\n";
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

    whiteTex = 0;
    blackTex = 0;
    normalTex = 0;
    blackCubeTex = 0;
    gSamplerRepeat = 0;
    gSamplerClamp = 0;
    gPositionVS = 0;
    gPositionWS = 0;
    gPosition = 0;
    gNormalDepthPacked = 0;
    gAlbedoSpec = 0;
    gDepth = 0;
    gEmissive = 0;
    gDepthDecal = 0;
    sceneDepthStencil = 0;
    sceneColor = 0;
    sceneColor2 = 0;
    lightBuffer = 0;

    if (shaderManager) shaderManager->Shutdown();
    shaderManager.reset();

    device_.reset();

    ownedCurrentMap_.reset();
    currentMap = nullptr;
    currentMapSourcePath_.clear();

    window_.reset();
    inputSystem_.reset();
    platform_.reset();
}

void DeferredRenderer::RenderFrame() {
    while (!ShouldClose()) {
        PollEvents();
        RenderSingleFrame();
    }
}

void DeferredRenderer::PollEvents() {
    if (window_) {
        window_->PollEvents();
    }
    if (inputSystem_) {
        inputSystem_->Update();
    }
}

void DeferredRenderer::RenderSingleFrame() {
    if (!window_ || window_->ShouldClose()) return;
    renderSingleFrame();
}

bool DeferredRenderer::ShouldClose() const {
    return !window_ || window_->ShouldClose();
}

void DeferredRenderer::Resize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    width = newWidth;
    height = newHeight;
    resizeFramebuffers(newWidth, newHeight);
}

void DeferredRenderer::AppendModel(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
    appendN3WTransform(path, pos, rot, scale);

    // Newly appended models can introduce meshes that are not yet packed in the mega buffer.
    // Rebuild/rebase so indirect draws point at valid index ranges.
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
    ApplyDisabledDrawFlags();
    WriteWebSnapshot("append_model");
}

void DeferredRenderer::LoadMap(const MapData* map) {
    if (!map) {
        std::cerr << "LoadMap called with null map\n";
        return;
    }

    if (currentMap == map) {
        ReloadMapWithCurrentMode();
        scenePrepared = true;
        return;
    }

    // Take ownership via deep copy so renderer lifetime does not depend on caller.
    ownedCurrentMap_ = std::make_unique<MapData>(*map);
    currentMap = ownedCurrentMap_.get();
    currentMapSourcePath_ = "[external_map]";

    ReloadMapWithCurrentMode();
    scenePrepared = true;
}

void DeferredRenderer::SetCheckGLErrors(bool enabled) {
    optCheckGLErrors = enabled;
    SetGLErrorChecking(enabled);
}

bool DeferredRenderer::GetCheckGLErrors() const {
    return optCheckGLErrors;
}

void DeferredRenderer::SetRenderLog(bool enabled) {
    optRenderLOG = enabled;
}

DrawCmd* DeferredRenderer::GetSelectedObject() {
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

