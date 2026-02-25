// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DRAWBATCHSYSTEM_H
#define NDEVC_DRAWBATCHSYSTEM_H

#include "glad/glad.h"
#include "glm.hpp"
#include <array>
#include <vector>
#include <unordered_map>

#include "Camera.h"
#include "Mesh.h"
#include "Rendering.h"

constexpr size_t kUploadRingSize = 16;

struct BatchKey {
    uint32_t tex[12];
    bool receivesDecals;
    uint32_t shaderHash;

    BatchKey() : receivesDecals(false), shaderHash(0) {
        std::fill(std::begin(tex), std::end(tex), 0);
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
            h ^= std::hash<uint32_t>{}(k.tex[i]) * primes[i];
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

    void flush(GLuint samplerRepeat, int numTexturesToBind = 4);
    void flushDecals(GLuint samplerRepeat, GLuint samplerClamp);
    void reset(bool invalidateStaticCache = false);

    const std::deque<DrawBatch*>& activeBatches() const { return active_; }

private:
    GLuint modelMatrixSSBO = 0;
    GLuint transientModelMatrixSSBO = 0;
    GLuint perObjectSSBO = 0;
    GLuint indirectBuffer = 0;
    GLuint transientIndirectBuffer = 0;
    size_t modelMatrixSSBOCapacity_ = 0;
    size_t transientModelMatrixSSBOCapacity_ = 0;
    size_t perObjectSSBOCapacity_ = 0;
    size_t decalParamsSSBOCapacity_ = 0;
    size_t indirectBufferCapacity_ = 0;
    size_t transientIndirectBufferCapacity_ = 0;
    std::vector<glm::mat4> modelMatrices;
    std::vector<float> perObjectDataBuffer;
    std::vector<glm::vec4> decalParamsBuffer;
    GLuint decalParamsSSBO = 0;

    std::unordered_map<BatchKey, DrawBatch, BatchKeyHash> batches_;
    std::deque<DrawBatch*> active_;

    size_t staticMatrixCount = 0;
    std::vector<glm::mat4> staticModelMatrices;
    std::unordered_map<BatchKey, std::vector<DrawCommand>, BatchKeyHash> staticBatchCommands;
    std::array<bool, kUploadRingSize> staticMatricesUploadedBySlot_{};
    bool initialMatrixUploadLogged = false;
    uint32_t modelMatrixUploadCursor_ = 0;
    uint32_t transientModelMatrixUploadCursor_ = 0;
    uint32_t perObjectUploadCursor_ = 0;
    uint32_t decalParamsUploadCursor_ = 0;
    uint32_t indirectUploadCursor_ = 0;
    uint32_t transientIndirectUploadCursor_ = 0;
    std::array<bool, kUploadRingSize> staticIndirectUploadedBySlot_{};
    std::vector<DrawCommand> staticIndirectCommandsPacked_;
    std::unordered_map<BatchKey, size_t, BatchKeyHash> staticIndirectOffsets_;
};

#endif
