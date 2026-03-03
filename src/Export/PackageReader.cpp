// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/PackageReader.h"
#include "Export/ExportCache.h"
#include "Export/ExportTypes.h"
#include "Core/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace NDEVC::Export {
namespace {

constexpr uint32_t kPackageMagic = 0x4E44504Bu; // 'NDPK'
constexpr uint32_t kPackageVersion = 1u;
constexpr size_t kHeaderSize = 28;

struct ManifestCursor {
    const std::vector<uint8_t>& bytes;
    size_t pos = 0;

    bool ReadU8(uint8_t& out) {
        if (pos + 1 > bytes.size()) return false;
        out = bytes[pos];
        ++pos;
        return true;
    }

    bool ReadU16(uint16_t& out) {
        if (pos + 2 > bytes.size()) return false;
        out = static_cast<uint16_t>(bytes[pos]) |
              (static_cast<uint16_t>(bytes[pos + 1]) << 8u);
        pos += 2;
        return true;
    }

    bool ReadU32(uint32_t& out) {
        if (pos + 4 > bytes.size()) return false;
        out = static_cast<uint32_t>(bytes[pos]) |
              (static_cast<uint32_t>(bytes[pos + 1]) << 8u) |
              (static_cast<uint32_t>(bytes[pos + 2]) << 16u) |
              (static_cast<uint32_t>(bytes[pos + 3]) << 24u);
        pos += 4;
        return true;
    }

    bool ReadU64(uint64_t& out) {
        if (pos + 8 > bytes.size()) return false;
        out = static_cast<uint64_t>(bytes[pos]) |
              (static_cast<uint64_t>(bytes[pos + 1]) << 8u) |
              (static_cast<uint64_t>(bytes[pos + 2]) << 16u) |
              (static_cast<uint64_t>(bytes[pos + 3]) << 24u) |
              (static_cast<uint64_t>(bytes[pos + 4]) << 32u) |
              (static_cast<uint64_t>(bytes[pos + 5]) << 40u) |
              (static_cast<uint64_t>(bytes[pos + 6]) << 48u) |
              (static_cast<uint64_t>(bytes[pos + 7]) << 56u);
        pos += 8;
        return true;
    }

    bool ReadStrU16(std::string& out) {
        uint16_t len = 0;
        if (!ReadU16(len)) return false;
        if (pos + len > bytes.size()) return false;
        out.assign(reinterpret_cast<const char*>(bytes.data() + pos), len);
        pos += len;
        return true;
    }
};

bool DeserializeManifest(const std::vector<uint8_t>& bytes, PackageManifest& outManifest) {
    ManifestCursor c{bytes};

    uint32_t version = 0;
    uint64_t buildTime = 0;
    uint64_t contentHash = 0;
    uint8_t profile = 0;
    if (!c.ReadU32(version) ||
        !c.ReadU64(buildTime) ||
        !c.ReadU64(contentHash) ||
        !c.ReadU8(profile)) {
        return false;
    }

    outManifest.version = version;
    outManifest.buildTime = buildTime;
    outManifest.contentHash = contentHash;
    outManifest.profile = static_cast<ExportProfile>(profile);

    uint32_t chunkCount = 0;
    if (!c.ReadU32(chunkCount)) return false;
    outManifest.chunks.clear();
    outManifest.chunks.reserve(chunkCount);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        PackageManifest::ChunkInfo chunk;
        uint8_t preload = 0;
        uint8_t compressed = 0;
        if (!c.ReadStrU16(chunk.name) ||
            !c.ReadU64(chunk.hash) ||
            !c.ReadU64(chunk.sizeBytes) ||
            !c.ReadU64(chunk.offsetBytes) ||
            !c.ReadU8(preload) ||
            !c.ReadU8(compressed)) {
            return false;
        }
        chunk.preload = preload != 0;
        chunk.compressed = compressed != 0;
        outManifest.chunks.push_back(std::move(chunk));
    }

    uint32_t assetCount = 0;
    if (!c.ReadU32(assetCount)) return false;
    outManifest.assets.clear();
    outManifest.assets.reserve(assetCount);
    for (uint32_t i = 0; i < assetCount; ++i) {
        PackageManifest::AssetInfo asset;
        if (!c.ReadStrU16(asset.assetPath) ||
            !c.ReadU32(asset.chunkId) ||
            !c.ReadU64(asset.assetHash)) {
            return false;
        }
        outManifest.assets.push_back(std::move(asset));
    }

    return c.pos <= bytes.size();
}

std::string NormalizeAssetPath(const std::string& rawPath) {
    if (rawPath.empty()) return {};
    std::string token = rawPath;
    std::replace(token.begin(), token.end(), '\\', '/');
    while (!token.empty() && token.front() == '/') {
        token.erase(token.begin());
    }
    std::filesystem::path normalized = std::filesystem::path(token).lexically_normal();
    if (normalized.empty() || normalized.is_absolute()) return {};

    for (const auto& part : normalized) {
        const std::string seg = part.generic_string();
        if (seg == "..") {
            return {};
        }
    }

    return normalized.generic_string();
}

bool HasMapExtension(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == ".map";
}

std::string BuildMapAssetHint(const std::string& rawPath) {
    if (rawPath.empty()) return {};
    std::string token = rawPath;
    std::replace(token.begin(), token.end(), '\\', '/');

    std::string lowered = token;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const size_t markerPos = lowered.find("/maps/");
    if (markerPos != std::string::npos) {
        return NormalizeAssetPath(token.substr(markerPos + 1));
    }
    return NormalizeAssetPath(token);
}

bool TryReadSceneChunkMapPath(std::ifstream& in,
                              uint64_t fileSize,
                              const PackageManifest::ChunkInfo& sceneChunk,
                              std::string& outMapPath) {
    if (sceneChunk.offsetBytes > fileSize || sceneChunk.sizeBytes > fileSize ||
        sceneChunk.offsetBytes + sceneChunk.sizeBytes > fileSize || sceneChunk.sizeBytes < 16) {
        return false;
    }
    std::vector<uint8_t> chunkBytes(static_cast<size_t>(sceneChunk.sizeBytes));
    in.clear();
    in.seekg(static_cast<std::streamoff>(sceneChunk.offsetBytes), std::ios::beg);
    in.read(reinterpret_cast<char*>(chunkBytes.data()), static_cast<std::streamsize>(chunkBytes.size()));
    if (in.gcount() != static_cast<std::streamsize>(chunkBytes.size())) {
        return false;
    }

    static constexpr char kSceneTag[] = "NDSCENE2";
    if (chunkBytes.size() < 8 || std::memcmp(chunkBytes.data(), kSceneTag, 8) != 0) {
        return false;
    }

    ManifestCursor c{chunkBytes};
    c.pos = 8; // skip "NDSCENE2" tag
    uint32_t sceneVersion = 0;
    std::string sceneGuid;
    std::string sceneName;
    std::string mapName;
    std::string mapPath;
    if (!c.ReadU32(sceneVersion) ||
        !c.ReadStrU16(sceneGuid) ||
        !c.ReadStrU16(sceneName) ||
        !c.ReadStrU16(mapName) ||
        !c.ReadStrU16(mapPath)) {
        return false;
    }
    outMapPath = mapPath;
    return !outMapPath.empty();
}

} // namespace

PackageMountResult MountPackageToDirectory(const std::string& packagePath,
                                           const std::string& outputDir) {
    namespace fs = std::filesystem;
    PackageMountResult result;
    result.packagePath = packagePath;
    result.mountRoot = outputDir;

    if (packagePath.empty()) {
        result.errorMsg = "package path is empty";
        return result;
    }
    if (outputDir.empty()) {
        result.errorMsg = "output directory is empty";
        return result;
    }

    std::error_code ec;
    const fs::path pkgPath = fs::path(packagePath).lexically_normal();
    if (!fs::exists(pkgPath, ec) || !fs::is_regular_file(pkgPath, ec)) {
        result.errorMsg = "package file not found: " + pkgPath.string();
        return result;
    }

    const uint64_t fileSize = fs::file_size(pkgPath, ec);
    if (ec || fileSize < kHeaderSize) {
        result.errorMsg = "invalid package size";
        return result;
    }

    std::ifstream in(pkgPath, std::ios::binary);
    if (!in.is_open()) {
        result.errorMsg = "failed to open package file";
        return result;
    }

    std::array<uint8_t, kHeaderSize> header{};
    in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (in.gcount() != static_cast<std::streamsize>(header.size())) {
        result.errorMsg = "failed to read package header";
        return result;
    }

    auto readHeaderU32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(header[off]) |
               (static_cast<uint32_t>(header[off + 1]) << 8u) |
               (static_cast<uint32_t>(header[off + 2]) << 16u) |
               (static_cast<uint32_t>(header[off + 3]) << 24u);
    };
    auto readHeaderU64 = [&](size_t off) -> uint64_t {
        return static_cast<uint64_t>(header[off]) |
               (static_cast<uint64_t>(header[off + 1]) << 8u) |
               (static_cast<uint64_t>(header[off + 2]) << 16u) |
               (static_cast<uint64_t>(header[off + 3]) << 24u) |
               (static_cast<uint64_t>(header[off + 4]) << 32u) |
               (static_cast<uint64_t>(header[off + 5]) << 40u) |
               (static_cast<uint64_t>(header[off + 6]) << 48u) |
               (static_cast<uint64_t>(header[off + 7]) << 56u);
    };

    const uint32_t magic = readHeaderU32(0);
    const uint32_t version = readHeaderU32(4);
    const uint32_t chunkCount = readHeaderU32(8);
    const uint64_t manifestOffset = readHeaderU64(12);
    const uint64_t manifestSize = readHeaderU64(20);

    if (magic != kPackageMagic) {
        result.errorMsg = "invalid package magic";
        return result;
    }
    if (version != kPackageVersion) {
        result.errorMsg = "unsupported package version";
        return result;
    }
    if (manifestOffset > fileSize || manifestSize > fileSize || manifestOffset + manifestSize > fileSize) {
        result.errorMsg = "invalid package manifest range";
        return result;
    }

    std::vector<uint8_t> manifestBytes(static_cast<size_t>(manifestSize));
    if (manifestSize > 0) {
        in.seekg(static_cast<std::streamoff>(manifestOffset), std::ios::beg);
        in.read(reinterpret_cast<char*>(manifestBytes.data()), static_cast<std::streamsize>(manifestBytes.size()));
        if (in.gcount() != static_cast<std::streamsize>(manifestBytes.size())) {
            result.errorMsg = "failed to read package manifest";
            return result;
        }
    }

    PackageManifest manifest;
    if (!DeserializeManifest(manifestBytes, manifest)) {
        result.errorMsg = "failed to parse package manifest";
        return result;
    }
    if (manifest.chunks.size() != chunkCount) {
        result.errorMsg = "package chunk count mismatch";
        return result;
    }

    std::string preferredMapAssetPath;
    for (const PackageManifest::ChunkInfo& chunk : manifest.chunks) {
        if (chunk.name == "scene") {
            std::string sceneMapPath;
            if (TryReadSceneChunkMapPath(in, fileSize, chunk, sceneMapPath)) {
                preferredMapAssetPath = BuildMapAssetHint(sceneMapPath);
            }
            break;
        }
    }

    fs::path mountPath = fs::path(outputDir).lexically_normal();
    if (mountPath.empty() || mountPath == mountPath.root_path()) {
        result.errorMsg = "refusing to mount package to unsafe root path";
        return result;
    }
    fs::path canonicalMount = fs::weakly_canonical(mountPath, ec);
    if (ec) {
        ec.clear();
        canonicalMount = mountPath;
    }
    fs::path currentDir = fs::current_path(ec);
    if (ec) {
        ec.clear();
        currentDir.clear();
    }
    const fs::path packageParent = pkgPath.parent_path();
    if (canonicalMount == canonicalMount.root_path() ||
        (!currentDir.empty() && canonicalMount == currentDir) ||
        (!packageParent.empty() && canonicalMount == packageParent)) {
        result.errorMsg = "refusing to mount package to unsafe root path";
        return result;
    }
    mountPath = canonicalMount;
    fs::remove_all(mountPath, ec);
    ec.clear();
    fs::create_directories(mountPath, ec);
    if (ec) {
        result.errorMsg = "failed to create package mount directory: " + ec.message();
        return result;
    }

    std::unordered_map<uint32_t, fs::path> extractedChunkPath;
    std::vector<uint8_t> ioBuffer(1024 * 1024);

    for (const PackageManifest::AssetInfo& asset : manifest.assets) {
        if (asset.assetPath.empty()) {
            continue;
        }
        if (asset.chunkId >= manifest.chunks.size()) {
            result.errorMsg = "invalid chunk id in asset manifest";
            return result;
        }

        const std::string relative = NormalizeAssetPath(asset.assetPath);
        if (relative.empty()) {
            result.errorMsg = "invalid asset path in package: " + asset.assetPath;
            return result;
        }
        const fs::path outPath = mountPath / fs::path(relative);
        const fs::path parentPath = outPath.parent_path();
        if (!parentPath.empty()) {
            fs::create_directories(parentPath, ec);
            if (ec) {
                result.errorMsg = "failed creating mount directories: " + ec.message();
                return result;
            }
        }

        auto extractedIt = extractedChunkPath.find(asset.chunkId);
        if (extractedIt != extractedChunkPath.end()) {
            ec.clear();
            if (!fs::equivalent(extractedIt->second, outPath, ec) || ec) {
                ec.clear();
                fs::copy_file(extractedIt->second, outPath, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    result.errorMsg = "failed duplicating mounted chunk to path: " + outPath.string();
                    return result;
                }
            }
            const bool isMap = HasMapExtension(relative);
            if (isMap &&
                (!preferredMapAssetPath.empty() && preferredMapAssetPath == relative)) {
                result.startupMapPath = outPath.string();
            } else if (result.startupMapPath.empty() && isMap) {
                result.startupMapPath = outPath.string();
            }
            continue;
        }

        const PackageManifest::ChunkInfo& chunk = manifest.chunks[asset.chunkId];
        if (chunk.compressed) {
            result.errorMsg = "compressed package chunks are not supported by runtime";
            return result;
        }
        if (chunk.offsetBytes > fileSize || chunk.sizeBytes > fileSize ||
            chunk.offsetBytes + chunk.sizeBytes > fileSize) {
            result.errorMsg = "invalid chunk range in package manifest";
            return result;
        }

        in.clear();
        in.seekg(static_cast<std::streamoff>(chunk.offsetBytes), std::ios::beg);
        if (!in.good()) {
            result.errorMsg = "failed seeking package chunk";
            return result;
        }

        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            result.errorMsg = "failed opening output asset path: " + outPath.string();
            return result;
        }

        uint64_t remaining = chunk.sizeBytes;
        uint64_t rollingHash = 0xcbf29ce484222325ull;
        while (remaining > 0) {
            const size_t request = static_cast<size_t>(
                std::min<uint64_t>(remaining, static_cast<uint64_t>(ioBuffer.size())));
            in.read(reinterpret_cast<char*>(ioBuffer.data()), static_cast<std::streamsize>(request));
            const std::streamsize readCount = in.gcount();
            if (readCount <= 0) {
                result.errorMsg = "failed reading package chunk bytes";
                return result;
            }
            out.write(reinterpret_cast<const char*>(ioBuffer.data()), readCount);
            if (!out.good()) {
                result.errorMsg = "failed writing mounted asset path";
                return result;
            }
            rollingHash = ExportCache::HashBytes(ioBuffer.data(), static_cast<size_t>(readCount), rollingHash);
            remaining -= static_cast<uint64_t>(readCount);
        }
        out.flush();
        out.close();

        if (chunk.hash != 0 && rollingHash != chunk.hash) {
            result.errorMsg = "package chunk hash mismatch while mounting";
            return result;
        }
        if (asset.assetHash != 0 && rollingHash != asset.assetHash) {
            result.errorMsg = "package asset hash mismatch while mounting";
            return result;
        }

        extractedChunkPath.emplace(asset.chunkId, outPath);
        ++result.extractedAssetCount;
        const bool isMap = HasMapExtension(relative);
        if (isMap &&
            (!preferredMapAssetPath.empty() && preferredMapAssetPath == relative)) {
            result.startupMapPath = outPath.string();
        } else if (result.startupMapPath.empty() && isMap) {
            result.startupMapPath = outPath.string();
        }
    }

    result.success = true;
    NC::LOGGING::Log("[PACKAGE] Mounted package path=", pkgPath.string(),
                     " root=", mountPath.string(),
                     " assets=", result.extractedAssetCount,
                     " startupMap=", result.startupMapPath.empty() ? "<none>" : result.startupMapPath);
    return result;
}

} // namespace NDEVC::Export
