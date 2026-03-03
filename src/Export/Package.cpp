// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/Package.h"
#include "Export/ExportCache.h"
#include "Core/Logger.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>

namespace NDEVC::Export {

PackageWriter::PackageWriter(ExportProfile profile)
    : profile_(profile)
{}

// ---------------------------------------------------------------------------
// AddChunk — buffer a named chunk and compute its hash
// ---------------------------------------------------------------------------
void PackageWriter::AddChunk(const std::string& name,
                             std::vector<uint8_t> data,
                             bool preload,
                             bool compressed)
{
    Chunk c;
    c.name       = name.substr(0, kNameMax - 1);
    c.hash       = ExportCache::HashBytes(data.data(), data.size());
    c.preload    = preload;
    c.compressed = compressed;
    c.data       = std::move(data);
    chunks_.push_back(std::move(c));
}

// ---------------------------------------------------------------------------
// SerializePVS — flat bit-matrix encoding of cell-to-cell visibility
//
// Wire format:
//   uint32_t cellCount
//   uint32_t bitBytes   = (cellCount*cellCount + 7) / 8
//   uint8_t  bits[bitBytes]   — bit[src*cellCount + dst] = 1 if visible
// ---------------------------------------------------------------------------
std::vector<uint8_t> PackageWriter::SerializePVS(const PVSData& pvs)
{
    if (!pvs.valid || pvs.cellCount <= 0) return {};

    const int N = pvs.cellCount;
    const uint32_t bitBytes = (static_cast<uint32_t>(N) * N + 7u) / 8u;

    std::vector<uint8_t> out;
    out.reserve(8 + bitBytes);

    // Header
    const uint32_t n32 = static_cast<uint32_t>(N);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((n32 >> (i * 8)) & 0xFF));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((bitBytes >> (i * 8)) & 0xFF));

    // Bit matrix
    std::vector<uint8_t> bits(bitBytes, 0u);
    for (int src = 0; src < N; ++src) {
        if (src >= static_cast<int>(pvs.cellVisibility.size())) break;
        for (int dst : pvs.cellVisibility[src]) {
            if (dst < 0 || dst >= N) continue;
            const uint32_t bit = static_cast<uint32_t>(src) * N + dst;
            bits[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        }
    }
    out.insert(out.end(), bits.begin(), bits.end());
    return out;
}

// ---------------------------------------------------------------------------
// SerializeHLOD — per-cell impostor records
//
// Wire format:
//   int32_t  highDetailRadiusChunks
//   uint32_t cellCount
//   per cell: int32_t cellIndex, float[3] boundsMin, float[3] boundsMax, int32_t drawCount
// ---------------------------------------------------------------------------
std::vector<uint8_t> PackageWriter::SerializeHLOD(const HLODData& hlod)
{
    if (!hlod.valid || hlod.cells.empty()) return {};

    const uint32_t N = static_cast<uint32_t>(hlod.cells.size());
    // 4 + 4 + N*(4 + 24 + 4) = 8 + N*32
    const size_t totalBytes = 8 + static_cast<size_t>(N) * 32;
    std::vector<uint8_t> out;
    out.resize(totalBytes, 0);

    uint8_t* p = out.data();
    auto writeI32 = [&](int32_t v) {
        std::memcpy(p, &v, 4); p += 4;
    };
    auto writeU32 = [&](uint32_t v) {
        std::memcpy(p, &v, 4); p += 4;
    };
    auto writeF32 = [&](float v) {
        std::memcpy(p, &v, 4); p += 4;
    };

    writeI32(hlod.highDetailRadiusChunks);
    writeU32(N);
    for (const HLODCell& c : hlod.cells) {
        writeI32(c.cellIndex);
        writeF32(c.boundsMin.x); writeF32(c.boundsMin.y); writeF32(c.boundsMin.z);
        writeF32(c.boundsMax.x); writeF32(c.boundsMax.y); writeF32(c.boundsMax.z);
        writeI32(c.drawCount);
    }
    return out;
}

// ---------------------------------------------------------------------------
// SerializeManifest — binary encoding of PackageManifest
// ---------------------------------------------------------------------------
std::vector<uint8_t> PackageWriter::SerializeManifest(const PackageManifest& m)
{
    std::vector<uint8_t> out;
    auto writeU32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    };
    auto writeU64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    };
    auto writeU8 = [&](uint8_t v) { out.push_back(v); };
    auto writeStr = [&](const std::string& s) {
        const uint16_t len = static_cast<uint16_t>(s.size() > 65535 ? 65535 : s.size());
        out.push_back(static_cast<uint8_t>(len & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.insert(out.end(), s.begin(), s.begin() + len);
    };

    writeU32(m.version);
    writeU64(m.buildTime);
    writeU64(m.contentHash);
    writeU8(static_cast<uint8_t>(m.profile));

    writeU32(static_cast<uint32_t>(m.chunks.size()));
    for (const PackageManifest::ChunkInfo& c : m.chunks) {
        writeStr(c.name);
        writeU64(c.hash);
        writeU64(c.sizeBytes);
        writeU64(c.offsetBytes);
        writeU8(c.preload ? 1u : 0u);
        writeU8(c.compressed ? 1u : 0u);
    }

    writeU32(static_cast<uint32_t>(m.assets.size()));
    for (const PackageManifest::AssetInfo& a : m.assets) {
        writeStr(a.assetPath);
        writeU32(a.chunkId);
        writeU64(a.assetHash);
    }

    return out;
}

// ---------------------------------------------------------------------------
// Finalize — write the complete archive to disk
// ---------------------------------------------------------------------------
bool PackageWriter::Finalize(const std::string& outputPath, PackageManifest& manifest)
{
    // Build time
    const uint64_t buildTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Compute layout:
    // [Header: 28 bytes]
    // [TOC: chunks_.size() * 44 bytes]
    // [Chunk data: variable]
    // [Manifest bytes: appended at end]

    constexpr uint64_t kHeaderSize = 28; // magic+ver+count+manifestOffset+manifestSize
    const uint64_t     tocSize     = static_cast<uint64_t>(chunks_.size()) * 44;
    uint64_t           dataOffset  = kHeaderSize + tocSize;

    // Assign offsets and populate manifest chunk entries
    manifest.version   = PackageManifest::kCurrentVersion;
    manifest.buildTime = buildTime;
    manifest.profile   = profile_;
    manifest.chunks.clear();
    manifest.chunks.reserve(chunks_.size());

    uint64_t contentHash = 0;
    for (const Chunk& c : chunks_) {
        PackageManifest::ChunkInfo ci;
        ci.name       = c.name;
        ci.hash       = c.hash;
        ci.sizeBytes  = static_cast<uint64_t>(c.data.size());
        ci.offsetBytes= dataOffset;
        ci.preload    = c.preload;
        ci.compressed = c.compressed;
        manifest.chunks.push_back(ci);
        dataOffset += ci.sizeBytes;
        contentHash ^= c.hash;
    }
    manifest.contentHash = contentHash;

    // Serialize manifest now that offsets are known
    const std::vector<uint8_t> manifestBytes = SerializeManifest(manifest);
    const uint64_t manifestOffset = dataOffset;
    const uint64_t manifestSize   = static_cast<uint64_t>(manifestBytes.size());

    std::ofstream f(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        NC::LOGGING::Error("[PACKAGE] Failed to open output: ", outputPath);
        return false;
    }

    auto writeU32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU64 = [&](uint64_t v) { f.write(reinterpret_cast<const char*>(&v), 8); };

    // Header (28 bytes)
    writeU32(kMagic);
    writeU32(kVersion);
    writeU32(static_cast<uint32_t>(chunks_.size()));
    writeU64(manifestOffset);
    writeU64(manifestSize);

    // TOC: 44 bytes per entry (name[16], flags uint32, offset uint64, size uint64, hash uint64)
    for (const PackageManifest::ChunkInfo& ci : manifest.chunks) {
        char name16[16]{};
        const size_t copyLen = std::min(ci.name.size(), static_cast<size_t>(15));
        std::memcpy(name16, ci.name.c_str(), copyLen);
        f.write(name16, 16);

        uint32_t flags = 0;
        if (ci.preload)    flags |= 0x01u;
        if (ci.compressed) flags |= 0x02u;
        writeU32(flags);
        writeU64(ci.offsetBytes);
        writeU64(ci.sizeBytes);
        writeU64(ci.hash);
    }

    // Chunk data
    for (const Chunk& c : chunks_) {
        if (!c.data.empty()) {
            f.write(reinterpret_cast<const char*>(c.data.data()),
                    static_cast<std::streamsize>(c.data.size()));
        }
    }

    // Manifest bytes (at end)
    if (!manifestBytes.empty()) {
        f.write(reinterpret_cast<const char*>(manifestBytes.data()),
                static_cast<std::streamsize>(manifestBytes.size()));
    }

    f.flush();
    const bool ok = f.good();
    f.close();

    if (ok) {
        NC::LOGGING::Log("[PACKAGE] Wrote ", outputPath,
                         " chunks=", chunks_.size(),
                         " dataSize=", dataOffset - kHeaderSize - tocSize, "B",
                         " manifestSize=", manifestSize, "B",
                         " totalSize=", manifestOffset + manifestSize, "B");
    } else {
        NC::LOGGING::Error("[PACKAGE] I/O error writing ", outputPath);
    }
    return ok;
}

} // namespace NDEVC::Export
