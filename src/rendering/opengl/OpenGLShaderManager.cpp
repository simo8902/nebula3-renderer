// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLShaderManager.h"
#include "Core/Errors.h"
#include "Core/Logger.h"
#include "Rendering/OpenGL/OpenGLShader.h"
#include <fstream>
#include <system_error>

namespace NDEVC::Graphics::OpenGL {

OpenGLShaderManager::OpenGLShaderManager() {
    NC::LOGGING::Log("[GL_SHADER_MGR] ctor");

    auto CreateShader = [this](const std::string& name, const char* vert, const char* frag) {
        NC::LOGGING::Log("[GL_SHADER_MGR] CreateShader name=", name, " V=", vert, " F=", frag);
        auto shader = std::make_shared<OpenGLShader>(vert, frag);
        if (!shader->IsValid()) {
            throw NC::Errors::LoggedRuntimeError(name + " shader init failed");
        }
        shaders_[name] = shader;
        const GLuint programId = shader->GetNativeHandle()
            ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
            : 0;
        NC::LOGGING::Log("[GL_SHADER_MGR] Shader ready name=", name, " program=", programId);
    };

    auto CreateShaderWithDefines = [this](const std::string& name, const char* vert, const char* frag,
                                          const std::string& vertDefines, const std::string& fragDefines) {
        NC::LOGGING::Log("[GL_SHADER_MGR] CreateShaderWithDefines name=", name, " V=", vert, " F=", frag,
                         " VDef=", vertDefines.size(), " FDef=", fragDefines.size());
        auto shader = std::make_shared<OpenGLShader>(vert, frag, vertDefines, fragDefines);
        if (!shader->IsValid()) {
            throw NC::Errors::LoggedRuntimeError(name + " shader init failed");
        }
        shaders_[name] = shader;
        const GLuint programId = shader->GetNativeHandle()
            ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
            : 0;
        NC::LOGGING::Log("[GL_SHADER_MGR] Shader ready name=", name, " program=", programId);
    };

    CreateShader("NDEVCdeferred", SOURCE_DIR "/shaders/NDEVCdeferred.vert", SOURCE_DIR "/shaders/NDEVCdeferred.frag");
    CreateShaderWithDefines(
        "NDEVCdeferred_bindless",
        SOURCE_DIR "/shaders/NDEVCdeferred.vert",
        SOURCE_DIR "/shaders/NDEVCdeferred.frag",
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("standard", SOURCE_DIR "/shaders/standard.vert", SOURCE_DIR "/shaders/standard.frag");
    CreateShader("particle", SOURCE_DIR "/shaders/particle.vert", SOURCE_DIR "/shaders/particle.frag");
    CreateShader("environment", SOURCE_DIR "/shaders/environment.vert", SOURCE_DIR "/shaders/environment.frag");
    CreateShader("environmentAlpha", SOURCE_DIR "/shaders/environment.vert", SOURCE_DIR "/shaders/environment_alpha.frag");
    CreateShaderWithDefines(
        "environmentAlpha_bindless",
        SOURCE_DIR "/shaders/environment.vert",
        SOURCE_DIR "/shaders/environment_alpha.frag",
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShaderWithDefines(
        "simplelayer",
        SOURCE_DIR "/shaders/simplelayer.vert",
        SOURCE_DIR "/shaders/simplelayer.frag",
        "#define SKINNING_MODE 2\n#define PASS 1\n",
        "#define PASS 3\n");
    CreateShaderWithDefines(
        "simplelayer_gbuffer",
        SOURCE_DIR "/shaders/simplelayer.vert",
        SOURCE_DIR "/shaders/simplelayer.frag",
        "#define SKINNING_MODE 2\n#define PASS 2\n",
        "#define PASS 5\n");
    CreateShaderWithDefines(
        "simplelayer_gbuffer_clip",
        SOURCE_DIR "/shaders/simplelayer.vert",
        SOURCE_DIR "/shaders/simplelayer.frag",
        "#define SKINNING_MODE 2\n#define PASS 2\n",
        "#define PASS 4\n");
    CreateShaderWithDefines(
        "simplelayer_shadow",
        SOURCE_DIR "/shaders/simplelayer.vert",
        SOURCE_DIR "/shaders/simplelayer.frag",
        "#define SKINNING_MODE 2\n#define PASS 3\n",
        "#define PASS 6\n");
    CreateShaderWithDefines(
        "simplelayer_depth",
        SOURCE_DIR "/shaders/simplelayer.vert",
        SOURCE_DIR "/shaders/simplelayer.frag",
        "#define SKINNING_MODE 2\n#define PASS 4\n",
        "#define PASS 7\n");
    CreateShader("postalphaunlit", SOURCE_DIR "/shaders/postalphaunlit.vert", SOURCE_DIR "/shaders/postalphaunlit.frag");
    CreateShader("NDEVCdecal_mesh", SOURCE_DIR "/shaders/NDEVCdecal_mesh.vert", SOURCE_DIR "/shaders/NDEVCdecal_mesh.frag");
    CreateShaderWithDefines(
        "NDEVCdecal_mesh_bindless",
        SOURCE_DIR "/shaders/NDEVCdecal_mesh.vert",
        SOURCE_DIR "/shaders/NDEVCdecal_mesh.frag",
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("refraction", SOURCE_DIR "/shaders/refraction.vert", SOURCE_DIR "/shaders/refraction.frag");
    CreateShaderWithDefines(
        "refraction_bindless",
        SOURCE_DIR "/shaders/refraction.vert",
        SOURCE_DIR "/shaders/refraction.frag",
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("water", SOURCE_DIR "/shaders/water.vert", SOURCE_DIR "/shaders/water.frag");
    CreateShaderWithDefines(
        "water_bindless",
        SOURCE_DIR "/shaders/water.vert",
        SOURCE_DIR "/shaders/water.frag",
        "#define BINDLESS 1\n",
        "#define BINDLESS 1\n");
    CreateShader("lighting", SOURCE_DIR "/shaders/lighting.vert", SOURCE_DIR "/shaders/lighting.frag");
    CreateShader("lightCompose", SOURCE_DIR "/shaders/lighting.vert", SOURCE_DIR "/shaders/lightCompose.frag");
    CreateShader("lightComposition", SOURCE_DIR "/shaders/lighting.vert", SOURCE_DIR "/shaders/lightComposition.frag");
    CreateShader("pointLight", SOURCE_DIR "/shaders/pointLight.vert", SOURCE_DIR "/shaders/pointLight.frag");
    CreateShader("lightShadows", SOURCE_DIR "/shaders/lightShadows.vert", SOURCE_DIR "/shaders/lightShadows.frag");
    CreateShader("blit", SOURCE_DIR "/shaders/blit.vert", SOURCE_DIR "/shaders/blit.frag");

    NC::LOGGING::Log("[GL_SHADER_MGR] initialized shaderCount=", shaders_.size());

    std::filesystem::path shadersPath = SOURCE_DIR "/shaders/";
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
    std::unordered_set<std::string> todo;
    {
        std::lock_guard<std::mutex> lock(shaderMutex_);
        todo.swap(pendingReloads_);
    }
    NC::LOGGING::Log("[GL_SHADER_MGR] ProcessPendingReloads count=", todo.size());
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
    std::lock_guard<std::mutex> lock(shaderMutex_);

    for (auto& [name, shader] : shaders_) {
        auto glShader = std::dynamic_pointer_cast<OpenGLShader>(shader);
        if (!glShader) continue;

        auto checkFile = [&](const std::string& path) {
            if (path.empty()) return;
            std::error_code ec;
            const std::filesystem::path fsPath(path);
            if (!std::filesystem::exists(fsPath, ec)) {
                if (ec) {
                    NC::LOGGING::Error("[GL_SHADER_MGR] exists() failed path=", path, " err=", ec.message());
                } else {
                    NC::LOGGING::Error("[GL_SHADER_MGR] Shader file missing path=", path);
                }
                return;
            }
            if (ec) {
                NC::LOGGING::Error("[GL_SHADER_MGR] exists() error path=", path, " err=", ec.message());
                return;
            }

            const auto ftime = std::filesystem::last_write_time(fsPath, ec);
            if (ec) {
                NC::LOGGING::Error("[GL_SHADER_MGR] last_write_time() failed path=", path, " err=", ec.message());
                return;
            }

            auto it = fileTimestamps_.find(path);
            if (it == fileTimestamps_.end() || it->second != ftime) {
                fileTimestamps_[path] = ftime;
                pendingReloads_.insert(name);
                NC::LOGGING::Log("[GL_SHADER_MGR] File changed name=", name, " path=", path);
            }
        };

        checkFile(glShader->GetPaths().vertex);
        checkFile(glShader->GetPaths().fragment);
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
        auto& shader = shaders_.at(name);
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
    std::lock_guard<std::mutex> lock(shaderMutex_);
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
