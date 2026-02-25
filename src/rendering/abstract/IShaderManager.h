// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ISHADERMANAGER_H
#define NDEVC_ISHADERMANAGER_H

#include <string>
#include <memory>
#include <vector>
#include <filesystem>

namespace NDEVC::Graphics {

class IShader;

class IShaderManager {
public:
    virtual ~IShaderManager() = default;

    virtual void Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual void ScanDirectory(const std::filesystem::path& directory) = 0;
    virtual void ReloadAll() = 0;
    virtual void HandleFileDrop(const std::vector<std::string>& paths) = 0;
    virtual void ProcessPendingReloads() = 0;

    virtual std::shared_ptr<IShader> GetShader(const std::string& name) = 0;
};

}
#endif