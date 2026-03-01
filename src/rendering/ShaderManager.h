// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_SHADERMANAGER_H
#define NDEVC_SHADERMANAGER_H

#include "Platform/NDEVcHeaders.h"
#include "Rendering/Interfaces/IShader.h"

class ShaderManager {
public:
    static ShaderManager& Instance() {
        static ShaderManager instance;
        return instance;
    }

    void Initialize();
    void Shutdown();
    void ScanDirectory(const std::filesystem::path& directory = "shaders");
    void ReloadAll();
    void HandleFileDrop(const std::vector<std::string>& paths);

    std::shared_ptr<NDEVC::Graphics::IShader> GetShader(const std::string& name);
    const auto& getShaders() const { return shaders; }
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;
    void ProcessPendingReloads();
private:
    ShaderManager();
    ~ShaderManager();

    std::unordered_set<std::string> pendingReloads;
    std::shared_ptr<NDEVC::Graphics::IShader> shaderProgram;

    std::unordered_map<std::string, std::shared_ptr<NDEVC::Graphics::IShader>> shaders;
    std::vector<std::filesystem::path> searchPaths;
    std::unique_ptr<std::thread> fileWatcher;
    std::atomic<bool> running{false};
    std::mutex shaderMutex;
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps;

    void FileWatchLoop();
    void UpdateFileMonitoring();
    void LoadShader(const std::filesystem::path& path);
    void ReloadShader(const std::string& name);
    std::string readFile(const std::string& filePath);
};

#endif //NDEVC_SHADERMANAGER_H
