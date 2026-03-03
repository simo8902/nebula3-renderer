// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EXPORT_PACKAGE_H
#define NDEVC_EXPORT_PACKAGE_H

#include "Export/ExportTypes.h"
#include "Export/ExportCache.h"
#include <string>
#include <vector>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// PackageWriter — produces a deterministic NDPK binary archive
//
// Format:
//   [Header] [ManifestBytes] [TOC] [Chunk0][Chunk1]...[ChunkN]
//
// Header  : magic 'NDPK', version uint32, chunkCount uint32,
//           manifestOffset uint64, manifestSize uint64
// TOC     : chunkCount × (name[16], flags uint32, offset uint64,
//                          size uint64, hash uint64)  = 44 bytes/entry
// Data    : raw chunk bytes in TOC order
// Manifest: binary-serialized PackageManifest appended at manifestOffset
//
// The manifest is written last so the TOC offsets are known before it.
// ---------------------------------------------------------------------------
class PackageWriter {
public:
    struct Chunk {
        std::string          name;
        std::vector<uint8_t> data;
        uint64_t             hash    = 0;
        bool                 preload = false;
        bool                 compressed = false;
    };

    explicit PackageWriter(ExportProfile profile);

    // Stage a named chunk. Name is truncated to 15 chars.
    // hash is computed from data bytes using FNV-1a64.
    void AddChunk(const std::string& name, std::vector<uint8_t> data,
                  bool preload = false, bool compressed = false);

    // Write the completed archive to outputPath.
    // Returns false on I/O failure. Updates manifest with offsets.
    bool Finalize(const std::string& outputPath, PackageManifest& manifest);

    // Serialization helpers — exposed for OptimizeNode/PackageNode cross-use
    static std::vector<uint8_t> SerializePVS(const PVSData& pvs);
    static std::vector<uint8_t> SerializeHLOD(const HLODData& hlod);
    static std::vector<uint8_t> SerializeManifest(const PackageManifest& m);

private:
    static constexpr uint32_t kMagic   = 0x4E44504Bu; // 'N','D','P','K'
    static constexpr uint32_t kVersion = 1u;
    static constexpr int      kNameMax = 16;           // incl. null terminator

    ExportProfile         profile_;
    std::vector<Chunk>    chunks_;
};

} // namespace NDEVC::Export

#endif // NDEVC_EXPORT_PACKAGE_H
