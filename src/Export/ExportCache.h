// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EXPORT_CACHE_H
#define NDEVC_EXPORT_CACHE_H

#include "Export/ExportTypes.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// ExportCache — deterministic content-addressed cache
//
// Key = FNV-1a64(file_bytes) XOR FNV-1a64(CookSettings_bytes)
// Backed by a flat binary index file (NDEC format) in the cache directory.
// Same input + same settings → same key → cache hit → skip re-cooking.
// ---------------------------------------------------------------------------
class ExportCache {
public:
    struct Entry {
        uint64_t    key             = 0;
        uint64_t    contentHash     = 0;
        uint64_t    settingsHash    = 0;
        std::string outputPath;
        uint64_t    outputSizeBytes = 0;
        int64_t     timestamp       = 0; // unix epoch seconds
        bool        valid           = false;
    };

    explicit ExportCache(std::string cacheDir);

    // Load/save the flat binary index. Load returns false if no index exists yet (first run).
    bool Load();
    void Save() const;

    bool  HasValid(uint64_t key) const;
    Entry Get(uint64_t key) const;
    void  Store(uint64_t key, const Entry& entry);
    void  Invalidate(uint64_t key);
    void  Clear();

    int EntryCount() const { return static_cast<int>(entries_.size()); }

    // ---- Hash utilities ---------------------------------------------------
    // FNV-1a 64-bit over arbitrary bytes
    static uint64_t HashBytes(const void* data, size_t len,
                              uint64_t seed = 0xcbf29ce484222325ull);

    // Hash the full contents of a file on disk; returns 0 on read failure
    static uint64_t HashFile(const std::string& path);

    // Hash the bytes of a CookSettings struct (POD-style, padding included)
    static uint64_t HashSettings(const CookSettings& s);

    // Combine content + settings into one cache key
    static uint64_t MakeKey(uint64_t contentHash, uint64_t settingsHash);

private:
    static constexpr uint32_t kMagic   = 0x4E444543u; // 'N','D','E','C'
    static constexpr uint32_t kVersion = 1u;

    std::string cacheDir_;
    std::string indexPath_;
    std::unordered_map<uint64_t, Entry> entries_;
    mutable bool dirty_ = false;
};

} // namespace NDEVC::Export

#endif // NDEVC_EXPORT_CACHE_H
