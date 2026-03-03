// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Core/VFS.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace NC {

// ---------------------------------------------------------------------------
// NDPK binary constants — must match Package.cpp / PackageReader.cpp
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t kNdpkMagic   = 0x4E44504Bu; // 'NDPK'
constexpr uint32_t kNdpkVersion = 1u;
constexpr size_t   kHeaderSize  = 28; // magic(4)+ver(4)+chunkCount(4)+manifestOffset(8)+manifestSize(8)
constexpr size_t   kTocEntrySize = 44; // name(16)+flags(4)+offset(8)+size(8)+hash(8)

struct TocEntry {
    char     name[16];
    uint32_t flags;
    uint64_t offsetBytes;
    uint64_t sizeBytes;
    uint64_t hash;
};

struct AssetRecord {
    std::string assetPath;
    uint32_t    chunkId = 0;
};

// Read LE integers from raw memory
inline uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}
inline uint64_t ReadU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (8u * i);
    return v;
}
inline uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
}

// Read a length-prefixed (uint16) string from a buffer cursor
bool ReadStr16(const uint8_t* buf, size_t bufSize, size_t& pos, std::string& out) {
    if (pos + 2 > bufSize) return false;
    const uint16_t len = ReadU16(buf + pos);
    pos += 2;
    if (pos + len > bufSize) return false;
    out.assign(reinterpret_cast<const char*>(buf + pos), len);
    pos += len;
    return true;
}

// Parse the manifest blob (serialized by Package.cpp::SerializeManifest).
// We only need chunkCount (already known from TOC) and the asset->chunkId table.
bool ParseManifestAssets(const uint8_t* buf, size_t bufSize,
                         std::vector<AssetRecord>& outAssets, uint8_t& outProfile) {
    if (!buf || bufSize < 4) return false;
    size_t pos = 0;

    // version(4) + buildTime(8) + contentHash(8) + profile(1)
    if (pos + 21 > bufSize) return false;
    pos += 4 + 8 + 8; // skip version, buildTime, contentHash
    outProfile = buf[pos];
    pos += 1;

    // chunkCount(4) + per-chunk: name(str16) + hash(8) + size(8) + offset(8) + preload(1) + compressed(1)
    if (pos + 4 > bufSize) return false;
    const uint32_t chunkCount = ReadU32(buf + pos);
    pos += 4;
    for (uint32_t i = 0; i < chunkCount; ++i) {
        std::string name;
        if (!ReadStr16(buf, bufSize, pos, name)) return false;
        if (pos + 8 + 8 + 8 + 1 + 1 > bufSize) return false;
        pos += 8 + 8 + 8 + 1 + 1; // hash, size, offset, preload, compressed
    }

    // assetCount(4)
    if (pos + 4 > bufSize) return false;
    const uint32_t assetCount = ReadU32(buf + pos);
    pos += 4;

    outAssets.reserve(assetCount);
    for (uint32_t i = 0; i < assetCount; ++i) {
        AssetRecord rec;
        if (!ReadStr16(buf, bufSize, pos, rec.assetPath)) return false;
        if (pos + 4 + 8 > bufSize) return false;
        rec.chunkId = ReadU32(buf + pos); pos += 4;
        pos += 8; // assetHash
        outAssets.push_back(std::move(rec));
    }
    return true;
}

// Read the startup map path from a "scene" chunk (NDSCENE2 format)
std::string ExtractMapPathFromSceneChunk(const uint8_t* chunkData, size_t chunkSize) {
    static constexpr char kTag[] = "NDSCENE2";
    if (!chunkData || chunkSize < 8) return {};
    if (std::memcmp(chunkData, kTag, 8) != 0) return {};
    size_t pos = 8;
    if (pos + 4 > chunkSize) return {};
    pos += 4; // version
    // sceneGuid, sceneName, mapName, mapPath — all str16
    for (int field = 0; field < 4; ++field) {
        std::string s;
        if (!ReadStr16(chunkData, chunkSize, pos, s)) return {};
        if (field == 3) return s; // mapPath
    }
    return {};
}

// Normalize any path to a relative key the VFS uses internally.
// "C:/work/shaders/blit.vert"  →  "shaders/blit.vert"
// "shaders/blit.vert"          →  "shaders/blit.vert"
// "C:\\work\\maps\\foo.map"    →  "maps/foo.map"
std::string NormalizeImpl(const std::string& raw) {
    if (raw.empty()) return {};

    // Canonicalize to forward slashes, lower-case for matching only
    std::string s = raw;
    for (char& c : s)
        if (c == '\\') c = '/';

    // Strip leading whitespace/quotes just in case
    while (!s.empty() && (s.front() == '"' || s.front() == ' ')) s.erase(s.begin());

    static const char* kMarkers[] = {
        "/shaders/", "/maps/", "/models/", "/meshes/",
        "/textures/", "/anims/", "/scenes/", nullptr
    };

    // Case-insensitive search for each marker
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (int i = 0; kMarkers[i]; ++i) {
        const size_t pos = low.find(kMarkers[i]);
        if (pos != std::string::npos) {
            // Return from after the leading '/'
            return s.substr(pos + 1);
        }
    }

    // Already relative (no drive letter or marker) — return as-is
    if (s.size() >= 2 && s[1] == ':') return {}; // still an absolute path but unknown dir
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// VFS singleton
// ---------------------------------------------------------------------------
VFS& VFS::Instance() {
    static VFS s;
    return s;
}

std::string VFS::Normalize(const std::string& path) {
    return NormalizeImpl(path);
}

// ---------------------------------------------------------------------------
// MountNdpk — memory-map the blob, build entries_ table
// ---------------------------------------------------------------------------
bool VFS::MountNdpk(const std::string& ndpkPath, std::string& outStartupMapPath) {
    Unmount();
    outStartupMapPath.clear();

    if (ndpkPath.empty()) {
        NC::LOGGING::Error("[VFS] MountNdpk: empty path");
        return false;
    }

#if defined(_WIN32)
    // Open the NDPK file for reading
    fileHandle_ = CreateFileA(ndpkPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle_ == INVALID_HANDLE_VALUE) {
        NC::LOGGING::Error("[VFS] Cannot open NDPK: ", ndpkPath);
        return false;
    }

    LARGE_INTEGER fileSizeLI{};
    if (!GetFileSizeEx(fileHandle_, &fileSizeLI) || fileSizeLI.QuadPart < static_cast<LONGLONG>(kHeaderSize)) {
        NC::LOGGING::Error("[VFS] NDPK too small: ", ndpkPath);
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    baseSize_ = static_cast<size_t>(fileSizeLI.QuadPart);

    mappingHandle_ = CreateFileMappingA(fileHandle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mappingHandle_) {
        NC::LOGGING::Error("[VFS] CreateFileMapping failed for: ", ndpkPath);
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    base_ = reinterpret_cast<const uint8_t*>(MapViewOfFile(mappingHandle_, FILE_MAP_READ, 0, 0, 0));
    if (!base_) {
        NC::LOGGING::Error("[VFS] MapViewOfFile failed for: ", ndpkPath);
        CloseHandle(mappingHandle_);
        CloseHandle(fileHandle_);
        mappingHandle_ = nullptr;
        fileHandle_    = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    fd_ = open(ndpkPath.c_str(), O_RDONLY);
    if (fd_ < 0) {
        NC::LOGGING::Error("[VFS] Cannot open NDPK: ", ndpkPath);
        return false;
    }
    struct stat st{};
    if (fstat(fd_, &st) != 0 || st.st_size < static_cast<off_t>(kHeaderSize)) {
        NC::LOGGING::Error("[VFS] NDPK too small: ", ndpkPath);
        close(fd_); fd_ = -1;
        return false;
    }
    baseSize_ = static_cast<size_t>(st.st_size);
    base_ = reinterpret_cast<const uint8_t*>(
        mmap(nullptr, baseSize_, PROT_READ, MAP_PRIVATE, fd_, 0));
    if (base_ == MAP_FAILED) {
        NC::LOGGING::Error("[VFS] mmap failed for: ", ndpkPath);
        close(fd_); fd_ = -1;
        base_ = nullptr;
        return false;
    }
#endif

    const uint8_t* const b = base_;

    // Parse header
    const uint32_t magic      = ReadU32(b + 0);
    const uint32_t version    = ReadU32(b + 4);
    const uint32_t chunkCount = ReadU32(b + 8);
    const uint64_t manifestOff  = ReadU64(b + 12);
    const uint64_t manifestSize = ReadU64(b + 20);

    if (magic != kNdpkMagic) {
        NC::LOGGING::Error("[VFS] Invalid NDPK magic");
        Unmount();
        return false;
    }
    if (version != kNdpkVersion) {
        NC::LOGGING::Error("[VFS] Unsupported NDPK version: ", version);
        Unmount();
        return false;
    }

    const size_t tocStart = kHeaderSize;
    const size_t tocEnd   = tocStart + static_cast<size_t>(chunkCount) * kTocEntrySize;
    if (tocEnd > baseSize_ || manifestOff + manifestSize > baseSize_) {
        NC::LOGGING::Error("[VFS] NDPK layout out of bounds");
        Unmount();
        return false;
    }

    // Parse TOC entries into a lookup by index
    struct ChunkLoc { uint64_t offset; uint64_t size; char name[17]; };
    std::vector<ChunkLoc> toc(chunkCount);
    for (uint32_t i = 0; i < chunkCount; ++i) {
        const uint8_t* e = b + tocStart + i * kTocEntrySize;
        std::memcpy(toc[i].name, e, 16);
        toc[i].name[16] = 0;
        // flags at offset 16 (4 bytes) — not needed for mount
        toc[i].offset = ReadU64(e + 20);
        toc[i].size   = ReadU64(e + 28);
        // hash at offset 36 (8 bytes) — skip
    }

    // Parse manifest to get asset->chunkId table and build profile
    std::vector<AssetRecord> assets;
    uint8_t manifestProfile = 0;
    if (manifestSize > 0) {
        if (!ParseManifestAssets(b + static_cast<size_t>(manifestOff),
                                 static_cast<size_t>(manifestSize), assets, manifestProfile)) {
            NC::LOGGING::Error("[VFS] Failed to parse NDPK manifest");
            Unmount();
            return false;
        }
    }

    // Build entries_ map
    entries_.reserve(assets.size());
    for (const AssetRecord& ar : assets) {
        if (ar.assetPath.empty()) continue;
        if (ar.chunkId >= chunkCount) continue;
        const ChunkLoc& cl = toc[ar.chunkId];
        if (cl.offset + cl.size > baseSize_) continue;

        View v;
        v.data = b + static_cast<size_t>(cl.offset);
        v.size = static_cast<size_t>(cl.size);

        const std::string key = NormalizeImpl(ar.assetPath);
        if (!key.empty())
            entries_[key] = v;
        // Also store under the original path so direct lookups work
        entries_[ar.assetPath] = v;
    }

    // Extract startup map path from "scene" chunk
    for (uint32_t i = 0; i < chunkCount; ++i) {
        if (std::strncmp(toc[i].name, "scene", 5) == 0) {
            outStartupMapPath = ExtractMapPathFromSceneChunk(
                b + static_cast<size_t>(toc[i].offset),
                static_cast<size_t>(toc[i].size));
            break;
        }
    }

    mounted_ = true;
    profile_ = manifestProfile;
    NC::LOGGING::Log("[VFS] Mounted NDPK: ", ndpkPath,
                     " profile=", static_cast<int>(profile_),
                     " entries=", entries_.size(),
                     " startupMap=", outStartupMapPath.empty() ? "<none>" : outStartupMapPath);
    return true;
}

// ---------------------------------------------------------------------------
// Read — look up by absolute or relative path
// ---------------------------------------------------------------------------
VFS::View VFS::Read(const std::string& path) const {
    if (!mounted_ || path.empty()) return {};

    // Try exact match first
    auto it = entries_.find(path);
    if (it != entries_.end()) return it->second;

    // Try normalized key
    const std::string key = NormalizeImpl(path);
    if (!key.empty()) {
        it = entries_.find(key);
        if (it != entries_.end()) return it->second;
    }
    return {};
}

bool VFS::Exists(const std::string& path) const {
    return Read(path).valid();
}

// ---------------------------------------------------------------------------
// Unmount
// ---------------------------------------------------------------------------
void VFS::Unmount() {
    entries_.clear();
    mounted_ = false;
    profile_ = 0;
#if defined(_WIN32)
    if (base_) {
        UnmapViewOfFile(base_);
        base_ = nullptr;
    }
    if (mappingHandle_) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
    }
    if (fileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
    }
    baseSize_ = 0;
#else
    if (base_ && base_ != MAP_FAILED) {
        munmap(const_cast<void*>(reinterpret_cast<const void*>(base_)), baseSize_);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    baseSize_ = 0;
#endif
}

} // namespace NC
