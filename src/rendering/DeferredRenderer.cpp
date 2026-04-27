// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Rendering.h"
#include "Rendering/DeferredRenderer.h"
#include "Rendering/GLStateDebug.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
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
#include <fstream>
#include <filesystem>
#include <system_error>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include "Rendering/MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"
#include "Assets/Model/ModelServer.h"
#include "Assets/Map/MapLoader.h"
#include "Assets/Servers/MeshServer.h"
#include "Assets/Servers/TextureServer.h"
#include "Assets/Map/MapHeader.h"
#include "Core/Logger.h"
#include "Core/VFS.h"
#include "gtx/norm.hpp"

using namespace NC::LOGGING;

// ── Qt external-platform injection (set before Initialize() runs) ─────────
static std::unique_ptr<NDEVC::Platform::IPlatform> s_pendingPlatform;
static std::shared_ptr<NDEVC::Platform::IWindow>   s_pendingWindow;
static GLADloadproc                                s_pendingLoader = nullptr;

void DeferredRenderer::SetPendingExternalPlatform(
        std::unique_ptr<NDEVC::Platform::IPlatform> platform,
        std::shared_ptr<NDEVC::Platform::IWindow>   window,
        GLADloadproc                                loader) {
    s_pendingPlatform = std::move(platform);
    s_pendingWindow   = std::move(window);
    s_pendingLoader   = loader;
}

extern bool gEnableGLErrorChecking;

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

bool CameraTraceEnabledByDefault() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "NDEVC_CAMERA_TRACE") != 0 || value == nullptr) {
        return true;
    }
    const bool enabled = value[0] != '0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv("NDEVC_CAMERA_TRACE");
    return value == nullptr || value[0] != '0';
#endif
}

bool MountPackageFromEnvironment() {
    if (NC::VFS::Instance().IsMounted()) {
        Debug(Category::Graphics, "VFS mounted, assets served in-memory");
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
    return ext == ".map" || ext == ".n3w";
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
        Debug(Category::Graphics, "Startup map autoload disabled (NDEVC_STARTUP_MAP empty)");
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
        Error(Category::Graphics, "NDEVC_STARTUP_MAP not found/usable: ", envPath);
        return {};
    }
    return {};
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

bool IsFiniteVec3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool BuildMapCameraFit(const MapInfo& info, glm::vec3& outPos, glm::vec3& outTarget) {
    const glm::vec3 center(info.center.x, info.center.y, info.center.z);
    const glm::vec3 extents(std::abs(info.extents.x), std::abs(info.extents.y), std::abs(info.extents.z));
    if (!IsFiniteVec3(center) || !IsFiniteVec3(extents)) {
        return false;
    }

    const float halfDiag = glm::length(glm::vec2(extents.x, extents.z));
    if (!std::isfinite(halfDiag) || halfDiag <= 0.001f) {
        return false;
    }

    outTarget = center;
    // Position camera at an angle looking at the center
    outPos = center + glm::vec3(halfDiag * 0.8f, halfDiag * 0.5f, halfDiag * 0.8f);
    return true;
}

}

static void checkGLError(const char* label) {
    if (!gEnableGLErrorChecking) return;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        Error(Category::Graphics, "GL Error at ", (label ? label : "<unknown>"), ": 0x", std::hex, static_cast<unsigned int>(err), std::dec);
        switch (err) {
            case GL_INVALID_ENUM:                  Error(Category::Graphics, "GL Reason INVALID_ENUM"); break;
            case GL_INVALID_VALUE:                 Error(Category::Graphics, "GL Reason INVALID_VALUE"); break;
            case GL_INVALID_OPERATION:             Error(Category::Graphics, "GL Reason INVALID_OPERATION"); break;
            case GL_OUT_OF_MEMORY:                 Error(Category::Graphics, "GL Reason OUT_OF_MEMORY"); break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: Error(Category::Graphics, "GL Reason INVALID_FRAMEBUFFER_OPERATION"); break;
        }
    }
}

DeferredRenderer::DeferredRenderer()
{
    Trace(Category::Graphics, "ctor");
}

double DeferredRenderer::GetTimeSeconds() const {
    return std::chrono::duration<double>(RendererClock::now() - rendererClockStart_).count();
}

void DeferredRenderer::releaseOwnedGLResources() {
    Debug(Category::Graphics, "releaseOwnedGLResources begin");

    quadVAO.reset();
    quadVBO.reset();
    screenVAO.id = 0;

    if (samplerShadow_abstracted) {
        gSamplerShadow.id = 0;
    } else {
        gSamplerShadow.reset();
    }
    Debug(Category::Graphics, "releaseOwnedGLResources end");
}

DeferredRenderer::~DeferredRenderer() {
    Trace(Category::Graphics, "dtor");
    Shutdown();
}

void DeferredRenderer::initGLFW() {
    Info(Category::Graphics, "initGLFW begin w=", width, " h=", height);

    // ── Platform / window selection ───────────────────────────────────────
    const bool useExternalPlatform = (s_pendingPlatform != nullptr);
    if (useExternalPlatform) {
        platform_ = std::move(s_pendingPlatform);
        window_   = std::move(s_pendingWindow);
        if (platform_) inputSystem_ = platform_->CreateInputSystem();
        Info(Category::Graphics, "initGLFW using external platform (Qt)");
    } else {
        using GLFWPlatformType = NDEVC::Platform::GLFW::GLFWPlatform;
        platform_ = std::make_unique<GLFWPlatformType>();
        if (platform_->Initialize()) {
            window_ = platform_->CreateApplicationWindow("NDEVC", width, height);
            if (window_) inputSystem_ = platform_->CreateInputSystem();
        }
        if (!window_) {
            Error(Category::Graphics, "initGLFW failed: Window creation failed");
            throw std::runtime_error("Window creation failed");
        }
    }

    window_->MakeCurrent();

    // ── GLFW-specific native callbacks ────────────────────────────────────
    if (!useExternalPlatform) {
        if (GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window_->GetNativeHandle())) {
            glfwSetWindowUserPointer(glfwWin, this);
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
        }
    }

    // ── Portable window callbacks (both GLFW and Qt paths) ───────────────
    window_->SetFramebufferSizeCallback([this](int w, int h) {
        if (w <= 0 || h <= 0) return;
        this->Resize(w, h);
    });
    window_->SetScrollCallback([this](double, double scrollY) {
        this->accumulatedScrollDelta_ += static_cast<float>(scrollY);
    });

    // ── GLAD ─────────────────────────────────────────────────────────────
    GLADloadproc loader = useExternalPlatform
        ? s_pendingLoader
        : reinterpret_cast<GLADloadproc>(glfwGetProcAddress);
    s_pendingLoader = nullptr;
    if (!gladLoadGLLoader(loader)) {
        Error(Category::Graphics, "initGLFW failed: Failed to init GLAD");
        throw std::runtime_error("Failed to init GLAD");
    }

    // ── GL_KHR_debug ─────────────────────────────────────────────────────
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity,
                                  GLsizei /*length*/, const GLchar* message, const void* /*userParam*/) {
            if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

            const char* src = source == GL_DEBUG_SOURCE_API             ? "API"
                            : source == GL_DEBUG_SOURCE_SHADER_COMPILER ? "SHADER_COMPILER"
                            : source == GL_DEBUG_SOURCE_APPLICATION      ? "APPLICATION"
                            : source == GL_DEBUG_SOURCE_WINDOW_SYSTEM    ? "WINDOW_SYSTEM"
                            : source == GL_DEBUG_SOURCE_THIRD_PARTY      ? "THIRD_PARTY"
                            : "OTHER";
            const char* tp  = type == GL_DEBUG_TYPE_ERROR               ? "ERROR"
                            : type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR   ? "UNDEFINED_BEHAVIOR"
                            : type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR  ? "DEPRECATED"
                            : type == GL_DEBUG_TYPE_PERFORMANCE          ? "PERFORMANCE"
                            : "INFO";
            const char* sev = severity == GL_DEBUG_SEVERITY_HIGH   ? "HIGH"
                            : severity == GL_DEBUG_SEVERITY_MEDIUM  ? "MEDIUM"
                            : "LOW";

            if (severity == GL_DEBUG_SEVERITY_HIGH || type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) {
                Error(Category::Graphics, "[KHR] src=", src, " type=", tp, " sev=", sev, " id=", id, " ", message);
            } else {
                Warn(Category::Graphics, "[KHR] src=", src, " type=", tp, " sev=", sev, " id=", id, " ", message);
            }
        }, nullptr);
        // Suppress the driver's own "buffer memory info" spam (NVIDIA 131185 / AMD 20072)
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        Info(Category::Graphics, "GL_KHR_debug enabled (synchronous)");
    } else {
        Warn(Category::Graphics, "GL_KHR_debug not available on this driver — no debug callback");
    }

    device_ = std::make_unique<NDEVC::Graphics::OpenGL::OpenGLDevice>();
    if (window_) {
        device_->SetDefaultFramebuffer(window_->GetDefaultFramebuffer());
    }

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glClearDepth(0.0f);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    window_->GetFramebufferSize(width, height);
    if (width  <= 0) width  = 1;
    if (height <= 0) height = 1;
    camera.updateAspectRatio(width, height);
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
        Info(Category::Graphics, "ShaderManager initialized");
    } catch(const std::exception& e) {
        Error(Category::Graphics, "ShaderManager init failed: ", e.what());
        throw;
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
        glTextureSubImage2D(whiteTex.id, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
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
        glTextureSubImage2D(blackTex.id, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);
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
        glTextureSubImage2D(normalTex.id, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, normal);
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
            for (int face = 0; face < 6; ++face) {
                glTextureSubImage3D(blackCubeTex.id, 0, 0, 0, face, 1, 1, 1,
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

    Info(Category::Graphics, "initGLFW end framebuffer=", width, "x", height,
                     " shaderManager=", (shaderManager ? 1 : 0));
}

void DeferredRenderer::resizeFramebuffers(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    static const bool cameraTrace = CameraTraceEnabledByDefault();
    if (cameraTrace) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
            "[CAMERA_TRACE][RESIZE_FBO] old=", width, "x", height,
            " new=", newWidth, "x", newHeight,
            " defaultFb=", (window_ ? window_->GetDefaultFramebuffer() : 0));
    }
    width = newWidth;
    height = newHeight;
    if (device_ && window_) {
        device_->SetDefaultFramebuffer(window_->GetDefaultFramebuffer());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, device_ ? device_->GetDefaultFramebuffer() : 0);

    if (device_) {
        device_->SetViewport({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f});
    }
    if (renderResources_) {
        renderResources_->Resize(width, height);
    }
    if (renderGraph_) {
        renderGraph_->OnResize(width, height);
    }

    camera.updateAspectRatio(width, height);
}

void DeferredRenderer::Initialize() {
    const bool traceInit = ReadEnvToggle("NDEVC_INIT_TRACE");
    Info(Category::Graphics, "Initialize begin trace=", (traceInit ? 1 : 0));
    auto logInitStep = [&](const char* step) {
        Debug(Category::Graphics, "Init step: ", (step ? step : "<null>"));
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
    logInitStep("InstallNax3Provider");
    initGLFW();
    logInitStep("initGLFW");

    // Engineering Mandate: Support bindless textures on modern hardware.
    // We only enable this if the extension is reported AND GLAD has successfully loaded the function pointers.
    bool bindless = (GLAD_GL_ARB_bindless_texture != 0) && (glad_glGetTextureHandleARB != nullptr);
    TextureServer::sBindlessSupported = bindless;

    Info(Category::Graphics, "GL_ARB_bindless_texture=",
        TextureServer::sBindlessSupported ? "YES" : "NO");
    InitScreenQuad();
    logInitStep("InitScreenQuad");
    InitEditorPreviewGrid();
    logInitStep("InitEditorPreviewGrid");
    InitShadowSampler();
    logInitStep("InitShadowSampler");
    if (!viewportDisabled_) {
        const std::string startupMapPath = ResolveStartupMapPath();
        if (startupMapPath.empty()) {
            Info(Category::Graphics, "Starting with empty scene (no startup map)");
        } else if (optRenderLOG) {
            std::cout << "[Init] Startup map: " << startupMapPath << "\n";
        }
        Debug(Category::Graphics, "StartupMapPath=", startupMapPath);

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
                Error(Category::Graphics, "Map load FAILED: ", startupMapPath);
            }
        } else {
            if (optRenderLOG) std::cout << "Map loaded: " << startupMap->instances.size() << " instances\n";
            Info(Category::Graphics, "Map loaded instances=", startupMap->instances.size());
            scene_->LoadMap(startupMap, startupMapPath);
            logInitStep("SceneManager::LoadMap");
        }

        // Center the camera to view the map.
        if (startupMap) {
            glm::vec3 pos(0.0f, 2.0f, 10.0f);
            glm::vec3 target(0.0f);
            if (BuildMapCameraFit(startupMap->info, pos, target)) {
                camera.setPosition(pos);
                camera.lookAt(target);
            }
        } else {
            camera.setPosition(glm::vec3(0.0f, 4.0f, 15.0f));
            camera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        }
    }
    scenePrepared = true;
    lastFrame = GetTimeSeconds(); // Initialize lastFrame to current time
    setupRenderPasses();
    logInitStep("setupRenderPasses");

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
        policy.editorLayoutEnabled = editorModeEnabled_;
        ApplyPolicy(policy);
        LogPolicySnapshot("STARTUP");
    }
    logInitStep("ApplyPolicy");

    scene_->LoadMap(nullptr);
    NC::LOGGING::Log("[SCENE] Startup empty viewport: last-scene auto-load skipped");

    NC::LOGGING::Log("[DEFERRED] Initialize end");
}


void DeferredRenderer::Shutdown() {
    if (shutdownComplete_) return;
    shutdownComplete_ = true;

    // Stop the render thread before reclaiming the GL context.
    // gate_.Stop() unblocks WaitFrame() so the thread exits cleanly.
    gate_.Stop();
    if (renderThread_.joinable()) renderThread_.join();

    // Reclaim the GL context on the main thread for shutdown GL cleanup.
    if (window_) {
        window_->MakeCurrent();
    }

    scene_->Clear();
    scenePrepared = false;

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
        TextureServer::instance().clearCache(true);
        releaseOwnedGLResources();
    } else {
        const bool hasLeakedGLState =
            MegaBuffer::instance().hasGLResources() ||
            TextureServer::instance().hasCachedTextures() ||
            quadVAO || quadVBO;
        if (hasLeakedGLState) {
            NC::LOGGING::Error("[DEFERRED][RAII] Shutdown without active GL context while GL resources are still live");
        }
        TextureServer::instance().clearCache(false);
    }
    MeshServer::instance().clearCache();
    ModelServer::instance().clearCache();

    whiteTex_abstracted.reset();
    blackTex_abstracted.reset();
    normalTex_abstracted.reset();
    blackCubeTex_abstracted.reset();

    samplerRepeat_abstracted.reset();
    samplerClamp_abstracted.reset();
    samplerShadow_abstracted.reset();

    whiteTex.id = 0;
    blackTex.id = 0;
    normalTex.id = 0;
    blackCubeTex.id = 0;
    gSamplerRepeat.id = 0;
    gSamplerClamp.id = 0;
    gSamplerShadow.id = 0;

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

void DeferredRenderer::UpdateFrameTime() {
    const double currentFrame = GetTimeSeconds();
    deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // Sanitize deltaTime to prevent instability
    if (deltaTime <= 0.0 || !std::isfinite(deltaTime)) deltaTime = 1.0 / 60.0;
    if (deltaTime > 1.0 / 30.0) deltaTime = 1.0 / 30.0;  // 33ms max, prevents 10x frame time variance
}

void DeferredRenderer::SetFrameDeltaTime(double dt) {
    deltaTime = dt;
}

void DeferredRenderer::PollEvents() {
    if (!window_) return;

    window_->PollEvents();

    // ── Mouse / Keyboard Input Handling (Previously PollFrameInput) ───────
    bool allowViewportKeyboardInput = editorHosted_ || !editorViewportInputRouting_;
    if (!allowViewportKeyboardInput && editorModeEnabled_ && editorViewportInputRouting_) {
        if (sceneViewportValid_) {
            double cursorX = 0.0, cursorY = 0.0;
            window_->GetCursorPos(cursorX, cursorY);
            allowViewportKeyboardInput = IsSceneViewportInputActive() || IsSceneViewportPointerInside(cursorX, cursorY);
        } else {
            allowViewportKeyboardInput = true;
        }
    }

    static bool lastLoggedState = false;
    if (allowViewportKeyboardInput != lastLoggedState) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
            "[INPUT] allowViewportKeyboardInput=", (allowViewportKeyboardInput ? 1 : 0),
            " editorHosted=", (editorHosted_ ? 1 : 0),
            " editorModeEnabled=", (editorModeEnabled_ ? 1 : 0),
            " editorViewportInputRouting=", (editorViewportInputRouting_ ? 1 : 0),
            " sceneViewportValid=", (sceneViewportValid_ ? 1 : 0));
        lastLoggedState = allowViewportKeyboardInput;
    }

    if (allowViewportKeyboardInput) {
        currentInput_.moveForward = window_->IsKeyPressed(GLFW_KEY_W);
        currentInput_.moveBackward = window_->IsKeyPressed(GLFW_KEY_S);
        currentInput_.moveRight = window_->IsKeyPressed(GLFW_KEY_D);
        currentInput_.moveLeft = window_->IsKeyPressed(GLFW_KEY_A);
        currentInput_.moveUp = window_->IsKeyPressed(GLFW_KEY_E) || window_->IsKeyPressed(GLFW_KEY_SPACE);
        currentInput_.moveDown = window_->IsKeyPressed(GLFW_KEY_Q) || window_->IsKeyPressed(GLFW_KEY_LEFT_CONTROL);

        bool anyMovement = currentInput_.moveForward || currentInput_.moveBackward ||
                          currentInput_.moveLeft || currentInput_.moveRight ||
                          currentInput_.moveUp || currentInput_.moveDown;
        if (anyMovement) {
            MarkDirty();
        }

        // Mouse Rotation — record input for fixed timestep update
        double mouseX = 0.0, mouseY = 0.0;
        window_->GetCursorPos(mouseX, mouseY);
        const bool rmbDown = window_->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
        
        bool pointerAllowed = !(editorModeEnabled_ && editorViewportInputRouting_) || !sceneViewportValid_ || IsSceneViewportPointerInside(mouseX, mouseY);
        if (isRotating_) pointerAllowed = true; // Lock focus while dragging

        currentInput_.mouseRotateX = 0.0f;
        currentInput_.mouseRotateY = 0.0f;
        if (!rmbDown || !pointerAllowed) {
            isRotating_ = false;
        } else {
            if (!isRotating_) {
                lastMouseX_ = mouseX;
                lastMouseY_ = mouseY;
                isRotating_ = true;
            } else {
                const float dx = static_cast<float>(mouseX - lastMouseX_);
                const float dy = static_cast<float>(lastMouseY_ - mouseY);
                lastMouseX_ = mouseX;
                lastMouseY_ = mouseY;
                if (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f) {
                    currentInput_.mouseRotateX += dx;
                    currentInput_.mouseRotateY += dy;
                    MarkDirty();
                }
            }
        }

        // Scroll Handling — record for fixed timestep update
        if (std::abs(accumulatedScrollDelta_) > 0.001f) {
            bool scrollAllowed = !(editorModeEnabled_ && editorViewportInputRouting_) || !sceneViewportValid_ || IsSceneViewportPointerInside(mouseX, mouseY);
            if (scrollAllowed) {
                currentInput_.scrollDelta += accumulatedScrollDelta_;
                MarkDirty();
            }
            accumulatedScrollDelta_ = 0.0f;
        }

        static const bool cameraTrace = CameraTraceEnabledByDefault();
        static uint64_t inputSeq = 0;
        ++inputSeq;
        const bool hasScroll = std::abs(currentInput_.scrollDelta) > 0.001f;
        if (cameraTrace && (inputSeq % 60 == 0 || hasScroll || !pointerAllowed)) {
            NC::LOGGING::Info(NC::LOGGING::Category::Input,
                "[CAMERA_TRACE][INPUT] seq=", inputSeq,
                " rendererDt=", deltaTime,
                " allow=", (allowViewportKeyboardInput ? 1 : 0),
                " W=", (currentInput_.moveForward ? 1 : 0),
                " A=", (currentInput_.moveLeft ? 1 : 0),
                " S=", (currentInput_.moveBackward ? 1 : 0),
                " D=", (currentInput_.moveRight ? 1 : 0),
                " Up=", (currentInput_.moveUp ? 1 : 0),
                " Down=", (currentInput_.moveDown ? 1 : 0),
                " mouse=", mouseX, ",", mouseY,
                " rmb=", (rmbDown ? 1 : 0),
                " pointerAllowed=", (pointerAllowed ? 1 : 0),
                " rotating=", (isRotating_ ? 1 : 0),
                " mouseDelta=", currentInput_.mouseRotateX, ",", currentInput_.mouseRotateY,
                " scroll=", currentInput_.scrollDelta);
        }
    }

    const bool lmbDown = window_->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    if (lmbDown && !lmbWasDown_)
        shiftHeldAtLastLeftClick_ = (window_->IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || window_->IsKeyPressed(GLFW_KEY_RIGHT_SHIFT));
    lmbWasDown_ = lmbDown;

    constexpr double actualDeltaThreshold = 0.1;
    if (deltaTime > actualDeltaThreshold) {
        NC::LOGGING::Warning(NC::LOGGING::Category::Graphics,
            "[PERF WARNING] Frame stall detected: deltaTime=", deltaTime, "s (clamped from larger value)");
    }
    HandleDroppedPaths();
    if (inputSystem_) {
        inputSystem_->Update();
    }
}

void DeferredRenderer::UpdateCameraFixed() {
    static const bool cameraTrace = CameraTraceEnabledByDefault();
    static uint64_t cameraSeq = 0;
    ++cameraSeq;

    const float shiftMultiplier = 4.0f;
    double cameraDt = deltaTime;
    if (cameraDt <= 0.0 || !std::isfinite(cameraDt)) {
        cameraDt = 1.0 / 60.0;
    }
    if (cameraDt > 1.0 / 30.0) {
        cameraDt = 1.0 / 30.0;
    }
    const float dt = static_cast<float>(cameraDt);
    const glm::dvec3 beforePos = camera.getPosition();
    const double beforeYaw = camera.getYaw();
    const double beforePitch = camera.getPitch();
    const bool anyMovement =
        currentInput_.moveForward || currentInput_.moveBackward ||
        currentInput_.moveLeft || currentInput_.moveRight ||
        currentInput_.moveUp || currentInput_.moveDown;
    const float pendingMouseX = currentInput_.mouseRotateX;
    const float pendingMouseY = currentInput_.mouseRotateY;
    const float pendingScroll = currentInput_.scrollDelta;

    if (currentInput_.moveForward)
        camera.processKeyboard(Camera::CameraMovement::FORWARD, dt, shiftMultiplier);
    if (currentInput_.moveBackward)
        camera.processKeyboard(Camera::CameraMovement::BACKWARD, dt, shiftMultiplier);
    if (currentInput_.moveRight)
        camera.processKeyboard(Camera::CameraMovement::RIGHT, dt, shiftMultiplier);
    if (currentInput_.moveLeft)
        camera.processKeyboard(Camera::CameraMovement::LEFT, dt, shiftMultiplier);
    if (currentInput_.moveUp)
        camera.processKeyboard(Camera::CameraMovement::UP, dt, shiftMultiplier);
    if (currentInput_.moveDown)
        camera.processKeyboard(Camera::CameraMovement::DOWN, dt, shiftMultiplier);

    if (std::abs(currentInput_.mouseRotateX) > 0.001f || std::abs(currentInput_.mouseRotateY) > 0.001f) {
        camera.processMouseMovement(currentInput_.mouseRotateX, currentInput_.mouseRotateY);
        currentInput_.mouseRotateX = 0.0f;
        currentInput_.mouseRotateY = 0.0f;
    }

    if (std::abs(currentInput_.scrollDelta) > 0.001f) {
        camera.processMouseScroll(currentInput_.scrollDelta);
        currentInput_.scrollDelta = 0.0f;
    }

    const glm::dvec3 afterPos = camera.getPosition();
    const glm::dvec3 deltaPos = afterPos - beforePos;
    const double moveLen = glm::length(deltaPos);
    if (cameraTrace && (cameraSeq % 60 == 0)) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
            "[CAMERA_TRACE][CAMERA_STEP] seq=", cameraSeq,
            " cameraDt=", dt,
            " rendererDt=", deltaTime,
            " movedInput=", (anyMovement ? 1 : 0),
            " pendingMouse=", pendingMouseX, ",", pendingMouseY,
            " pendingScroll=", pendingScroll,
            " posBefore=", beforePos.x, ",", beforePos.y, ",", beforePos.z,
            " posAfter=", afterPos.x, ",", afterPos.y, ",", afterPos.z,
            " delta=", deltaPos.x, ",", deltaPos.y, ",", deltaPos.z,
            " deltaLen=", moveLen,
            " yaw=", beforeYaw, "->", camera.getYaw(),
            " pitch=", beforePitch, "->", camera.getPitch());
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
            mapDropPath = path;
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

void DeferredRenderer::QueueDroppedPaths(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    bool hasMap = false;
    std::string lastMapPath;
    std::vector<std::string> nonMapPaths;
    nonMapPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        if (IsMapFilePath(path)) {
            hasMap = true;
            lastMapPath = path;
        } else {
            nonMapPaths.push_back(path);
        }
    }

    if (hasMap) {
        pendingDroppedPaths_.erase(
            std::remove_if(pendingDroppedPaths_.begin(), pendingDroppedPaths_.end(), IsMapFilePath),
            pendingDroppedPaths_.end());
        pendingDroppedPaths_.insert(pendingDroppedPaths_.end(), nonMapPaths.begin(), nonMapPaths.end());
        pendingDroppedPaths_.push_back(lastMapPath);
        NC::LOGGING::Log("[SCENE] Queued dropped map path=", lastMapPath);
    } else {
        pendingDroppedPaths_.insert(pendingDroppedPaths_.end(), paths.begin(), paths.end());
    }
    MarkDirty();
}

void DeferredRenderer::QueueDroppedMapLoad(const std::string& mapDropPath) {
    mapDropLoadStage_ = MapDropLoadStage::Queued;
    mapDropLoadPath_ = mapDropPath;
    mapDropLoadedMap_.reset();
    mapDropLoadStartSec_ = GetTimeSeconds();
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
            window_->SwapBuffers();
        }
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
        const double t0 = GetTimeSeconds();
        MapLoader loader;
        mapDropLoadedMap_ = loader.load_map(mapDropLoadPath_);
        mapDropLoadFileSec_ = GetTimeSeconds() - t0;

        if (!mapDropLoadedMap_) {
            mapDropLoadTotalSec_ = GetTimeSeconds() - mapDropLoadStartSec_;
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
        const double t0 = GetTimeSeconds();
        const MapData* droppedMapPtr = mapDropLoadedMap_.get();
        scene_->ImportMapAsEditableScene(droppedMapPtr, mapDropLoadPath_);
        SaveLastScenePath(mapDropLoadPath_);

        if (droppedMapPtr) {
            glm::vec3 pos(0.0f, 2.0f, 10.0f);
            glm::vec3 target(0.0f);
            if (!BuildMapCameraFit(droppedMapPtr->info, pos, target) && !droppedMapPtr->instances.empty()) {
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
                target = 0.5f * (minPos + maxPos);
                const float halfDiag = 0.5f * glm::length(glm::vec2(maxPos.x - minPos.x, maxPos.z - minPos.z));
                pos = target + glm::vec3(halfDiag * 0.8f, halfDiag * 0.5f, halfDiag * 0.8f);
            }
            camera.setPosition(pos);
            camera.lookAt(target);
            NC::LOGGING::Log("[SCENE] Camera fit target=(", target.x, ",", target.y, ",", target.z,
                             ") pos=(", camera.getPosition().x, ",", camera.getPosition().y, ",", camera.getPosition().z, ")");
        }
        mapDropLoadApplySec_ = GetTimeSeconds() - t0;
        mapDropLoadTotalSec_ = GetTimeSeconds() - mapDropLoadStartSec_;
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
        mapDropLoadStage_ = MapDropLoadStage::Idle;
        mapDropLoadPath_.clear();
        mapDropLoadStatus_.clear();
        mapDropLoadProgress_ = 0.0f;
        return false;
    }

    if (mapDropLoadStage_ == MapDropLoadStage::Complete ||
        mapDropLoadStage_ == MapDropLoadStage::Failed) {
        if (mapDropLoadStage_ == MapDropLoadStage::Complete) {
            mapDropLoadStage_ = MapDropLoadStage::Idle;
            mapDropLoadPath_.clear();
            mapDropLoadStatus_.clear();
            mapDropLoadProgress_ = 0.0f;
            MarkDirty();
            return false;
        }
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
    static const bool cameraTrace = CameraTraceEnabledByDefault();
    if (newWidth == width && newHeight == height) {
        if (cameraTrace) {
            NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                "[CAMERA_TRACE][RESIZE_SKIP] size=", newWidth, "x", newHeight);
        }
        return;
    }
    if (cameraTrace) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
            "[CAMERA_TRACE][RESIZE] old=", width, "x", height,
            " new=", newWidth, "x", newHeight);
    }
    NC::LOGGING::Log("[DEFERRED] Resize old=", width, "x", height, " new=", newWidth, "x", newHeight);
    if (device_ && window_) {
        device_->SetDefaultFramebuffer(window_->GetDefaultFramebuffer());
    }
    resizeFramebuffers(newWidth, newHeight);
    MarkDirty();
}

void DeferredRenderer::AppendModel(const std::string& path, const glm::vec3& pos,
    const glm::quat& rot, const glm::vec3& scale) {
    scene_->AppendModel(path, pos, rot, scale);
}

void DeferredRenderer::LoadMap(const MapData* map) {
    NC::LOGGING::Log("[DEFERRED] LoadMap ptr=", (map ? 1 : 0));
    scene_->LoadMap(map);
}

void DeferredRenderer::ReloadMapWithCurrentMode() {
    NC::LOGGING::Log("[DEFERRED] ReloadMapWithCurrentMode");
    scene_->ReloadMap();
}

void DeferredRenderer::SetCheckGLErrors(bool enabled) {
    NC::LOGGING::Log("[DEFERRED] SetCheckGLErrors ", (enabled ? 1 : 0));
    optCheckGLErrors = enabled;
}

bool DeferredRenderer::GetCheckGLErrors() const {
    return optCheckGLErrors;
}

void DeferredRenderer::SetRenderLog(bool enabled) {
    optRenderLOG = enabled;
}

void DeferredRenderer::AttachScene(SceneManager& scene) {
    scene_ = &scene;
}

SceneManager& DeferredRenderer::GetScene() {
    return *scene_;
}

Camera& DeferredRenderer::GetCamera() {
    return camera;
}

const Camera& DeferredRenderer::GetCamera() const {
    return camera;
}

void DeferredRenderer::SaveLastScenePath(const std::string& scenePath) {
    const char* envConfigPath = std::getenv("NDEVC_LAST_SCENE_CONFIG");
    std::string configPath = (envConfigPath && envConfigPath[0]) ? envConfigPath : "ndevc_last_scene.cfg";

    std::ofstream outFile(configPath, std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        NC::LOGGING::Error("[SCENE] Failed to open config file for writing: ", configPath);
        return;
    }

    outFile << scenePath << "\n";
    outFile.close();
    NC::LOGGING::Log("[SCENE] Saved last scene path to config: ", scenePath);
}

std::string DeferredRenderer::LoadLastScenePath() const {
    const char* envConfigPath = std::getenv("NDEVC_LAST_SCENE_CONFIG");
    std::string configPath = (envConfigPath && envConfigPath[0]) ? envConfigPath : "ndevc_last_scene.cfg";

    std::ifstream inFile(configPath);
    if (!inFile.is_open()) {
        NC::LOGGING::Log("[SCENE] No saved scene config found: ", configPath);
        return "";
    }

    std::string scenePath;
    if (std::getline(inFile, scenePath)) {
        inFile.close();
        if (!scenePath.empty()) {
            NC::LOGGING::Log("[SCENE] Loaded last scene path from config: ", scenePath);
            return scenePath;
        }
    }
    inFile.close();
    return "";
}

void DeferredRenderer::AutoLoadLastScene() {
    std::string lastScenePath = LoadLastScenePath();
    if (lastScenePath.empty()) {
        NC::LOGGING::Log("[SCENE] No last scene to auto-load");
        return;
    }

    std::ifstream sceneFile(lastScenePath);
    if (!sceneFile.is_open()) {
        NC::LOGGING::Error("[SCENE] Last scene file not found or inaccessible: ", lastScenePath);
        return;
    }
    sceneFile.close();

    MapLoader loader;
    std::unique_ptr<MapData> loadedMap = loader.load_map(lastScenePath);
    if (!loadedMap) {
        NC::LOGGING::Error("[SCENE] Failed to load last scene: ", lastScenePath);
        return;
    }

    scene_->ImportMapAsEditableScene(loadedMap.get(), lastScenePath);

    NC::LOGGING::Log("[SCENE] Auto-loaded last scene: ", lastScenePath);
}
