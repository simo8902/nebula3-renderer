// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "OpenGLShaderManager.h"
#include "OpenGLShader.h"
#include "../../Shader.h"
#include <iostream>
#include <fstream>
#include <system_error>

namespace NDEVC::Graphics::OpenGL {

OpenGLShaderManager::OpenGLShaderManager() {
    std::cerr << "OpenGLShaderManager()" << std::endl;

    auto CreateShader = [this](const std::string& name, const char* vert, const char* frag) {
        auto shader = std::make_shared<OpenGLShader>(vert, frag);
        if (!shader->IsValid()) {
            std::cerr << "[FATAL] " << name << " shader failed to compile/link\n";
            throw std::runtime_error(name + " shader init failed");
        }
        shaders_[name] = shader;
        std::cout << "[ShaderManager] " << name << " program ID: " << (uintptr_t)shader->GetNativeHandle() << "\n";
    };

    CreateShader("NDEVCdeferred", SOURCE_DIR "/shaders/NDEVCdeferred.vert", SOURCE_DIR "/shaders/NDEVCdeferred.frag");
    CreateShader("particle", SOURCE_DIR "/shaders/particle.vert", SOURCE_DIR "/shaders/particle.frag");
    CreateShader("environment", SOURCE_DIR "/shaders/environment.vert", SOURCE_DIR "/shaders/environment.frag");
    CreateShader("environmentAlpha", SOURCE_DIR "/shaders/environment.vert", SOURCE_DIR "/shaders/environment_alpha.frag");
    CreateShader("simplelayer", SOURCE_DIR "/shaders/simplelayer.vert", SOURCE_DIR "/shaders/simplelayer.frag");
    CreateShader("postalphaunlit", SOURCE_DIR "/shaders/postalphaunlit.vert", SOURCE_DIR "/shaders/postalphaunlit.frag");
    CreateShader("NDEVCdecal_mesh", SOURCE_DIR "/shaders/NDEVCdecal_mesh.vert", SOURCE_DIR "/shaders/NDEVCdecal_mesh.frag");
    CreateShader("refraction", SOURCE_DIR "/shaders/refraction.vert", SOURCE_DIR "/shaders/refraction.frag");
    CreateShader("water", SOURCE_DIR "/shaders/water.vert", SOURCE_DIR "/shaders/water.frag");
    CreateShader("lighting", SOURCE_DIR "/shaders/lighting.vert", SOURCE_DIR "/shaders/lighting.frag");
    CreateShader("lightCompose", SOURCE_DIR "/shaders/lighting.vert", SOURCE_DIR "/shaders/lightCompose.frag");
    CreateShader("lightComposition", SOURCE_DIR "/shaders/lighting.vert", SOURCE_DIR "/shaders/lightComposition.frag");
    CreateShader("pointLight", SOURCE_DIR "/shaders/pointLight.vert", SOURCE_DIR "/shaders/pointLight.frag");
    CreateShader("lightShadows", SOURCE_DIR "/shaders/lightShadows.vert", SOURCE_DIR "/shaders/lightShadows.frag");
    CreateShader("blit", SOURCE_DIR "/shaders/blit.vert", SOURCE_DIR "/shaders/blit.frag");

    std::cout << "OpenGLShaderManager initialized with " << shaders_.size() << " shaders." << std::endl;

    std::filesystem::path shadersPath = SOURCE_DIR "/shaders/";
    searchPaths_.emplace_back(shadersPath);
}

OpenGLShaderManager::~OpenGLShaderManager() {
    Shutdown();
}

void OpenGLShaderManager::Initialize() {
    running_ = true;
    fileWatcher_ = std::make_unique<std::thread>(&OpenGLShaderManager::FileWatchLoop, this);
}

void OpenGLShaderManager::Shutdown() {
    running_ = false;
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
    for (auto& name : todo) ReloadShader(name);
}

void OpenGLShaderManager::FileWatchLoop() {
    while(running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        UpdateFileMonitoring();
    }
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
                    std::cerr << "[ShaderManager][ERROR] exists() failed for '" << path
                              << "': " << ec.message() << "\n";
                } else {
                    std::cerr << "[ShaderManager][ERROR] Shader file missing: '" << path << "'\n";
                }
                return;
            }
            if (ec) {
                std::cerr << "[ShaderManager][ERROR] exists() error for '" << path
                          << "': " << ec.message() << "\n";
                return;
            }

            const auto ftime = std::filesystem::last_write_time(fsPath, ec);
            if (ec) {
                std::cerr << "[ShaderManager][ERROR] last_write_time() failed for '" << path
                          << "': " << ec.message() << "\n";
                return;
            }

            auto it = fileTimestamps_.find(path);
            if (it == fileTimestamps_.end() || it->second != ftime) {
                fileTimestamps_[path] = ftime;
                pendingReloads_.insert(name);
            }
        };

        checkFile(glShader->GetInternalShader()->getPaths().vertex);
        checkFile(glShader->GetInternalShader()->getPaths().fragment);
    }
}

void OpenGLShaderManager::ScanDirectory(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Shader scan error: Invalid directory: " << fs::absolute(directory) << "\n";
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
        std::cerr << "Shader scan error: " << e.what() << "\n";
    }
}

std::string OpenGLShaderManager::ReadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void OpenGLShaderManager::LoadShader(const std::filesystem::path& path) {
   try {
        std::string baseName = path.stem().string();
        std::lock_guard<std::mutex> lock(shaderMutex_);

        if (shaders_.find(baseName) != shaders_.end()) {
            ReloadShader(baseName);
            return;
        }

        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Shader file does not exist or is not a regular file: " + path.string());
        }

        if (auto extension = path.extension().string(); extension == ".frag" || extension == ".vert") {
            std::filesystem::path vertPath = path.parent_path() / (baseName + ".vert");
            std::filesystem::path fragPath = path.parent_path() / (baseName + ".frag");

            if (!std::filesystem::exists(vertPath)) {
                throw std::runtime_error("Vertex shader file does not exist: " + vertPath.string());
            }
            if (!std::filesystem::exists(fragPath)) {
                throw std::runtime_error("Fragment shader file does not exist: " + fragPath.string());
            }

            auto shader = std::make_shared<OpenGLShader>(vertPath.string().c_str(), fragPath.string().c_str());

            if (shader->IsValid()) {
                shaders_[baseName] = shader;
                std::error_code ecVert;
                std::error_code ecFrag;
                const auto vertTime = std::filesystem::last_write_time(vertPath, ecVert);
                const auto fragTime = std::filesystem::last_write_time(fragPath, ecFrag);
                if (ecVert) {
                    std::cerr << "[ShaderManager][ERROR] last_write_time() failed for '"
                              << vertPath.string() << "': " << ecVert.message() << "\n";
                } else {
                    fileTimestamps_[vertPath.string()] = vertTime;
                }
                if (ecFrag) {
                    std::cerr << "[ShaderManager][ERROR] last_write_time() failed for '"
                              << fragPath.string() << "': " << ecFrag.message() << "\n";
                } else {
                    fileTimestamps_[fragPath.string()] = fragTime;
                }
            } else {
                throw std::runtime_error("Failed to compile combined shader: " + baseName);
            }
        } else if (extension == ".glsl") {
            auto shader = std::make_shared<OpenGLShader>(path.string().c_str(), path.string().c_str());

            if (shader->IsValid()) {
                shaders_[baseName] = shader;
                std::error_code ecTime;
                const auto ftime = std::filesystem::last_write_time(path, ecTime);
                if (ecTime) {
                    std::cerr << "[ShaderManager][ERROR] last_write_time() failed for '"
                              << path.string() << "': " << ecTime.message() << "\n";
                } else {
                    fileTimestamps_[path.string()] = ftime;
                }
            } else {
                throw std::runtime_error("Failed to compile GLSL shader: " + baseName);
            }
        } else {
            throw std::runtime_error("Unsupported shader file extension: " + extension);
        }
    } catch (const std::exception& e) {
        std::cerr << "Shader load error: " << e.what() << "\n";
    }
}

void OpenGLShaderManager::ReloadAll() {
    std::lock_guard<std::mutex> lock(shaderMutex_);
    for(auto& [name, _] : shaders_) {
        ReloadShader(name);
    }
}

void OpenGLShaderManager::ReloadShader(const std::string& name) {
    try {
        auto& shader = shaders_.at(name);
        shader->Reload();
    } catch(const std::exception& e) {
        std::cerr << "Shader reload failed: " << name << " - " << e.what() << "\n";
    }
}

void OpenGLShaderManager::HandleFileDrop(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(shaderMutex_);
    for(const auto& path : paths) {
        LoadShader(path);
    }
}

std::shared_ptr<IShader> OpenGLShaderManager::GetShader(const std::string& name) {
    std::lock_guard<std::mutex> lock(shaderMutex_);
    auto it = shaders_.find(name);
    return it != shaders_.end() ? it->second : nullptr;
}

}
