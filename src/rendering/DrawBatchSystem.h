// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DRAWBATCHSYSTEM_H
#define NDEVC_DRAWBATCHSYSTEM_H

#include "glm.hpp"
#include <array>
#include <cstdint>
#include <vector>
#include <unordered_map>

#include "Rendering/Camera.h"
#include "Rendering/Mesh.h"
#include "Rendering/Rendering.h"
#include "Rendering/Interfaces/ITexture.h"
#include "Rendering/OpenGL/GLHandles.h"
#include "Rendering/Interfaces/ISampler.h"

constexpr size_t kUploadRingSize = 16;

struct BatchKey {
    NDEVC::Graphics::ITexture* tex[12];
    bool receivesDecals;
    uint32_t shaderHash;

    BatchKey() : receivesDecals(false), shaderHash(0) {
        std::fill(std::begin(tex), std::end(tex), nullptr);
    }

    bool operator==(const BatchKey& o) const {
        return std::equal(std::begin(tex), std::end(tex), std::begin(o.tex)) &&
               receivesDecals == o.receivesDecals &&
               shaderHash == o.shaderHash;
    }
};

struct BatchKeyHash {
    size_t operator()(const BatchKey& k) const {
        size_t h = k.shaderHash;
        constexpr size_t primes[] = {2654435761u, 40503u, 12345678u, 98765432u,
                                     16777619u, 33554467u, 67108879u, 134217757u,
                                     268435459u, 536870923u, 1073741827u, 2147483659u};
        for (int i = 0; i < 12; ++i) {
            h ^= std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.tex[i])) * primes[i];
        }
        h ^= std::hash<bool>{}(k.receivesDecals) * 999999937u;
        return h;
    }
};

struct DrawBatch {
    BatchKey key;
    size_t staticCommandCount = 0;
    std::vector<DrawCommand> commands;
    std::vector<float> perObjectData;
    std::vector<glm::vec4> decalParams;

    ~DrawBatch() = default;

    DrawBatch(const DrawBatch&) = delete;
    DrawBatch& operator=(const DrawBatch&) = delete;

    DrawBatch(DrawBatch&& other) noexcept
        : key(other.key)
        , staticCommandCount(other.staticCommandCount)
        , commands(std::move(other.commands))
        , perObjectData(std::move(other.perObjectData))
        , decalParams(std::move(other.decalParams))
    {
        other.staticCommandCount = 0;
    }

    DrawBatch& operator=(DrawBatch&& other) noexcept {
        if (this != &other) {
            key = other.key;
            staticCommandCount = other.staticCommandCount;
            commands = std::move(other.commands);
            perObjectData = std::move(other.perObjectData);
            decalParams = std::move(other.decalParams);
            other.staticCommandCount = 0;
        }
        return *this;
    }

    DrawBatch() = default;
    explicit DrawBatch(const BatchKey& k) : key(k) {}
};

class DrawBatchSystem {
public:
    DrawBatchSystem() = default;
    ~DrawBatchSystem() = default;

    DrawBatchSystem(const DrawBatchSystem&) = delete;
    DrawBatchSystem& operator=(const DrawBatchSystem&) = delete;

    static DrawBatchSystem& instance();
    void shutdownGL();
    bool hasGLResources() const;

    void init(const std::vector<DrawCmd>& solidDraws);
    void cull(const std::vector<DrawCmd>& solidDraws, const Camera::Frustum& frustum);

    void cullGeneric(const std::vector<DrawCmd>& draws, const Camera::Frustum& frustum,
                     uint32_t shaderHash, int numTextures = 4);

    void cullDecals(const std::vector<DrawCmd>& draws);

    struct PackedDrawRange {
        size_t staticOffset = 0;
        size_t staticCount = 0;
        size_t dynamicOffset = 0;
        size_t dynamicCount = 0;
    };

    void flush(NDEVC::Graphics::ISampler* samplerRepeat, int numTexturesToBind = 4, GLuint currentProgram = 0, bool bindlessMode = false);
    void flushDecals(NDEVC::Graphics::ISampler* samplerRepeat, NDEVC::Graphics::ISampler* samplerClamp, bool bindlessMode = false);
    void reset(bool invalidateStaticCache = false);
    void invalidateStaticCache();

    const std::deque<DrawBatch*>& activeBatches() const { return active_; }

    // ── Parity metrics: batch vs draw command count (Nebula framebatch parity) ──
    struct FlushMetrics {
        size_t batchCount = 0;
        size_t commandCount = 0;
    };
    const FlushMetrics& lastFlushMetrics() const { return lastFlushMetrics_; }

private:
    FlushMetrics lastFlushMetrics_;
    std::unordered_map<GLuint, GLint> receivesDecalsLocCache_;
    NDEVC::GL::GLBufHandle modelMatrixSSBO;
    NDEVC::GL::GLBufHandle transientModelMatrixSSBO;
    NDEVC::GL::GLBufHandle perObjectSSBO;
    NDEVC::GL::GLBufHandle indirectBuffer;
    NDEVC::GL::GLBufHandle transientIndirectBuffer;
    void* modelMatrixPersistent_ = nullptr;
    void* transientModelMatrixPersistent_ = nullptr;
    void* indirectPersistent_ = nullptr;
    void* transientIndirectPersistent_ = nullptr;
    void* decalParamsPersistent_ = nullptr;
    size_t modelMatrixSSBOCapacity_ = 0;
    size_t transientModelMatrixSSBOCapacity_ = 0;
    size_t perObjectSSBOCapacity_ = 0;
    size_t decalParamsSSBOCapacity_ = 0;
    size_t indirectBufferCapacity_ = 0;
    size_t transientIndirectBufferCapacity_ = 0;
    std::vector<glm::mat4> modelMatrices;
    std::vector<uint32_t> materialIndices;
    std::vector<float> perObjectDataBuffer;
    std::vector<glm::vec4> decalParamsBuffer;
    std::vector<DrawCommand> dynamicPackedCommands_;
    std::vector<PackedDrawRange> packedRanges_;
    NDEVC::GL::GLBufHandle decalParamsSSBO;
    NDEVC::GL::GLBufHandle materialIndexSSBO_;
    size_t materialIndexSSBOCapacity_ = 0;

    std::unordered_map<BatchKey, DrawBatch, BatchKeyHash> batches_;
    std::deque<DrawBatch*> active_;

    size_t staticMatrixCount = 0;
    std::vector<glm::mat4> staticModelMatrices;
    std::vector<uint32_t> staticMaterialIndices;
    std::unordered_map<BatchKey, std::vector<DrawCommand>, BatchKeyHash> staticBatchCommands;
    std::unordered_map<BatchKey, std::vector<size_t>, BatchKeyHash> staticBatchDrawIndices_;
    std::unordered_map<BatchKey, std::vector<std::vector<size_t>>, BatchKeyHash> staticBatchInstanceDrawIndices_;
    std::array<bool, kUploadRingSize> staticMatricesUploadedBySlot_{};
    bool initialMatrixUploadLogged = false;
    uint64_t staticCacheSignature_ = 0;
    uint64_t staticCacheContentHash_ = 0;
    size_t staticCacheSourceCount_ = 0;
    bool staticCacheValid_ = false;
    size_t dynamicSolidIndexSourceCount_ = 0;
    size_t dynamicSolidStaticSourceCount_ = 0;
    std::vector<size_t> dynamicSolidDrawIndices_;
    uint32_t modelMatrixUploadCursor_ = 0;
    uint32_t transientModelMatrixUploadCursor_ = 0;
    uint32_t perObjectUploadCursor_ = 0;
    uint32_t decalParamsUploadCursor_ = 0;
    uint32_t indirectUploadCursor_ = 0;
    uint32_t transientIndirectUploadCursor_ = 0;
    std::array<bool, kUploadRingSize> staticIndirectUploadedBySlot_{};
    std::vector<DrawCommand> staticIndirectCommandsPacked_;
    std::unordered_map<BatchKey, size_t, BatchKeyHash> staticIndirectOffsets_;
    // Parallel to staticIndirectCommandsPacked_: representative source draw index for each command.
    std::vector<size_t> staticCmdDrawIndices_;
    // Parallel to staticIndirectCommandsPacked_: flattened per-command source draw indices.
    std::vector<size_t> staticCmdInstanceOffsets_;
    std::vector<uint32_t> staticCmdInstanceCounts_;
    std::vector<size_t> staticCmdInstanceDrawIndices_;
    std::vector<glm::vec4> staticDecalParamsPacked_;

    struct DynamicGenericGroup {
        uint32_t count = 0;
        uint32_t firstIndex = 0;
        float alphaCutoff = 0.5f;
        std::vector<glm::mat4> matrices;
        std::vector<uint32_t> materialIndices;
    };
    std::unordered_map<BatchKey, std::unordered_map<uint64_t, DynamicGenericGroup>, BatchKeyHash> dynamicGroupsCache_;

public:
    // Update instanceCount in static indirect commands based on DrawCmd::disabled flags.
    // Much cheaper than invalidateStaticCache() — no matrix/batch rebuild, just toggles.
    void updateStaticVisibility(const std::vector<DrawCmd>& solidDraws);
};

#endif
