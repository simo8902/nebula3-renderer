// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Interfaces/IShaderManager.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>

namespace NDEVC::Graphics::OpenGL {

class OpenGLShaderManager : public IShaderManager {
public:
    OpenGLShaderManager();
    ~OpenGLShaderManager() override;

    void Initialize() override;
    void Shutdown() override;
    void ScanDirectory(const std::filesystem::path& directory) override;
    void ReloadAll() override;
    void HandleFileDrop(const std::vector<std::string>& paths) override;
    void ProcessPendingReloads() override;

    std::shared_ptr<IShader> GetShader(const std::string& name) override;

private:
    std::unordered_map<std::string, std::shared_ptr<IShader>> shaders_;
    std::vector<std::filesystem::path> searchPaths_;
    std::unique_ptr<std::thread> fileWatcher_;
    std::atomic<bool> running_{false};
    std::mutex shaderMutex_;
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps_;
    std::unordered_set<std::string> pendingReloads_;
    std::unordered_map<std::string, std::unordered_set<std::string>> pathToMaterialsMap_;

    void FileWatchLoop();
    void UpdateFileMonitoring();
    void LoadShader(const std::filesystem::path& path);
    void ReloadShader(const std::string& name);
    void RegisterShaderFileDependencies(const std::string& materialName, const std::shared_ptr<IShader>& shader);
    std::string ReadFile(const std::string& filePath);
};

}
