// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLShaderManager.h"
#include "Core/Errors.h"
#include "Core/GlobalState.h"
#include "Core/Logger.h"
#include "Core/VFS.h"
#include "Rendering/OpenGL/OpenGLShader.h"
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace NDEVC::Graphics::OpenGL {
namespace {

std::string ResolveShaderBaseDir() {
    const char* envBase = std::getenv("NDEVC_SOURCE_DIR");
    if (envBase && envBase[0]) {
        return std::string(envBase);
    }
    return std::string(SOURCE_DIR);
}

std::string BuildShaderPath(const std::string& shaderBaseDir, const char* fileName) {
    return (std::filesystem::path(shaderBaseDir) / "shaders" / fileName).string();
}

bool CameraTraceEnabledByDefault() {
    const char* value = std::getenv("NDEVC_CAMERA_TRACE");
    return value == nullptr || value[0] != '0';
}

} // namespace

OpenGLShaderManager::OpenGLShaderManager() {
    const std::string shaderBaseDir = ResolveShaderBaseDir();
    NC::LOGGING::Log("[GL_SHADER_MGR] ctor shaderBaseDir=", shaderBaseDir);

    auto ShaderSourceAvailable = [](const std::string& path) {
        return std::filesystem::exists(path) || NC::VFS::Instance().Exists(path);
    };

    auto CreateShader = [this, ShaderSourceAvailable](const std::string& name,
                                                       const std::string& vert,
                                                       const std::string& frag,
                                                       bool required = false) {
        if (!ShaderSourceAvailable(vert) || !ShaderSourceAvailable(frag)) {
            if (required) {
                throw NC::Errors::LoggedRuntimeError(name + " shader source missing");
            }
            NC::LOGGING::Warning("[GL_SHADER_MGR] Skipping missing shader name=", name);
            return;
        }
        auto shader = std::make_shared<OpenGLShader>(vert.c_str(), frag.c_str());
        shaders_[name] = shader;

        const GLuint programId = shader->GetNativeHandle()
            ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
            : 0;
        NC::LOGGING::Log("[GL_SHADER_MGR] Shader ready name=", name, " program=", programId);
    };

    auto CreateShaderWithDefines = [this, ShaderSourceAvailable](const std::string& name,
                                                                 const std::string& vert,
                                                                 const std::string& frag,
                                                                 const std::string& vertDefines,
                                                                 const std::string& fragDefines,
                                                                 bool required = false) {
        if (!ShaderSourceAvailable(vert) || !ShaderSourceAvailable(frag)) {
            if (required) {
                throw NC::Errors::LoggedRuntimeError(name + " shader source missing");
            }
            NC::LOGGING::Warning("[GL_SHADER_MGR] Skipping missing shader name=", name);
            return;
        }
        NC::LOGGING::Log("[GL_SHADER_MGR] CreateShaderWithDefines name=", name, " V=", vert, " F=", frag,
                         " VDef=", vertDefines.size(), " FDef=", fragDefines.size());
        auto shader = std::make_shared<OpenGLShader>(vert.c_str(), frag.c_str(), vertDefines, fragDefines);
        if (!shader->IsValid()) {
            throw NC::Errors::LoggedRuntimeError(name + " shader init failed");
        }
        shaders_[name] = shader;
        const GLuint programId = shader->GetNativeHandle()
            ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
            : 0;
        NC::LOGGING::Log("[GL_SHADER_MGR] Shader ready name=", name, " program=", programId);
    };

    CreateShaderWithDefines(
        "NDEVCdeferred",
        BuildShaderPath(shaderBaseDir, "standard.vert"),
        BuildShaderPath(shaderBaseDir, "standard.frag"),
        "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_BUMP_GBUFFER 1\n",
        "#define STANDARD_PASS_BUMP_GBUFFER 1\n",
        true);
    CreateShaderWithDefines(
        "NDEVCdeferred_bindless",
        BuildShaderPath(shaderBaseDir, "standard.vert"),
        BuildShaderPath(shaderBaseDir, "standard.frag"),
        "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_BUMP_GBUFFER 1\n",
        "#define STANDARD_PASS_BUMP_GBUFFER 1\n",
        true);
    CreateShaderWithDefines(
        "NDEVCdeferred_alpha_clip",
        BuildShaderPath(shaderBaseDir, "standard.vert"),
        BuildShaderPath(shaderBaseDir, "standard.frag"),
        "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_BUMP_GBUFFER 1\n#define STANDARD_ALPHA_TEST 1\n",
        "#define STANDARD_PASS_BUMP_GBUFFER 1\n#define STANDARD_ALPHA_TEST 1\n",
        true);
    CreateShaderWithDefines(
        "NDEVCsimplelayer",
        BuildShaderPath(shaderBaseDir, "NDEVCsimplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "NDEVCsimplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 5\n",
        "#define PASS 5\n",
        true);
    CreateShaderWithDefines(
        "NDEVCsimplelayer_alpha_clip",
        BuildShaderPath(shaderBaseDir, "NDEVCsimplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "NDEVCsimplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 4\n#define ALPHA_CLIP 1\n",
        "#define PASS 4\n#define ALPHA_CLIP 1\n",
        true);
    CreateShader("standard_gbuffer_compose",
                 BuildShaderPath(shaderBaseDir, "standard_gbuffer_compose.vert"),
                 BuildShaderPath(shaderBaseDir, "standard_gbuffer_compose.frag"),
                 true);
    CreateShader("standard_light_accum",
                 BuildShaderPath(shaderBaseDir, "standard_gbuffer_compose.vert"),
                 BuildShaderPath(shaderBaseDir, "standard_light_accum.frag"),
                 true);
    CreateShader("standard_present",
                 BuildShaderPath(shaderBaseDir, "standard_gbuffer_compose.vert"),
                 BuildShaderPath(shaderBaseDir, "standard_present.frag"),
                 true);
    CreateShader("compose",
                 BuildShaderPath(shaderBaseDir, "compose.vert"),
                 BuildShaderPath(shaderBaseDir, "compose.frag"),
                 true);
    CreateShader("standard",
                 BuildShaderPath(shaderBaseDir, "standard.vert"),
                 BuildShaderPath(shaderBaseDir, "standard.frag"));
    const std::string standardVert = BuildShaderPath(shaderBaseDir, "standard.vert");
    const std::string standardFrag = BuildShaderPath(shaderBaseDir, "standard.frag");
    auto CreateStandardVariant = [&](const std::string& name,
                                     const std::string& vertDefines,
                                     const std::string& fragDefines) {
        CreateShaderWithDefines(name, standardVert, standardFrag, vertDefines, fragDefines);
    };
    CreateStandardVariant("standard_highlight",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_HIGHLIGHT 1\n",
                          "#define STANDARD_PASS_HIGHLIGHT 1\n");
    CreateStandardVariant("standard_diffuse_fog_alpha",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_DIFFUSE_FOG_ALPHA 1\n",
                          "#define STANDARD_PASS_DIFFUSE_FOG_ALPHA 1\n");
    CreateStandardVariant("standard_lit_composite",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_LIT_COMPOSITE 1\n",
                          "#define STANDARD_PASS_LIT_COMPOSITE 1\n");
    CreateStandardVariant("standard_lit_composite_alpha",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_LIT_COMPOSITE 1\n",
                          "#define STANDARD_PASS_LIT_COMPOSITE 1\n#define STANDARD_ALPHA_MODULATE 1\n");
    CreateStandardVariant("standard_lit_composite_alpha_test",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_LIT_COMPOSITE 1\n#define STANDARD_ALPHA_TEST 1\n",
                          "#define STANDARD_PASS_LIT_COMPOSITE 1\n#define STANDARD_ALPHA_TEST 1\n");
    CreateStandardVariant("standard_emissive_tint_fog",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_EMISSIVE_TINT_FOG 1\n",
                          "#define STANDARD_PASS_EMISSIVE_TINT_FOG 1\n");
    CreateStandardVariant("standard_bump_gbuffer",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_BUMP_GBUFFER 1\n",
                          "#define STANDARD_PASS_BUMP_GBUFFER 1\n");
    CreateStandardVariant("standard_bump_gbuffer_alpha_test",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_BUMP_GBUFFER 1\n#define STANDARD_ALPHA_TEST 1\n",
                          "#define STANDARD_PASS_BUMP_GBUFFER 1\n#define STANDARD_ALPHA_TEST 1\n");
    CreateStandardVariant("standard_projective_depth",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_PROJECTIVE_DEPTH 1\n",
                          "#define STANDARD_PASS_PROJECTIVE_DEPTH 1\n");
    CreateStandardVariant("standard_projective_depth_alpha_test",
                          "#define STANDARD_GEOMETRY_STATIC 1\n#define STANDARD_PASS_PROJECTIVE_DEPTH 1\n#define STANDARD_ALPHA_TEST 1\n",
                          "#define STANDARD_PASS_PROJECTIVE_DEPTH 1\n#define STANDARD_ALPHA_TEST 1\n");
    CreateShader("particle",
                 BuildShaderPath(shaderBaseDir, "particle.vert"),
                 BuildShaderPath(shaderBaseDir, "particle.frag"));
    CreateShader("environment",
                 BuildShaderPath(shaderBaseDir, "environment.vert"),
                 BuildShaderPath(shaderBaseDir, "environment.frag"));
    CreateShader("environmentAlpha",
                 BuildShaderPath(shaderBaseDir, "environment.vert"),
                 BuildShaderPath(shaderBaseDir, "environment_alpha.frag"));
    CreateShaderWithDefines(
        "environmentAlpha_bindless",
        BuildShaderPath(shaderBaseDir, "environment.vert"),
        BuildShaderPath(shaderBaseDir, "environment_alpha.frag"),
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShaderWithDefines(
        "simplelayer",
        BuildShaderPath(shaderBaseDir, "simplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "simplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 1\n",
        "#define PASS 3\n");
    CreateShaderWithDefines(
        "simplelayer_gbuffer",
        BuildShaderPath(shaderBaseDir, "simplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "simplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 2\n",
        "#define PASS 5\n");
    CreateShaderWithDefines(
        "simplelayer_gbuffer_clip",
        BuildShaderPath(shaderBaseDir, "simplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "simplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 2\n",
        "#define PASS 4\n");
    CreateShaderWithDefines(
        "simplelayer_shadow",
        BuildShaderPath(shaderBaseDir, "simplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "simplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 3\n",
        "#define PASS 6\n");
    CreateShaderWithDefines(
        "simplelayer_depth",
        BuildShaderPath(shaderBaseDir, "simplelayer.vert"),
        BuildShaderPath(shaderBaseDir, "simplelayer.frag"),
        "#define SKINNING_MODE 2\n#define PASS 4\n",
        "#define PASS 7\n");
    CreateShader("postalphaunlit",
                 BuildShaderPath(shaderBaseDir, "postalphaunlit.vert"),
                 BuildShaderPath(shaderBaseDir, "postalphaunlit.frag"));
    CreateShader("NDEVCdecal_mesh",
                 BuildShaderPath(shaderBaseDir, "NDEVCdecal_mesh.vert"),
                 BuildShaderPath(shaderBaseDir, "NDEVCdecal_mesh.frag"));
    CreateShaderWithDefines(
        "NDEVCdecal_mesh_bindless",
        BuildShaderPath(shaderBaseDir, "NDEVCdecal_mesh.vert"),
        BuildShaderPath(shaderBaseDir, "NDEVCdecal_mesh.frag"),
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("refraction",
                 BuildShaderPath(shaderBaseDir, "refraction.vert"),
                 BuildShaderPath(shaderBaseDir, "refraction.frag"));
    CreateShaderWithDefines(
        "refraction_bindless",
        BuildShaderPath(shaderBaseDir, "refraction.vert"),
        BuildShaderPath(shaderBaseDir, "refraction.frag"),
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("water",
                 BuildShaderPath(shaderBaseDir, "water.vert"),
                 BuildShaderPath(shaderBaseDir, "water.frag"));
    CreateShaderWithDefines(
        "water_bindless",
        BuildShaderPath(shaderBaseDir, "water.vert"),
        BuildShaderPath(shaderBaseDir, "water.frag"),
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("lighting",
                 BuildShaderPath(shaderBaseDir, "lighting.vert"),
                 BuildShaderPath(shaderBaseDir, "lighting.frag"));
    CreateShader("lightCompose",
                 BuildShaderPath(shaderBaseDir, "lighting.vert"),
                 BuildShaderPath(shaderBaseDir, "lightCompose.frag"));
    CreateShader("lightComposition",
                 BuildShaderPath(shaderBaseDir, "lighting.vert"),
                 BuildShaderPath(shaderBaseDir, "lightComposition.frag"));
    CreateShader("pointLight",
                 BuildShaderPath(shaderBaseDir, "pointLight.vert"),
                 BuildShaderPath(shaderBaseDir, "pointLight.frag"));
    CreateShader("lightShadows",
                 BuildShaderPath(shaderBaseDir, "lightShadows.vert"),
                 BuildShaderPath(shaderBaseDir, "lightShadows.frag"));
    CreateShader("blit",
                 BuildShaderPath(shaderBaseDir, "blit.vert"),
                 BuildShaderPath(shaderBaseDir, "blit.frag"));

    NC::LOGGING::Log("[GL_SHADER_MGR] initialized shaderCount=", shaders_.size());

    std::filesystem::path shadersPath = std::filesystem::path(shaderBaseDir) / "shaders";
    searchPaths_.emplace_back(shadersPath);
}

OpenGLShaderManager::~OpenGLShaderManager() {
    NC::LOGGING::Log("[GL_SHADER_MGR] dtor");
    Shutdown();
}

void OpenGLShaderManager::Initialize() {
    running_ = true;
    NC::LOGGING::Log("[GL_SHADER_MGR] Initialize watcher");
    fileWatcher_ = std::make_unique<std::thread>(&OpenGLShaderManager::FileWatchLoop, this);
}

void OpenGLShaderManager::Shutdown() {
    running_ = false;
    NC::LOGGING::Log("[GL_SHADER_MGR] Shutdown watcher");
    if(fileWatcher_ && fileWatcher_->joinable()) {
        fileWatcher_->join();
    }
}

void OpenGLShaderManager::ProcessPendingReloads() {
    static const bool cameraTrace = CameraTraceEnabledByDefault();
    std::unordered_set<std::string> todo;
    size_t remaining = 0;
    {
        std::lock_guard<std::mutex> lock(shaderMutex_);
        auto it = pendingReloads_.begin();
        if (it != pendingReloads_.end()) {
            todo.insert(*it);
            pendingReloads_.erase(it);
        }
        remaining = pendingReloads_.size();
    }
    static bool loggedIdleOnce = false;
    if (todo.empty()) {
        if (!loggedIdleOnce) {
            NC::LOGGING::Log("[GL_SHADER_MGR] ProcessPendingReloads count=0");
            loggedIdleOnce = true;
        }
        return;
    }

    loggedIdleOnce = false;
    NC::LOGGING::Log("[GL_SHADER_MGR] ProcessPendingReloads count=", todo.size());
    if (cameraTrace) {
        for (const auto& name : todo) {
            NC::LOGGING::Info(NC::LOGGING::Category::Shader,
                "[CAMERA_TRACE][SHADER_RELOAD] name=", name,
                " remainingQueued=", remaining);
        }
    }
    for (auto& name : todo) ReloadShader(name);
}

void OpenGLShaderManager::FileWatchLoop() {
    NC::LOGGING::Log("[GL_SHADER_MGR] FileWatchLoop start");
    while(running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        UpdateFileMonitoring();
    }
    NC::LOGGING::Log("[GL_SHADER_MGR] FileWatchLoop end");
}

void OpenGLShaderManager::UpdateFileMonitoring() {
    struct WatchedShaderPath {
        std::string name;
        std::string path;
    };

    std::vector<WatchedShaderPath> watchedPaths;
    std::unordered_map<std::string, std::filesystem::file_time_type> timestampsSnapshot;
    {
        std::lock_guard<std::mutex> lock(shaderMutex_);
        watchedPaths.reserve(shaders_.size() * 2);
        for (const auto& [name, shader] : shaders_) {
            auto glShader = std::dynamic_pointer_cast<OpenGLShader>(shader);
            if (!glShader) continue;
            const auto& paths = glShader->GetPaths();
            if (!paths.vertex.empty()) {
                watchedPaths.push_back({name, paths.vertex});
            }
            if (!paths.fragment.empty()) {
                watchedPaths.push_back({name, paths.fragment});
            }
        }
        timestampsSnapshot = fileTimestamps_;
    }

    std::unordered_map<std::string, std::filesystem::file_time_type> observedTimestamps;
    std::unordered_map<std::string, std::filesystem::file_time_type> changedTimestamps;
    std::unordered_set<std::string> changedShaders;
    for (const auto& watched : watchedPaths) {
        std::error_code ec;
        const std::filesystem::path fsPath(watched.path);
        if (!std::filesystem::exists(fsPath, ec)) {
            if (ec) {
                NC::LOGGING::Error("[GL_SHADER_MGR] exists() failed path=", watched.path, " err=", ec.message());
            } else {
                NC::LOGGING::Error("[GL_SHADER_MGR] Shader file missing path=", watched.path);
            }
            continue;
        }
        if (ec) {
            NC::LOGGING::Error("[GL_SHADER_MGR] exists() error path=", watched.path, " err=", ec.message());
            continue;
        }

        const auto ftime = std::filesystem::last_write_time(fsPath, ec);
        if (ec) {
            NC::LOGGING::Error("[GL_SHADER_MGR] last_write_time() failed path=", watched.path, " err=", ec.message());
            continue;
        }

        auto snapshotIt = timestampsSnapshot.find(watched.path);
        if (snapshotIt == timestampsSnapshot.end()) {
            observedTimestamps[watched.path] = ftime;
        } else if (snapshotIt->second != ftime) {
            changedTimestamps[watched.path] = ftime;
            changedShaders.insert(watched.name);
            NC::LOGGING::Log("[GL_SHADER_MGR] File changed name=", watched.name, " path=", watched.path);
        }
    }

    if (!observedTimestamps.empty() || !changedShaders.empty()) {
        std::lock_guard<std::mutex> lock(shaderMutex_);
        for (const auto& [path, ftime] : observedTimestamps) {
            fileTimestamps_[path] = ftime;
        }
        for (const auto& [path, ftime] : changedTimestamps) {
            fileTimestamps_[path] = ftime;
        }
        if (watcherPrimed_) {
            pendingReloads_.insert(changedShaders.begin(), changedShaders.end());
            if (CameraTraceEnabledByDefault()) {
                NC::LOGGING::Info(NC::LOGGING::Category::Shader,
                    "[CAMERA_TRACE][SHADER_WATCH] changedShaders=", changedShaders.size(),
                    " pendingQueued=", pendingReloads_.size(),
                    " observedNewPaths=", observedTimestamps.size());
            }
        } else {
            watcherPrimed_ = true;
            if (CameraTraceEnabledByDefault()) {
                NC::LOGGING::Info(NC::LOGGING::Category::Shader,
                    "[CAMERA_TRACE][SHADER_WATCH_PRIME] observedPaths=", observedTimestamps.size(),
                    " ignoredInitialChanges=", changedShaders.size());
            }
        }
    } else {
        std::lock_guard<std::mutex> lock(shaderMutex_);
        watcherPrimed_ = true;
    }
}

void OpenGLShaderManager::ScanDirectory(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        NC::LOGGING::Error("[GL_SHADER_MGR] Scan invalid directory: ", fs::absolute(directory).string());
        return;
    }

    try {
        std::unordered_map<std::string, std::pair<std::filesystem::path, std::filesystem::path>> shaderPairs;

        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                const auto& filePath = entry.path();
                const auto& extension = filePath.extension();

                if (extension == ".vert" || extension == ".frag") {
                    std::string name = filePath.stem().string();
                    if (extension == ".vert") {
                        shaderPairs[name].first = filePath;
                    } else if (extension == ".frag") {
                        shaderPairs[name].second = filePath;
                    }
                } else if (extension == ".glsl") {
                    LoadShader(filePath);
                }
            }
        }

        for (const auto& [name, paths] : shaderPairs) {
            if (!paths.first.empty() && !paths.second.empty()) {
                LoadShader(paths.first);
            }
        }

    } catch (const fs::filesystem_error& e) {
        NC::LOGGING::Error("[GL_SHADER_MGR] Scan error: ", e.what());
    }
}

std::string OpenGLShaderManager::ReadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw NC::Errors::LoggedRuntimeError("Could not open file: " + filename);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void OpenGLShaderManager::LoadShader(const std::filesystem::path& path) {
   try {
        std::string baseName = path.stem().string();
        std::lock_guard<std::mutex> lock(shaderMutex_);
        NC::LOGGING::Log("[GL_SHADER_MGR] LoadShader base=", baseName, " path=", path.string());

        if (shaders_.find(baseName) != shaders_.end()) {
            ReloadShader(baseName);
            return;
        }

        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            throw NC::Errors::LoggedRuntimeError("Shader file does not exist or is not a regular file: " + path.string());
        }

        if (auto extension = path.extension().string(); extension == ".frag" || extension == ".vert") {
            std::filesystem::path vertPath = path.parent_path() / (baseName + ".vert");
            std::filesystem::path fragPath = path.parent_path() / (baseName + ".frag");

            if (!std::filesystem::exists(vertPath)) {
                throw NC::Errors::LoggedRuntimeError("Vertex shader file does not exist: " + vertPath.string());
            }
            if (!std::filesystem::exists(fragPath)) {
                throw NC::Errors::LoggedRuntimeError("Fragment shader file does not exist: " + fragPath.string());
            }

            auto shader = std::make_shared<OpenGLShader>(vertPath.string().c_str(), fragPath.string().c_str());

            if (shader->IsValid()) {
                shaders_[baseName] = shader;
                std::error_code ecVert;
                std::error_code ecFrag;
                const auto vertTime = std::filesystem::last_write_time(vertPath, ecVert);
                const auto fragTime = std::filesystem::last_write_time(fragPath, ecFrag);
                if (ecVert) {
                    NC::LOGGING::Error("[GL_SHADER_MGR] last_write_time() failed path=", vertPath.string(), " err=", ecVert.message());
                } else {
                    fileTimestamps_[vertPath.string()] = vertTime;
                }
                if (ecFrag) {
                    NC::LOGGING::Error("[GL_SHADER_MGR] last_write_time() failed path=", fragPath.string(), " err=", ecFrag.message());
                } else {
                    fileTimestamps_[fragPath.string()] = fragTime;
                }
                NC::LOGGING::Log("[GL_SHADER_MGR] LoadShader pair ready name=", baseName);
            } else {
                throw NC::Errors::LoggedRuntimeError("Failed to compile combined shader: " + baseName);
            }
        } else if (extension == ".glsl") {
            auto shader = std::make_shared<OpenGLShader>(path.string().c_str(), path.string().c_str());

            if (shader->IsValid()) {
                shaders_[baseName] = shader;
                std::error_code ecTime;
                const auto ftime = std::filesystem::last_write_time(path, ecTime);
                if (ecTime) {
                    NC::LOGGING::Error("[GL_SHADER_MGR] last_write_time() failed path=", path.string(), " err=", ecTime.message());
                } else {
                    fileTimestamps_[path.string()] = ftime;
                }
                NC::LOGGING::Log("[GL_SHADER_MGR] LoadShader glsl ready name=", baseName);
            } else {
                throw NC::Errors::LoggedRuntimeError("Failed to compile GLSL shader: " + baseName);
            }
        } else {
            throw NC::Errors::LoggedRuntimeError("Unsupported shader file extension: " + extension);
        }
    } catch (const std::exception& e) {
        NC::LOGGING::Error("[GL_SHADER_MGR] Shader load error: ", e.what());
    }
}

void OpenGLShaderManager::ReloadAll() {
    std::lock_guard<std::mutex> lock(shaderMutex_);
    NC::LOGGING::Log("[GL_SHADER_MGR] ReloadAll count=", shaders_.size());
    for(auto& [name, _] : shaders_) {
        ReloadShader(name);
    }
}

void OpenGLShaderManager::ReloadShader(const std::string& name) {
    try {
        std::shared_ptr<IShader> shader;
        {
            std::lock_guard<std::mutex> lock(shaderMutex_);
            auto it = shaders_.find(name);
            if (it == shaders_.end()) {
                NC::LOGGING::Warning("[GL_SHADER_MGR] Cannot reload, shader not found: ", name);
                return;
            }
            shader = it->second;
        }

        auto glShader = std::dynamic_pointer_cast<OpenGLShader>(shader);
        if (glShader) {
            glShader->Reload();
        } else {
            shader->Reload();
        }
        NC::LOGGING::Log("[GL_SHADER_MGR] ReloadShader success name=", name);
    } catch(const std::exception& e) {
        NC::LOGGING::Error("[GL_SHADER_MGR] Shader reload failed name=", name, " err=", e.what());
    }
}

void OpenGLShaderManager::HandleFileDrop(const std::vector<std::string>& paths) {
    NC::LOGGING::Log("[GL_SHADER_MGR] HandleFileDrop count=", paths.size());
    for(const auto& path : paths) {
        NC::LOGGING::Log("[GL_SHADER_MGR] HandleFileDrop path=", path);
        LoadShader(path);
    }
}

std::shared_ptr<IShader> OpenGLShaderManager::GetShader(const std::string& name) {
    std::lock_guard<std::mutex> lock(shaderMutex_);
    auto it = shaders_.find(name);
    if (it == shaders_.end()) {
        NC::LOGGING::Warning("[GL_SHADER_MGR] GetShader miss name=", name);
        return nullptr;
    }
    return it->second;
}

}
