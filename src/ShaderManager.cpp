// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "ShaderManager.h"

ShaderManager::ShaderManager() {
    std::cerr << "ShaderManager()" << std::endl;

    shaderProgram = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/NDEVCdeferred.vert",
               SOURCE_DIR "/shaders/NDEVCdeferred.frag"
           );

    if (!shaderProgram->isValid()) {
        std::cerr << "[FATAL] NDEVCdeferred shader failed to compile/link\n";
        throw std::runtime_error("Main shader init failed");
    }

    std::cout << "[ShaderManager] NDEVCdeferred program ID: " << shaderProgram->getProgramID() << "\n";

    auto particleShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/particle.vert",
               SOURCE_DIR "/shaders/particle.frag"
           );

    if (!particleShader->isValid()) {
        std::cerr << "[FATAL] particle shader failed to compile/link\n";
        throw std::runtime_error("Particle shader init failed");
    }

    auto environmentShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/environment.vert",
               SOURCE_DIR "/shaders/environment.frag"
           );
    if (!environmentShader->isValid()) {
        std::cerr << "[FATAL] environment shader failed to compile/link\n";
        throw std::runtime_error("Environment shader init failed");
    }

    auto environmentAlphaShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/environment.vert",
               SOURCE_DIR "/shaders/environment.frag"
           );
    if (!environmentAlphaShader->isValid()) {
        std::cerr << "[FATAL] environmentAlpha shader failed to compile/link\n";
        throw std::runtime_error("EnvironmentAlpha shader init failed");
    }

    auto simpleLayerShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/simplelayer.vert",
               SOURCE_DIR "/shaders/simplelayer.frag"
           );
    if (!simpleLayerShader->isValid()) {
        std::cerr << "[FATAL] simplelayer shader failed to compile/link\n";
        throw std::runtime_error("Simplelayer shader init failed");
    }

    auto postAlphaUnlitShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/postalphaunlit.vert",
               SOURCE_DIR "/shaders/postalphaunlit.frag"
           );
    if (!postAlphaUnlitShader->isValid()) {
        std::cerr << "[FATAL] postalphaunlit shader failed to compile/link\n";
        throw std::runtime_error("PostAlphaUnlit shader init failed");
    }

    auto decalShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/NDEVCdecal_mesh.vert",
               SOURCE_DIR "/shaders/NDEVCdecal_mesh.frag"
           );
    if (!decalShader->isValid()) {
        std::cerr << "[FATAL] decal shader failed to compile/link\n";
        throw std::runtime_error("Decal shader init failed");
    }

    auto refractionShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/refraction.vert",
               SOURCE_DIR "/shaders/refraction.frag"
           );
    if (!refractionShader->isValid()) {
        std::cerr << "[FATAL] refraction shader failed to compile/link\n";
        throw std::runtime_error("Refraction shader init failed");
    }

    auto waterShader = std::make_shared<NDEVC::Graphics::Shader>(
               SOURCE_DIR "/shaders/water.vert",
               SOURCE_DIR "/shaders/water.frag"
           );
    if (!waterShader->isValid()) {
        std::cerr << "[FATAL] water shader failed to compile/link\n";
        throw std::runtime_error("Water shader init failed");
    }

    auto lightingShader = std::make_shared<NDEVC::Graphics::Shader>(
           SOURCE_DIR "/shaders/lighting.vert",
           SOURCE_DIR "/shaders/lighting.frag"
       );
    if (!lightingShader->isValid()) {
        std::cerr << "[FATAL] lighting shader failed to compile/link\n";
        throw std::runtime_error("Lighting shader init failed");
    }

    auto lightCompose = std::make_shared<NDEVC::Graphics::Shader>(
           SOURCE_DIR "/shaders/lighting.vert",
           SOURCE_DIR "/shaders/lightCompose.frag"
       );
    if (!lightCompose->isValid()) {
        std::cerr << "[FATAL] lighting compose shader failed to compile/link\n";
        throw std::runtime_error("Lighting compose shader init failed");
    }

    auto lightComposition = std::make_shared<NDEVC::Graphics::Shader>(
       SOURCE_DIR "/shaders/lighting.vert",
       SOURCE_DIR "/shaders/lightComposition.frag"
    );
    if (!lightComposition->isValid()) {
        std::cerr << "[FATAL] lighting composition shader failed to compile/link\n";
        throw std::runtime_error("Lighting composition shader init failed");
    }

    auto pointLight = std::make_shared<NDEVC::Graphics::Shader>(
   SOURCE_DIR "/shaders/pointLight.vert",
   SOURCE_DIR "/shaders/pointLight.frag"
    );
    if (!pointLight->isValid()) {
        std::cerr << "[FATAL] point light shader failed to compile/link\n";
        throw std::runtime_error("Point lighting shader init failed");
    }

    auto lightShadow = std::make_shared<NDEVC::Graphics::Shader>(
       SOURCE_DIR "/shaders/lightShadows.vert",
       SOURCE_DIR "/shaders/lightShadows.frag"
    );
    if (!lightShadow->isValid()) {
        std::cerr << "[FATAL] lightShadows shader failed to compile/link\n";
        throw std::runtime_error("Light Shadows shader init failed");
    }

    auto blitShader = std::make_shared<NDEVC::Graphics::Shader>(
       SOURCE_DIR "/shaders/blit.vert",
       SOURCE_DIR "/shaders/blit.frag"
    );
    if (!blitShader->isValid()) {
        std::cerr << "[FATAL] blit shader failed to compile/link\n";
        throw std::runtime_error("Blit shader init failed");
    }

    shaders["NDEVCdeferred"] = shaderProgram;
    shaders["particle"] = particleShader;
    shaders["environment"] = environmentShader;
    shaders["environmentAlpha"] = environmentAlphaShader;
    shaders["simplelayer"] = simpleLayerShader;
    shaders["postalphaunlit"] = postAlphaUnlitShader;
    shaders["NDEVCdecal_mesh"] = decalShader;
    shaders["refraction"] = refractionShader;
    shaders["water"] = waterShader;
    shaders["lighting"] = lightingShader;
    shaders["lightCompose"] = lightCompose;
    shaders["lightComposition"] = lightComposition;
    shaders["pointLight"] = pointLight;
    shaders["lightShadows"] = lightShadow;
    shaders["blit"] = blitShader;

    std::cout << "ShaderManager initialized with " << shaders.size() << " shaders." << std::endl;

    std::filesystem::path shadersPath = SOURCE_DIR "/shaders/";
    searchPaths.emplace_back(shadersPath);
}

ShaderManager::~ShaderManager() {
    Shutdown();
}

void ShaderManager::Initialize() {
    running = true;
    fileWatcher = std::make_unique<std::thread>(&ShaderManager::FileWatchLoop, this);
    for (const auto& path : searchPaths) {
      //  ScanDirectory(path);
    }
}

void ShaderManager::Shutdown() {
    running = false;
    if(fileWatcher && fileWatcher->joinable()) {
        fileWatcher->join();
    }
}

void ShaderManager::ProcessPendingReloads() {
    std::unordered_set<std::string> todo;
    {
        std::lock_guard<std::mutex> lock(shaderMutex);
        todo.swap(pendingReloads);
    }
    for (auto& name : todo) ReloadShader(name);
}

void ShaderManager::FileWatchLoop() {
    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        UpdateFileMonitoring();
    }
}

void ShaderManager::UpdateFileMonitoring() {
    std::lock_guard<std::mutex> lock(shaderMutex);

    for (auto& [name, shader] : shaders) {
        auto checkFile = [&](const std::string& path) {
            if (path.empty()) return;
            auto ftime = std::filesystem::last_write_time(path);
            auto it = fileTimestamps.find(path);
            if (it == fileTimestamps.end() || it->second != ftime) {
                fileTimestamps[path] = ftime;
                pendingReloads.insert(name);
            }
        };

        checkFile(shader->getPaths().vertex);
        checkFile(shader->getPaths().fragment);
    }
}

void ShaderManager::ScanDirectory(const std::filesystem::path& directory) {

     namespace fs = std::filesystem;

  //  std::cout << "Scanning directory: " << fs::absolute(directory) << "\n";

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
               // std::cout << "Found shader pair: " << name << " (Vertex: " << paths.first << ", Fragment: " << paths.second << ")\n";
                LoadShader(paths.first);
                LoadShader(paths.second);
            } else {
                if (!paths.first.empty()) {
                    std::cerr << "Warning: Found vertex shader without corresponding fragment: " << paths.first << "\n";
                }
                if (!paths.second.empty()) {
                    std::cerr << "Warning: Found fragment shader without corresponding vertex: " << paths.second << "\n";
                }
            }
        }

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Shader scan error: " << e.what() << "\n";
    }
}
std::string ShaderManager::readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void ShaderManager::LoadShader(const std::filesystem::path& path) {
   try {
        std::string baseName = path.stem().string(); // Base name without extension
        std::lock_guard<std::mutex> lock(shaderMutex);


        if (shaders.find(baseName) != shaders.end()) {
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

            /*
            std::cout << "Found shader pair: " << baseName
                      << " (Vertex: \"" << vertPath.string()
                      << "\", Fragment: \"" << fragPath.string() << "\")\n";*/

            auto shader = std::make_shared<NDEVC::Graphics::Shader>(vertPath.string().c_str(), fragPath.string().c_str());

            if (shader->isValid()) {
                shaders[baseName] = shader;
                fileTimestamps[vertPath.string()] = std::filesystem::last_write_time(vertPath);
                fileTimestamps[fragPath.string()] = std::filesystem::last_write_time(fragPath);
             //   std::cout << "Successfully loaded combined shader: " << baseName << "\n";
            } else {
                throw std::runtime_error("Failed to compile combined shader: " + baseName);
            }
        } else if (extension == ".glsl") {
            auto shader = std::make_shared<NDEVC::Graphics::Shader>(path.string().c_str(), path.string().c_str());

            if (shader->isValid()) {
                shaders[baseName] = shader;
                fileTimestamps[path.string()] = std::filesystem::last_write_time(path);
              //  std::cout << "Successfully loaded GLSL shader: " << baseName << "\n";
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

void ShaderManager::ReloadAll() {
    std::lock_guard<std::mutex> lock(shaderMutex);
    for(auto& [name, _] : shaders) {
        ReloadShader(name);
    }
}

void ShaderManager::ReloadShader(const std::string& name) {
    try {
        auto& shader = shaders.at(name);
        shader->reload();
       // std::cout << "Reloaded shader: " << name << "\n";
    } catch(const std::exception& e) {
        std::cerr << "Shader reload failed: " << name << " - " << e.what() << "\n";
    }
}

void ShaderManager::HandleFileDrop(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    for(const auto& path : paths) {
        LoadShader(path);
    }
}

std::shared_ptr<NDEVC::Graphics::Shader> ShaderManager::GetShader(const std::string& name) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    auto it = shaders.find(name);
    return it != shaders.end() ? it->second : nullptr;
}
