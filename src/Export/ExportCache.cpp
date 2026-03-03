// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/ExportCache.h"
#include "Core/Logger.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
ExportCache::ExportCache(std::string cacheDir)
    : cacheDir_(std::move(cacheDir))
    , indexPath_(cacheDir_ + "/export_cache.ndec")
{}

// ---------------------------------------------------------------------------
uint64_t ExportCache::HashBytes(const void* data, size_t len, uint64_t h) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

uint64_t ExportCache::HashFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return 0;
    const auto size = f.tellg();
    if (size <= 0) return HashBytes(nullptr, 0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return HashBytes(buf.data(), buf.size());
}

uint64_t ExportCache::HashSettings(const CookSettings& s) {
    return HashBytes(&s, sizeof(CookSettings));
}

uint64_t ExportCache::MakeKey(uint64_t contentHash, uint64_t settingsHash) {
    // XOR-combine; good enough for a cache discriminator
    return contentHash ^ (settingsHash * 0x9e3779b97f4a7c15ull);
}

// ---------------------------------------------------------------------------
bool ExportCache::Load() {
    std::ifstream f(indexPath_, std::ios::binary);
    if (!f.is_open()) return false;

    uint32_t magic = 0, version = 0, count = 0;
    f.read(reinterpret_cast<char*>(&magic),   sizeof(magic));
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&count),   sizeof(count));

    if (magic != kMagic || version != kVersion) {
        NC::LOGGING::Warning("[EXPORT_CACHE] Index has incompatible magic/version, discarding");
        return false;
    }

    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Entry e;
        f.read(reinterpret_cast<char*>(&e.key),             sizeof(e.key));
        f.read(reinterpret_cast<char*>(&e.contentHash),     sizeof(e.contentHash));
        f.read(reinterpret_cast<char*>(&e.settingsHash),    sizeof(e.settingsHash));
        f.read(reinterpret_cast<char*>(&e.outputSizeBytes), sizeof(e.outputSizeBytes));
        f.read(reinterpret_cast<char*>(&e.timestamp),       sizeof(e.timestamp));

        uint8_t validByte = 0;
        f.read(reinterpret_cast<char*>(&validByte), 1);
        e.valid = (validByte != 0);

        uint16_t pathLen = 0;
        f.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        if (pathLen > 0) {
            e.outputPath.resize(pathLen);
            f.read(e.outputPath.data(), pathLen);
        }

        if (!f) break;
        entries_[e.key] = std::move(e);
    }

    dirty_ = false;
    NC::LOGGING::Log("[EXPORT_CACHE] Loaded ", entries_.size(), " entries from ", indexPath_);
    return true;
}

void ExportCache::Save() const {
    if (!dirty_) return;

    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);

    std::ofstream f(indexPath_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        NC::LOGGING::Error("[EXPORT_CACHE] Cannot open index for write: ", indexPath_);
        return;
    }

    const uint32_t count = static_cast<uint32_t>(entries_.size());
    f.write(reinterpret_cast<const char*>(&kMagic),   sizeof(kMagic));
    f.write(reinterpret_cast<const char*>(&kVersion), sizeof(kVersion));
    f.write(reinterpret_cast<const char*>(&count),    sizeof(count));

    for (const auto& [key, e] : entries_) {
        f.write(reinterpret_cast<const char*>(&e.key),             sizeof(e.key));
        f.write(reinterpret_cast<const char*>(&e.contentHash),     sizeof(e.contentHash));
        f.write(reinterpret_cast<const char*>(&e.settingsHash),    sizeof(e.settingsHash));
        f.write(reinterpret_cast<const char*>(&e.outputSizeBytes), sizeof(e.outputSizeBytes));
        f.write(reinterpret_cast<const char*>(&e.timestamp),       sizeof(e.timestamp));
        const uint8_t validByte = e.valid ? 1 : 0;
        f.write(reinterpret_cast<const char*>(&validByte), 1);
        const auto pathLen = static_cast<uint16_t>(e.outputPath.size());
        f.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        if (pathLen > 0) f.write(e.outputPath.data(), pathLen);
    }

    dirty_ = false;
    NC::LOGGING::Log("[EXPORT_CACHE] Saved ", count, " entries to ", indexPath_);
}

// ---------------------------------------------------------------------------
bool ExportCache::HasValid(uint64_t key) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    if (!it->second.valid) return false;
    // Sanity: check output file still exists
    if (!it->second.outputPath.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(it->second.outputPath, ec)) return false;
    }
    return true;
}

ExportCache::Entry ExportCache::Get(uint64_t key) const {
    auto it = entries_.find(key);
    return (it != entries_.end()) ? it->second : Entry{};
}

void ExportCache::Store(uint64_t key, const Entry& entry) {
    entries_[key] = entry;
    dirty_ = true;
}

void ExportCache::Invalidate(uint64_t key) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second.valid = false;
        dirty_ = true;
    }
}

void ExportCache::Clear() {
    entries_.clear();
    dirty_ = true;
}

} // namespace NDEVC::Export
