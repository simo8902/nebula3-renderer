// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace NC {

// In-process virtual file system.
// Mounts an NDPK blob via OS memory-mapping — no bytes are ever written to disk.
// All asset loaders (shaders, textures, models, maps) call VFS::Instance().Read()
// before attempting a real file open.
class VFS {
public:
    struct View {
        const uint8_t* data = nullptr;
        size_t         size = 0;
        bool           valid() const { return data && size > 0; }
    };

    static VFS& Instance();

    // Memory-map the NDPK and build the path->view table.
    // outStartupMapPath is set to the original map path recorded in the scene chunk.
    bool MountNdpk(const std::string& ndpkPath, std::string& outStartupMapPath);

    bool IsMounted() const { return mounted_; }

    // Build profile stored in the NDPK manifest: 0=Work, 1=Playtest, 2=Shipping.
    // Only valid after a successful MountNdpk call.
    uint8_t GetProfile() const { return profile_; }

    // Look up asset bytes by absolute or relative path.
    // Returns an invalid View when not found; never copies bytes.
    View Read(const std::string& path) const;

    bool Exists(const std::string& path) const;

    void Unmount();

private:
    VFS() = default;
    ~VFS() { Unmount(); }
    VFS(const VFS&) = delete;
    VFS& operator=(const VFS&) = delete;

    // Normalize any absolute or relative path to a canonical relative key
    // like "shaders/blit.vert", "maps/foo.map", "textures/stone.dds".
    static std::string Normalize(const std::string& path);

    std::unordered_map<std::string, View> entries_;
    bool    mounted_ = false;
    uint8_t profile_ = 0; // ExportProfile value from manifest: 0=Work, 1=Playtest, 2=Shipping

#if defined(_WIN32)
    HANDLE fileHandle_    = INVALID_HANDLE_VALUE;
    HANDLE mappingHandle_ = nullptr;
    const uint8_t* base_  = nullptr;
    size_t         baseSize_ = 0;
#else
    int fd_ = -1;
    void* base_ = nullptr;
    size_t baseSize_ = 0;
#endif
};

} // namespace NC
