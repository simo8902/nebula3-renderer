// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RESOURCE_SERVER_H
#define NDEVC_RESOURCE_SERVER_H

#include "Rendering/Mesh.h"
#include <unordered_map>
#include <cstdint>

class ResourceServer {
public:
    static ResourceServer& instance() {
        static ResourceServer srv;
        return srv;
    }

    uint32_t Register(const Mesh* mesh) {
        if (!mesh) return UINT32_MAX;
        auto it = handles_.find(mesh);
        if (it != handles_.end()) return it->second;
        const uint32_t handle = nextHandle_++;
        handles_[mesh] = handle;
        return handle;
    }

    uint32_t Resolve(const Mesh* mesh) const {
        if (!mesh) return UINT32_MAX;
        auto it = handles_.find(mesh);
        return (it != handles_.end()) ? it->second : UINT32_MAX;
    }

    void Clear() {
        handles_.clear();
        nextHandle_ = 0;
    }

private:
    ResourceServer() = default;
    ResourceServer(const ResourceServer&) = delete;
    ResourceServer& operator=(const ResourceServer&) = delete;

    std::unordered_map<const Mesh*, uint32_t> handles_;
    uint32_t nextHandle_ = 0;
};

#endif // NDEVC_RESOURCE_SERVER_H
