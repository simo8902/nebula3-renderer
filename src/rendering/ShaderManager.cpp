// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/ShaderManager.h"
#include "Core/Errors.h"
#include "Core/Logger.h"
#include "Rendering/OpenGL/OpenGLShader.h"

ShaderManager::ShaderManager() {
    NC::LOGGING::Log("[SHADER_MGR] Constructor");

    shaderProgram = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/NDEVCdeferred.vert",
               SOURCE_DIR "/shaders/NDEVCdeferred.frag"
           );

    if (!shaderProgram->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Main shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready NDEVCdeferred");

    NC::LOGGING::Log("[SHADER_MGR] NDEVCdeferred program ID: ", *(GLuint*)shaderProgram->GetNativeHandle());

    auto standardShaderProgram = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/standard.vert",
               SOURCE_DIR "/shaders/standard.frag"
           );

    if (!standardShaderProgram->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Standard shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready standard");

    auto particleShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/particle.vert",
               SOURCE_DIR "/shaders/particle.frag"
           );

    if (!particleShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Particle shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready particle");

    auto environmentShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/environment.vert",
               SOURCE_DIR "/shaders/environment.frag"
           );
    if (!environmentShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Environment shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready environment");

    auto environmentAlphaShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/environment.vert",
               SOURCE_DIR "/shaders/environment_alpha.frag"
           );
    if (!environmentAlphaShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("EnvironmentAlpha shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready environmentAlpha");

    auto simpleLayerShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/simplelayer.vert",
               SOURCE_DIR "/shaders/simplelayer.frag",
               "#define SKINNING_MODE 2\n#define PASS 1\n",
               "#define PASS 3\n"
            );
    if (!simpleLayerShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Simplelayer shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready simplelayer");

    auto simpleLayerGBufferShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/simplelayer.vert",
               SOURCE_DIR "/shaders/simplelayer.frag",
               "#define SKINNING_MODE 2\n#define PASS 2\n",
               "#define PASS 5\n"
            );
    if (!simpleLayerGBufferShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Simplelayer gbuffer shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready simplelayer_gbuffer");

    auto simpleLayerGBufferClipShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/simplelayer.vert",
               SOURCE_DIR "/shaders/simplelayer.frag",
               "#define SKINNING_MODE 2\n#define PASS 2\n",
               "#define PASS 4\n"
            );
    if (!simpleLayerGBufferClipShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Simplelayer gbuffer clip shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready simplelayer_gbuffer_clip");

    auto simpleLayerShadowShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/simplelayer.vert",
               SOURCE_DIR "/shaders/simplelayer.frag",
               "#define SKINNING_MODE 2\n#define PASS 3\n",
               "#define PASS 6\n"
            );
    if (!simpleLayerShadowShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Simplelayer shadow shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready simplelayer_shadow");

    auto simpleLayerDepthShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/simplelayer.vert",
               SOURCE_DIR "/shaders/simplelayer.frag",
               "#define SKINNING_MODE 2\n#define PASS 4\n",
               "#define PASS 7\n"
            );
    if (!simpleLayerDepthShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Simplelayer depth shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready simplelayer_depth");

    auto postAlphaUnlitShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/postalphaunlit.vert",
               SOURCE_DIR "/shaders/postalphaunlit.frag"
           );
    if (!postAlphaUnlitShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("PostAlphaUnlit shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready postalphaunlit");

    auto decalShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/NDEVCdecal_mesh.vert",
               SOURCE_DIR "/shaders/NDEVCdecal_mesh.frag"
           );
    if (!decalShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Decal shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready NDEVCdecal_mesh");

    auto refractionShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/refraction.vert",
               SOURCE_DIR "/shaders/refraction.frag"
           );
    if (!refractionShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Refraction shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready refraction");

    auto waterShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
               SOURCE_DIR "/shaders/water.vert",
               SOURCE_DIR "/shaders/water.frag"
           );
    if (!waterShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Water shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready water");

    auto lightingShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
           SOURCE_DIR "/shaders/lighting.vert",
           SOURCE_DIR "/shaders/lighting.frag"
       );
    if (!lightingShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Lighting shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready lighting");

    auto lightCompose = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
           SOURCE_DIR "/shaders/lighting.vert",
           SOURCE_DIR "/shaders/lightCompose.frag"
       );
    if (!lightCompose->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Lighting compose shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready lightCompose");

    auto lightComposition = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
       SOURCE_DIR "/shaders/lighting.vert",
       SOURCE_DIR "/shaders/lightComposition.frag"
    );
    if (!lightComposition->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Lighting composition shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready lightComposition");

    auto pointLight = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
   SOURCE_DIR "/shaders/pointLight.vert",
   SOURCE_DIR "/shaders/pointLight.frag"
    );
    if (!pointLight->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Point lighting shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready pointLight");

    auto lightShadow = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
       SOURCE_DIR "/shaders/lightShadows.vert",
       SOURCE_DIR "/shaders/lightShadows.frag"
    );
    if (!lightShadow->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Light Shadows shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready lightShadows");

    auto blitShader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(
       SOURCE_DIR "/shaders/blit.vert",
       SOURCE_DIR "/shaders/blit.frag"
    );
    if (!blitShader->IsValid()) {
        throw NC::Errors::LoggedRuntimeError("Blit shader init failed");
    }
    NC::LOGGING::Log("[SHADER_MGR] Ready blit");

    shaders["NDEVCdeferred"] = shaderProgram;
    shaders["standard"] = standardShaderProgram;
    shaders["particle"] = particleShader;
    shaders["environment"] = environmentShader;
    shaders["environmentAlpha"] = environmentAlphaShader;
    shaders["simplelayer"] = simpleLayerShader;
    shaders["simplelayer_gbuffer"] = simpleLayerGBufferShader;
    shaders["simplelayer_gbuffer_clip"] = simpleLayerGBufferClipShader;
    shaders["simplelayer_shadow"] = simpleLayerShadowShader;
    shaders["simplelayer_depth"] = simpleLayerDepthShader;
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
    for (const auto& [name, shader] : shaders) {
        const auto* handle = static_cast<GLuint*>(shader->GetNativeHandle());
        NC::LOGGING::Log("[SHADER_MGR] Registered shader ", name, " program=", (handle ? *handle : 0u));
    }

    NC::LOGGING::Log("[SHADER_MGR] Initialized with ", shaders.size(), " shaders");

    std::filesystem::path shadersPath = SOURCE_DIR "/shaders/";
    searchPaths.emplace_back(shadersPath);
}

ShaderManager::~ShaderManager() {
    Shutdown();
}

void ShaderManager::Initialize() {
    running = true;
    NC::LOGGING::Log("[SHADER_MGR] Initialize file watcher");
    fileWatcher = std::make_unique<std::thread>(&ShaderManager::FileWatchLoop, this);
    for (const auto& path : searchPaths) {
      //  ScanDirectory(path);
    }
}

void ShaderManager::Shutdown() {
    running = false;
    NC::LOGGING::Log("[SHADER_MGR] Shutdown file watcher");
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
    NC::LOGGING::Log("[SHADER_MGR] ProcessPendingReloads count=", todo.size());
    for (auto& name : todo) ReloadShader(name);
}

void ShaderManager::FileWatchLoop() {
    NC::LOGGING::Log("[SHADER_MGR] FileWatchLoop start");
    uint32_t tick = 0;
    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        UpdateFileMonitoring();
        ++tick;
        if ((tick % 120) == 0) {
            NC::LOGGING::Log("[SHADER_MGR] FileWatchLoop heartbeat tick=", tick);
        }
    }
    NC::LOGGING::Log("[SHADER_MGR] FileWatchLoop stop");
}

void ShaderManager::UpdateFileMonitoring() {
    std::lock_guard<std::mutex> lock(shaderMutex);
    NC::LOGGING::Log("[SHADER_MGR] UpdateFileMonitoring shaders=", shaders.size());

    for (auto& [name, shader] : shaders) {
        auto checkFile = [&](const std::string& path) {
            if (path.empty()) return;
            std::error_code ec;
            auto ftime = std::filesystem::last_write_time(path, ec);
            if (ec) {
                NC::LOGGING::Warning("[SHADER_MGR] last_write_time failed name=", name, " path=", path, " err=", ec.message());
                return;
            }
            auto it = fileTimestamps.find(path);
            if (it == fileTimestamps.end() || it->second != ftime) {
                fileTimestamps[path] = ftime;
                pendingReloads.insert(name);
                NC::LOGGING::Log("[SHADER_MGR] Change detected for ", name, " path=", path);
            }
        };

        auto glShader = std::dynamic_pointer_cast<NDEVC::Graphics::OpenGL::OpenGLShader>(shader);
        if (!glShader) continue;
        checkFile(glShader->GetPaths().vertex);
        checkFile(glShader->GetPaths().fragment);
    }
}

void ShaderManager::ScanDirectory(const std::filesystem::path& directory) {

     namespace fs = std::filesystem;
    NC::LOGGING::Log("[SHADER_MGR] ScanDirectory start dir=", fs::absolute(directory).string());

  //  std::cout << "Scanning directory: " << fs::absolute(directory) << "\n";

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        NC::LOGGING::Error("[SHADER_MGR] Scan error invalid directory: ", fs::absolute(directory).string());
        return;
    }

    try {
        std::unordered_map<std::string, std::pair<std::filesystem::path, std::filesystem::path>> shaderPairs;
        size_t scannedFiles = 0;
        size_t pairFiles = 0;
        size_t combinedFiles = 0;

        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                ++scannedFiles;
                const auto& filePath = entry.path();
                const auto& extension = filePath.extension();

                if (extension == ".vert" || extension == ".frag") {
                    std::string name = filePath.stem().string();
                    ++pairFiles;
                    if (extension == ".vert") {
                        shaderPairs[name].first = filePath;
                    } else if (extension == ".frag") {
                        shaderPairs[name].second = filePath;
                    }
                } else if (extension == ".glsl") {
                    ++combinedFiles;
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
                    NC::LOGGING::Warning("[SHADER_MGR] Found vertex shader without corresponding fragment: ", paths.first.string());
                }
                if (!paths.second.empty()) {
                    NC::LOGGING::Warning("[SHADER_MGR] Found fragment shader without corresponding vertex: ", paths.second.string());
                }
            }
        }
        NC::LOGGING::Log("[SHADER_MGR] ScanDirectory done dir=", fs::absolute(directory).string(),
                         " scanned=", scannedFiles, " pairFiles=", pairFiles, " glsl=", combinedFiles,
                         " shaderPairs=", shaderPairs.size());

    } catch (const fs::filesystem_error& e) {
        NC::LOGGING::Error("[SHADER_MGR] Scan error: ", e.what());
    }
}
std::string ShaderManager::readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw NC::Errors::LoggedRuntimeError("Could not open file: " + filename);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void ShaderManager::LoadShader(const std::filesystem::path& path) {
   try {
        std::string baseName = path.stem().string(); // Base name without extension
        std::lock_guard<std::mutex> lock(shaderMutex);
        NC::LOGGING::Log("[SHADER_MGR] LoadShader request base=", baseName, " path=", path.string());


        if (shaders.find(baseName) != shaders.end()) {
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

            /*
            std::cout << "Found shader pair: " << baseName
                      << " (Vertex: \"" << vertPath.string()
                      << "\", Fragment: \"" << fragPath.string() << "\")\n";*/

            auto shader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(vertPath.string().c_str(), fragPath.string().c_str());

            if (shader->IsValid()) {
                shaders[baseName] = shader;
                fileTimestamps[vertPath.string()] = std::filesystem::last_write_time(vertPath);
                fileTimestamps[fragPath.string()] = std::filesystem::last_write_time(fragPath);
                NC::LOGGING::Log("[SHADER_MGR] Loaded shader pair ", baseName,
                                 " V=", vertPath.string(), " F=", fragPath.string());
             //   std::cout << "Successfully loaded combined shader: " << baseName << "\n";
            } else {
                throw NC::Errors::LoggedRuntimeError("Failed to compile combined shader: " + baseName);
            }
        } else if (extension == ".glsl") {
            auto shader = std::make_shared<NDEVC::Graphics::OpenGL::OpenGLShader>(path.string().c_str(), path.string().c_str());

            if (shader->IsValid()) {
                shaders[baseName] = shader;
                fileTimestamps[path.string()] = std::filesystem::last_write_time(path);
                NC::LOGGING::Log("[SHADER_MGR] Loaded combined shader ", baseName, " path=", path.string());
              //  std::cout << "Successfully loaded GLSL shader: " << baseName << "\n";
            } else {
                throw NC::Errors::LoggedRuntimeError("Failed to compile GLSL shader: " + baseName);
            }

        } else {
            throw NC::Errors::LoggedRuntimeError("Unsupported shader file extension: " + extension);
        }
    } catch (const std::exception& e) {
        NC::LOGGING::Error("[SHADER_MGR] Load error: ", e.what());
    }

}

void ShaderManager::ReloadAll() {
    std::lock_guard<std::mutex> lock(shaderMutex);
    NC::LOGGING::Log("[SHADER_MGR] ReloadAll begin count=", shaders.size());
    for(auto& [name, _] : shaders) {
        ReloadShader(name);
    }
    NC::LOGGING::Log("[SHADER_MGR] ReloadAll end");
}

void ShaderManager::ReloadShader(const std::string& name) {
    try {
        auto& shader = shaders.at(name);
        shader->Reload();
        NC::LOGGING::Log("[SHADER_MGR] Reloaded shader ", name);
       // std::cout << "Reloaded shader: " << name << "\n";
    } catch(const std::exception& e) {
        NC::LOGGING::Error("[SHADER_MGR] Reload failed: ", name, " - ", e.what());
    }
}

void ShaderManager::HandleFileDrop(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    NC::LOGGING::Log("[SHADER_MGR] HandleFileDrop count=", paths.size());
    for(const auto& path : paths) {
        NC::LOGGING::Log("[SHADER_MGR] HandleFileDrop path=", path);
        LoadShader(path);
    }
}

std::shared_ptr<NDEVC::Graphics::IShader> ShaderManager::GetShader(const std::string& name) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    auto it = shaders.find(name);
    if (it == shaders.end()) {
        NC::LOGGING::Warning("[SHADER_MGR] GetShader miss name=", name);
        return nullptr;
    }
    const auto* handle = static_cast<GLuint*>(it->second->GetNativeHandle());
    NC::LOGGING::Log("[SHADER_MGR] GetShader hit name=", name, " program=", (handle ? *handle : 0u));
    return it->second;
}

