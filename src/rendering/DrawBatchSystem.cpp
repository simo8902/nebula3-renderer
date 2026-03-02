// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DrawBatchSystem.h"
#include "Rendering/Rendering.h"
#include "Rendering/MegaBuffer.h"
#include "glad/glad.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace {
inline bool IsFrustumCullingDisabled() {
    static const bool disabled = []() {
#if defined(_WIN32)
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, "NDEVC_DISABLE_FRUSTUM_CULLING") != 0 || value == nullptr) {
            return false;
        }
        const bool result = value[0] != '\0' && value[0] != '0';
        std::free(value);
        return result;
#else
        const char* value = std::getenv("NDEVC_DISABLE_FRUSTUM_CULLING");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
    }();
    return disabled;
}

inline bool UseStaticBatchCache() {
    // Nebula-style visibility path: static batch cache is opt-in only.
    // Set NDEVC_ENABLE_STATIC_BATCH_CACHE=1 to enable cache mode.
    static const bool enabled = []() {
#if defined(_WIN32)
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, "NDEVC_ENABLE_STATIC_BATCH_CACHE") != 0 || value == nullptr) {
            return false;
        }
        const bool result = value[0] != '\0' && value[0] != '0';
        std::free(value);
        return result;
#else
        const char* value = std::getenv("NDEVC_ENABLE_STATIC_BATCH_CACHE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
    }();
    return enabled;
}

inline uint64_t MixHash64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

inline void HashCombine64(uint64_t& seed, uint64_t value) {
    const uint64_t mixed = MixHash64(value + 0x9e3779b97f4a7c15ull);
    seed ^= mixed + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

inline uint64_t HashPointer64(const void* ptr) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
}

inline uint64_t HashFloatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<uint64_t>(bits);
}

inline uint64_t HashMatrix4(const glm::mat4& matrix) {
    uint64_t hash = 0x6f3d9b6f3d9b6f3dull;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            HashCombine64(hash, HashFloatBits(matrix[c][r]));
        }
    }
    return hash;
}

inline void EnsureCullBounds(const DrawCmd& obj, glm::vec3& worldCenter, float& radius) {
    const uint64_t transformHash = HashMatrix4(obj.worldMatrix);
    if (obj.cullBoundsValid && obj.cullTransformHash == transformHash) {
        worldCenter = obj.cullWorldCenter;
        radius = obj.cullWorldRadius;
        return;
    }

    const glm::vec3 localCenter = (glm::vec3(obj.localBoxMin) + glm::vec3(obj.localBoxMax)) * 0.5f;
    const glm::mat4& worldMat = obj.worldMatrix;
    worldCenter = glm::vec3(worldMat * glm::vec4(localCenter, 1.0f));
    const float localRadius = glm::length(glm::vec3(obj.localBoxMax) - glm::vec3(obj.localBoxMin)) * 0.5f;
    const float sx = glm::length(glm::vec3(worldMat[0]));
    const float sy = glm::length(glm::vec3(worldMat[1]));
    const float sz = glm::length(glm::vec3(worldMat[2]));
    radius = localRadius * std::max(sx, std::max(sy, sz));

    obj.cullWorldCenter = worldCenter;
    obj.cullWorldRadius = radius;
    obj.cullBoundsValid = true;
    obj.cullTransformHash = transformHash;
}

inline bool IsValidGroupForDraw(const DrawCmd& obj, const Nvx2Group& g) {
    if (!obj.mesh) return false;

    const uint64_t first = static_cast<uint64_t>(g.firstIndex());
    const uint64_t count = static_cast<uint64_t>(g.indexCount());
    const uint64_t total = static_cast<uint64_t>(obj.mesh->idx.size());
    constexpr uint64_t kMaxSafeIndexCountPerDraw = 10ull * 1024ull * 1024ull;

    if (count == 0) return false;
    if (count > kMaxSafeIndexCountPerDraw) return false;
    if (first >= total) return false;
    if (first + count > total) return false;
    return true;
}

inline bool BatchKeyLess(const BatchKey& a, const BatchKey& b) {
    if (a.shaderHash != b.shaderHash) return a.shaderHash < b.shaderHash;
    if (a.receivesDecals != b.receivesDecals) return a.receivesDecals < b.receivesDecals;
    for (int i = 0; i < 12; ++i) {
        if (a.tex[i] != b.tex[i]) return a.tex[i] < b.tex[i];
    }
    return false;
}

inline bool BatchPtrLess(const DrawBatch* a, const DrawBatch* b) {
    if (a == b) return false;
    if (a == nullptr) return true;
    if (b == nullptr) return false;
    if (BatchKeyLess(a->key, b->key)) return true;
    if (BatchKeyLess(b->key, a->key)) return false;
    return a < b;
}

inline uint32_t NextUploadSlot(uint32_t& cursor) {
    const uint32_t slot = cursor % static_cast<uint32_t>(kUploadRingSize);
    ++cursor;
    return slot;
}

inline size_t AlignUp(size_t value, size_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

inline size_t SsboOffsetAlignment() {
    static size_t alignment = []() {
        GLint value = 256;
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &value);
        if (value <= 0) value = 256;
        return static_cast<size_t>(value);
    }();
    return alignment;
}

inline bool UploadMappedRange(GLenum target, GLintptr offset, size_t size, const void* data) {
    if (size == 0) return true;

    void* dst = glMapBufferRange(target, offset, static_cast<GLsizeiptr>(size),
                                 GL_MAP_WRITE_BIT |
                                 GL_MAP_INVALIDATE_RANGE_BIT |
                                 GL_MAP_UNSYNCHRONIZED_BIT);
    if (!dst) return false;

    std::memcpy(dst, data, size);
    return glUnmapBuffer(target) == GL_TRUE;
}

inline void UploadRangeOrFallback(GLenum target, GLintptr offset, size_t size, const void* data) {
    if (size == 0) return;
    if (!UploadMappedRange(target, offset, size, data)) {
        glBufferSubData(target, offset, static_cast<GLsizeiptr>(size), data);
    }
}

struct FallbackTextures {
    GLuint white = 0;
    GLuint black = 0;
    GLuint normal = 0;
    GLuint blackCube = 0;
    bool initialized = false;
};

inline void InitFallbackTexture2D(GLuint& tex, const uint8_t rgba[4]) {
    if (tex != 0) return;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

inline void InitFallbackTextureCube(GLuint& tex, const uint8_t rgba[4]) {
    if (tex != 0) return;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

inline const FallbackTextures& GetFallbackTextures() {
    static FallbackTextures fb;
    if (!fb.initialized) {
        const uint8_t white[4] = {255, 255, 255, 255};
        const uint8_t black[4] = {0, 0, 0, 255};
        const uint8_t normal[4] = {128, 128, 255, 255};
        InitFallbackTexture2D(fb.white, white);
        InitFallbackTexture2D(fb.black, black);
        InitFallbackTexture2D(fb.normal, normal);
        InitFallbackTextureCube(fb.blackCube, black);
        fb.initialized = true;
    }
    return fb;
}

constexpr uint64_t kStaticCacheSignatureNone = 0ull;
constexpr uint64_t kStaticCacheSignatureSolid = 0x534F4C4944000001ull;
constexpr uint64_t kStaticCacheSignatureDecal = 0x444543414C000001ull;

inline uint64_t MakeGenericStaticCacheSignature(uint32_t shaderHash, int numTextures) {
    return (0x47454E4552494300ull |
            (static_cast<uint64_t>(numTextures & 0xFF) << 32) |
            static_cast<uint64_t>(shaderHash));
}

inline uint64_t HashStaticDrawGroups(const DrawCmd& obj) {
    uint64_t hash = 0x5d6f7a8b9c011223ull;
    if (!obj.mesh) {
        return hash;
    }
    HashCombine64(hash, static_cast<uint64_t>(obj.mesh->groups.size()));
    HashCombine64(hash, static_cast<uint64_t>(obj.mesh->idx.size()));

    if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
        const auto& g = obj.mesh->groups[obj.group];
        HashCombine64(hash, static_cast<uint64_t>(obj.group + 1));
        HashCombine64(hash, static_cast<uint64_t>(g.firstIndex()));
        HashCombine64(hash, static_cast<uint64_t>(g.indexCount()));
    } else {
        HashCombine64(hash, 0ull);
        for (const auto& g : obj.mesh->groups) {
            HashCombine64(hash, static_cast<uint64_t>(g.firstIndex()));
            HashCombine64(hash, static_cast<uint64_t>(g.indexCount()));
        }
    }
    return hash;
}

inline uint64_t ComputeSolidStaticCacheFingerprint(const std::vector<DrawCmd>& draws) {
    uint64_t hash = 0x534f4c4944465052ull;
    for (const DrawCmd& obj : draws) {
        if (!obj.isStatic) continue;
        HashCombine64(hash, 1ull);
        HashCombine64(hash, HashPointer64(obj.mesh));
        HashCombine64(hash, HashPointer64(obj.sourceNode));
        HashCombine64(hash, static_cast<uint64_t>(obj.megaIndexOffset));
        HashCombine64(hash, static_cast<uint64_t>(obj.receivesDecals ? 1 : 0));
        for (int i = 0; i < 4; ++i) {
            HashCombine64(hash, HashPointer64(obj.tex[i]));
        }
        HashCombine64(hash, HashMatrix4(obj.worldMatrix));
        HashCombine64(hash, HashStaticDrawGroups(obj));
    }
    return hash;
}

inline uint64_t ComputeGenericStaticCacheFingerprint(const std::vector<DrawCmd>& draws,
                                                     uint32_t shaderHash,
                                                     int textureCount) {
    uint64_t hash = 0x47454e4552494350ull;
    HashCombine64(hash, static_cast<uint64_t>(shaderHash));
    HashCombine64(hash, static_cast<uint64_t>(textureCount));
    for (const DrawCmd& obj : draws) {
        if (!obj.isStatic || !obj.mesh) continue;
        HashCombine64(hash, 1ull);
        HashCombine64(hash, HashPointer64(obj.mesh));
        HashCombine64(hash, HashPointer64(obj.sourceNode));
        HashCombine64(hash, static_cast<uint64_t>(obj.megaIndexOffset));
        HashCombine64(hash, static_cast<uint64_t>(obj.receivesDecals ? 1 : 0));
        for (int i = 0; i < textureCount; ++i) {
            HashCombine64(hash, HashPointer64(obj.tex[i]));
        }
        HashCombine64(hash, HashMatrix4(obj.worldMatrix));
        HashCombine64(hash, HashStaticDrawGroups(obj));
    }
    return hash;
}

inline uint64_t ComputeDecalStaticCacheFingerprint(const std::vector<DrawCmd>& draws) {
    uint64_t hash = 0x444543414c465052ull;
    for (const DrawCmd& obj : draws) {
        if (!obj.isStatic || !obj.mesh) continue;
        std::string shdrLower = obj.shdr;
        std::transform(shdrLower.begin(), shdrLower.end(), shdrLower.begin(), ::tolower);
        const bool isDecalReceiver =
            shdrLower.find("decalreceive") != std::string::npos ||
            shdrLower.find("decal_receive") != std::string::npos;
        const bool isRenderableDecal = !isDecalReceiver &&
                                       ((shdrLower == "shd:decal") ||
                                        (shdrLower.find("decal") != std::string::npos) ||
                                        obj.isDecal);
        if (!isRenderableDecal) continue;

        HashCombine64(hash, 1ull);
        HashCombine64(hash, HashPointer64(obj.mesh));
        HashCombine64(hash, HashPointer64(obj.sourceNode));
        HashCombine64(hash, static_cast<uint64_t>(obj.megaIndexOffset));
        HashCombine64(hash, HashPointer64(obj.tex[0]));
        HashCombine64(hash, HashPointer64(obj.tex[3]));
        HashCombine64(hash, HashMatrix4(obj.worldMatrix));
        HashCombine64(hash, HashFloatBits(obj.localBoxMin.x));
        HashCombine64(hash, HashFloatBits(obj.localBoxMin.y));
        HashCombine64(hash, HashFloatBits(obj.localBoxMin.z));
        HashCombine64(hash, HashFloatBits(obj.localBoxMax.x));
        HashCombine64(hash, HashFloatBits(obj.localBoxMax.y));
        HashCombine64(hash, HashFloatBits(obj.localBoxMax.z));
        HashCombine64(hash, HashFloatBits(obj.decalScale));
        HashCombine64(hash, static_cast<uint64_t>(obj.cullMode));
        HashCombine64(hash, HashStaticDrawGroups(obj));
    }
    return hash;
}

inline bool PassesFrustum(const DrawCmd& obj, const Camera::Frustum& frustum) {
    if (IsFrustumCullingDisabled()) {
        return true;
    }

    glm::vec3 worldCenter;
    float radius = 0.0f;
    EnsureCullBounds(obj, worldCenter, radius);

    for (int i = 0; i < 6; ++i) {
        const float dist = glm::dot(frustum.planes[i].normal, worldCenter) + frustum.planes[i].d;
        if (dist < -radius * 1.2f) {
            return false;
        }
    }
    return true;
}
}

DrawBatchSystem& DrawBatchSystem::instance() {
    static DrawBatchSystem inst;
    return inst;
}

bool DrawBatchSystem::hasGLResources() const {
    return modelMatrixSSBO.valid() ||
           transientModelMatrixSSBO.valid() ||
           perObjectSSBO.valid() ||
           decalParamsSSBO.valid() ||
           indirectBuffer.valid() ||
           transientIndirectBuffer.valid();
}

void DrawBatchSystem::shutdownGL() {
    reset(true);

    modelMatrixSSBOCapacity_ = 0;
    transientModelMatrixSSBOCapacity_ = 0;
    perObjectSSBOCapacity_ = 0;
    decalParamsSSBOCapacity_ = 0;
    indirectBufferCapacity_ = 0;
    transientIndirectBufferCapacity_ = 0;
}

void DrawBatchSystem::init(const std::vector<DrawCmd>& solidDraws) {
    (void)solidDraws;
    reset(true);
}

void DrawBatchSystem::reset(bool invalidateStaticCache) {
    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.perObjectData.clear();
        batch.decalParams.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();
    perObjectDataBuffer.clear();
    decalParamsBuffer.clear();
    dynamicGroupsCache_.clear();
    dynamicPackedCommands_.clear();
    packedRanges_.clear();
    dynamicSolidDrawIndices_.clear();
    dynamicSolidIndexSourceCount_ = 0;
    dynamicSolidStaticSourceCount_ = 0;

    if (invalidateStaticCache) {
        batches_.clear();
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticBatchDrawIndices_.clear();
        staticBatchInstanceDrawIndices_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        staticDecalParamsPacked_.clear();
        staticCacheSignature_ = kStaticCacheSignatureNone;
        staticCacheContentHash_ = 0ull;
        staticCacheSourceCount_ = 0;
        dynamicSolidIndexSourceCount_ = 0;
        dynamicSolidStaticSourceCount_ = 0;
        dynamicSolidDrawIndices_.clear();
        staticMatricesUploadedBySlot_.fill(false);
        staticIndirectUploadedBySlot_.fill(false);
        modelMatrixUploadCursor_ = 0;
        transientModelMatrixUploadCursor_ = 0;
        perObjectUploadCursor_ = 0;
        decalParamsUploadCursor_ = 0;
        indirectUploadCursor_ = 0;
        transientIndirectUploadCursor_ = 0;
        indirectBufferCapacity_ = 0;
        transientIndirectBufferCapacity_ = 0;
        transientModelMatrixSSBOCapacity_ = 0;
        initialMatrixUploadLogged = false;
    }
}

void DrawBatchSystem::invalidateStaticCache() {
    staticMatrixCount = 0;
    staticModelMatrices.clear();
    staticBatchCommands.clear();
    staticBatchDrawIndices_.clear();
    staticBatchInstanceDrawIndices_.clear();
    staticIndirectCommandsPacked_.clear();
    staticIndirectOffsets_.clear();
    staticCmdDrawIndices_.clear();
    staticCmdInstanceOffsets_.clear();
    staticCmdInstanceCounts_.clear();
    staticCmdInstanceDrawIndices_.clear();
    staticDecalParamsPacked_.clear();
    staticCacheSignature_ = kStaticCacheSignatureNone;
    staticCacheContentHash_ = 0ull;
    staticCacheSourceCount_ = 0;
    dynamicSolidIndexSourceCount_ = 0;
    dynamicSolidStaticSourceCount_ = 0;
    dynamicSolidDrawIndices_.clear();
    staticMatricesUploadedBySlot_.fill(false);
    staticIndirectUploadedBySlot_.fill(false);
    modelMatrixUploadCursor_ = 0;
    indirectUploadCursor_ = 0;
    initialMatrixUploadLogged = false;

    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.perObjectData.clear();
        batch.decalParams.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();
    perObjectDataBuffer.clear();
    decalParamsBuffer.clear();
}

void DrawBatchSystem::updateStaticVisibility(const std::vector<DrawCmd>& solidDraws) {
    if (staticCmdInstanceOffsets_.size() != staticIndirectCommandsPacked_.size() ||
        staticCmdInstanceCounts_.size() != staticIndirectCommandsPacked_.size()) {
        return;
    }

    bool changed = false;
    for (size_t i = 0; i < staticIndirectCommandsPacked_.size(); ++i) {
        const size_t offset = staticCmdInstanceOffsets_[i];
        const size_t count = static_cast<size_t>(staticCmdInstanceCounts_[i]);
        uint32_t visibleCount = 0;

        if (offset < staticCmdInstanceDrawIndices_.size()) {
            const size_t available = staticCmdInstanceDrawIndices_.size() - offset;
            const size_t clampedCount = std::min(count, available);
            for (size_t j = 0; j < clampedCount; ++j) {
                const size_t di = staticCmdInstanceDrawIndices_[offset + j];
                if (di < solidDraws.size() && !solidDraws[di].disabled) {
                    ++visibleCount;
                }
            }
        }

        const uint32_t want = visibleCount;
        if (staticIndirectCommandsPacked_[i].instanceCount != want) {
            staticIndirectCommandsPacked_[i].instanceCount = want;
            changed = true;
        }
    }
    if (changed) {
        // Force re-upload of the static indirect buffer on next flush
        staticIndirectUploadedBySlot_.fill(false);
    }
}

void DrawBatchSystem::cull(const std::vector<DrawCmd>& solidDraws,
                           const Camera::Frustum& frustum) {
    const bool allowStaticCache = UseStaticBatchCache();
    if (!allowStaticCache) {
        if (!staticModelMatrices.empty() || !staticIndirectCommandsPacked_.empty() || staticMatrixCount > 0) {
            invalidateStaticCache();
        }
        dynamicSolidDrawIndices_.clear();
        dynamicSolidDrawIndices_.reserve(solidDraws.size());
        for (size_t i = 0; i < solidDraws.size(); ++i) {
            dynamicSolidDrawIndices_.push_back(i);
        }
        dynamicSolidIndexSourceCount_ = solidDraws.size();
        dynamicSolidStaticSourceCount_ = 0;
    } else {
        bool rebuildDynamicSolidIndices = dynamicSolidIndexSourceCount_ != solidDraws.size();
        if (!rebuildDynamicSolidIndices) {
            if (!dynamicSolidDrawIndices_.empty()) {
                for (size_t drawIndex : dynamicSolidDrawIndices_) {
                    if (drawIndex >= solidDraws.size() || solidDraws[drawIndex].isStatic) {
                        rebuildDynamicSolidIndices = true;
                        break;
                    }
                }
                if (!rebuildDynamicSolidIndices &&
                    dynamicSolidDrawIndices_.size() + dynamicSolidStaticSourceCount_ != solidDraws.size()) {
                    rebuildDynamicSolidIndices = true;
                }
            } else if (dynamicSolidStaticSourceCount_ != solidDraws.size()) {
                rebuildDynamicSolidIndices = true;
            }
        }

        if (rebuildDynamicSolidIndices) {
            dynamicSolidDrawIndices_.clear();
            dynamicSolidDrawIndices_.reserve(solidDraws.size());
            dynamicSolidStaticSourceCount_ = 0;
            for (size_t i = 0; i < solidDraws.size(); ++i) {
                if (solidDraws[i].isStatic) {
                    ++dynamicSolidStaticSourceCount_;
                } else {
                    dynamicSolidDrawIndices_.push_back(i);
                }
            }
            dynamicSolidIndexSourceCount_ = solidDraws.size();
        }
    }

    const size_t staticSourceCount = allowStaticCache ? dynamicSolidStaticSourceCount_ : 0;
    const uint64_t staticContentHash = allowStaticCache ? ComputeSolidStaticCacheFingerprint(solidDraws) : 0ull;
    const bool buildingStaticBatches = allowStaticCache && (
        staticModelMatrices.empty() ||
        staticCacheSignature_ != kStaticCacheSignatureSolid ||
        staticCacheContentHash_ != staticContentHash ||
        staticCacheSourceCount_ != staticSourceCount ||
        staticCmdDrawIndices_.size() != staticIndirectCommandsPacked_.size() ||
        staticCmdInstanceOffsets_.size() != staticIndirectCommandsPacked_.size() ||
        staticCmdInstanceCounts_.size() != staticIndirectCommandsPacked_.size());

    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();

    if (!allowStaticCache || !buildingStaticBatches) {
        staticMatrixCount = allowStaticCache ? staticModelMatrices.size() : 0;
    } else {
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticBatchDrawIndices_.clear();
        staticBatchInstanceDrawIndices_.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        staticDecalParamsPacked_.clear();
        staticCacheSignature_ = kStaticCacheSignatureSolid;
        staticCacheContentHash_ = staticContentHash;
        staticCacheSourceCount_ = staticSourceCount;
    }

    auto ensureBatch = [&](const BatchKey& key) -> DrawBatch& {
        auto it = batches_.find(key);
        if (it == batches_.end()) {
            it = batches_.emplace(key, DrawBatch{key}).first;
        }
        it->second.key = key;
        return it->second;
    };

    if (buildingStaticBatches) {
        size_t drawIdx = 0;
        for (const auto& obj : solidDraws) {
            ++drawIdx;
            if (!obj.mesh) continue;
            if (!obj.isStatic) continue;

            BatchKey key;
            key.tex[0] = obj.tex[0];
            key.tex[1] = obj.tex[1];
            key.tex[2] = obj.tex[2];
            key.tex[3] = obj.tex[3];
            key.receivesDecals = obj.receivesDecals;
            key.shaderHash = 0;

            ensureBatch(key);
            auto& staticCommands = staticBatchCommands[key];
            auto& staticDrawIdxs = staticBatchDrawIndices_[key];
            auto& staticInstanceDrawIdxs = staticBatchInstanceDrawIndices_[key];
            const uint32_t baseInstance = static_cast<uint32_t>(staticModelMatrices.size());
            bool matrixAllocated = false;
            auto ensureMatrix = [&]() {
                if (!matrixAllocated) {
                    staticModelMatrices.push_back(obj.worldMatrix);
                    matrixAllocated = true;
                }
            };

            const size_t srcIdx = drawIdx - 1;
            auto appendGroup = [&](const Nvx2Group& g) {
                if (!IsValidGroupForDraw(obj, g)) return;
                ensureMatrix();

                DrawCommand cmd{};
                cmd.count = g.indexCount();
                cmd.instanceCount = 0;
                cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                cmd.baseVertex = 0;
                cmd.baseInstance = baseInstance;

                staticCommands.push_back(cmd);
                staticDrawIdxs.push_back(srcIdx);
                staticInstanceDrawIdxs.emplace_back(std::vector<size_t>{srcIdx});
            };

            if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
                appendGroup(obj.mesh->groups[obj.group]);
            } else {
                for (const auto& g : obj.mesh->groups) {
                    appendGroup(g);
                }
            }
        }

        for (const auto& [k, staticCmds] : staticBatchCommands) {
            if (staticCmds.empty()) continue;
            auto bit = batches_.find(k);
            if (bit != batches_.end()) {
                active_.push_back(&bit->second);
            }
        }

        staticMatrixCount = staticModelMatrices.size();
    }

    for (auto& [k, outerMap] : dynamicGroupsCache_) {
        for (auto& [gk, grp] : outerMap) {
            grp.matrices.clear();
        }
    }

    for (size_t drawIndex : dynamicSolidDrawIndices_) {
        if (drawIndex >= solidDraws.size()) {
            continue;
        }
        const DrawCmd& obj = solidDraws[drawIndex];
        if (!obj.mesh) continue;
        if (obj.disabled) continue;
        if (!PassesFrustum(obj, frustum)) continue;

        BatchKey key;
        key.tex[0] = obj.tex[0];
        key.tex[1] = obj.tex[1];
        key.tex[2] = obj.tex[2];
        key.tex[3] = obj.tex[3];
        key.receivesDecals = obj.receivesDecals;
        key.shaderHash = 0;

        auto appendGroup = [&](const Nvx2Group& g) {
            if (!IsValidGroupForDraw(obj, g)) return;
            const uint32_t count = g.indexCount();
            const uint32_t firstIndex = obj.megaIndexOffset + g.firstIndex();
            const uint64_t geomKey = (static_cast<uint64_t>(count) << 32) | firstIndex;
            auto& entry = dynamicGroupsCache_[key][geomKey];
            if (entry.count == 0) {
                entry.count = count;
                entry.firstIndex = firstIndex;
            }
            entry.matrices.push_back(obj.worldMatrix);
        };

        if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
            appendGroup(obj.mesh->groups[obj.group]);
        } else {
            for (const auto& g : obj.mesh->groups) {
                appendGroup(g);
            }
        }
    }

    for (auto& [key, groups] : dynamicGroupsCache_) {
        DrawBatch& batch = ensureBatch(key);
        for (auto& [geomKey, entry] : groups) {
            (void)geomKey;
            if (entry.matrices.empty()) continue;

            DrawCommand cmd{};
            cmd.count = entry.count;
            cmd.instanceCount = static_cast<uint32_t>(entry.matrices.size());
            cmd.firstIndex = entry.firstIndex;
            cmd.baseVertex = 0;
            cmd.baseInstance = static_cast<uint32_t>(staticMatrixCount + modelMatrices.size());

            modelMatrices.insert(modelMatrices.end(), entry.matrices.begin(), entry.matrices.end());
            batch.commands.push_back(cmd);
        }
        if (!batch.commands.empty()) {
            active_.push_back(&batch);
        }
    }

    if (allowStaticCache && !buildingStaticBatches) {
        for (auto& [k, batch] : batches_) {
            const bool hasDynamic = !batch.commands.empty();
            auto sit = staticBatchCommands.find(k);
            const bool hasStatic = sit != staticBatchCommands.end() && !sit->second.empty();
            if (hasStatic && !hasDynamic) {
                active_.push_back(&batch);
            }
        }
    }

    if (!active_.empty()) {
        std::sort(active_.begin(), active_.end(), BatchPtrLess);
        active_.erase(std::unique(active_.begin(), active_.end()), active_.end());
    }

    if (buildingStaticBatches) {
        staticMatricesUploadedBySlot_.fill(false);
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        for (const auto& [k, staticCmds] : staticBatchCommands) {
            if (!staticCmds.empty()) {
                staticIndirectOffsets_[k] = staticIndirectCommandsPacked_.size();
                staticIndirectCommandsPacked_.insert(staticIndirectCommandsPacked_.end(),
                                                     staticCmds.begin(),
                                                     staticCmds.end());
                auto idxIt = staticBatchDrawIndices_.find(k);
                auto instIdxIt = staticBatchInstanceDrawIndices_.find(k);
                for (size_t cmdIdx = 0; cmdIdx < staticCmds.size(); ++cmdIdx) {
                    const size_t representativeIdx =
                        (idxIt != staticBatchDrawIndices_.end() && cmdIdx < idxIt->second.size())
                            ? idxIt->second[cmdIdx]
                            : 0u;
                    staticCmdDrawIndices_.push_back(representativeIdx);
                    staticCmdInstanceOffsets_.push_back(staticCmdInstanceDrawIndices_.size());

                    if (instIdxIt != staticBatchInstanceDrawIndices_.end() &&
                        cmdIdx < instIdxIt->second.size() &&
                        !instIdxIt->second[cmdIdx].empty()) {
                        const auto& refs = instIdxIt->second[cmdIdx];
                        staticCmdInstanceDrawIndices_.insert(staticCmdInstanceDrawIndices_.end(),
                                                             refs.begin(),
                                                             refs.end());
                        staticCmdInstanceCounts_.push_back(static_cast<uint32_t>(refs.size()));
                    } else {
                        staticCmdInstanceDrawIndices_.push_back(representativeIdx);
                        staticCmdInstanceCounts_.push_back(1u);
                    }
                }
                auto bit = batches_.find(k);
                if (bit != batches_.end()) {
                    bit->second.staticCommandCount = staticCmds.size();
                }
            }
        }
        staticIndirectUploadedBySlot_.fill(false);
    }

    if (allowStaticCache) {
        updateStaticVisibility(solidDraws);
    }

}

void DrawBatchSystem::cullGeneric(const std::vector<DrawCmd>& draws, const Camera::Frustum& frustum,
                                    uint32_t shaderHash, int numTextures) {
    const bool allowStaticCache = UseStaticBatchCache();
    const int textureCount = std::clamp(numTextures, 0, 12);
    const uint64_t cacheSignature = MakeGenericStaticCacheSignature(shaderHash, textureCount);
    const uint64_t staticContentHash =
        ComputeGenericStaticCacheFingerprint(draws, shaderHash, textureCount);
    size_t staticSourceCount = 0;
    if (allowStaticCache) {
        for (const DrawCmd& obj : draws) {
            if (obj.mesh != nullptr && obj.isStatic) {
                ++staticSourceCount;
            }
        }
    }
    const bool hasStaticCandidates = allowStaticCache && staticSourceCount > 0;
    const bool buildingStaticBatches =
        hasStaticCandidates && (
            staticModelMatrices.empty() ||
            staticCacheSignature_ != cacheSignature ||
            staticCacheContentHash_ != staticContentHash ||
            staticCacheSourceCount_ != staticSourceCount ||
            staticCmdDrawIndices_.size() != staticIndirectCommandsPacked_.size() ||
            staticCmdInstanceOffsets_.size() != staticIndirectCommandsPacked_.size() ||
            staticCmdInstanceCounts_.size() != staticIndirectCommandsPacked_.size());

    if (!allowStaticCache &&
        (!staticModelMatrices.empty() || !staticIndirectCommandsPacked_.empty() || staticMatrixCount > 0)) {
        invalidateStaticCache();
    }

    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.perObjectData.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();
    perObjectDataBuffer.clear();

    if (!hasStaticCandidates) {
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticBatchDrawIndices_.clear();
        staticBatchInstanceDrawIndices_.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        staticDecalParamsPacked_.clear();
        staticCacheSignature_ = kStaticCacheSignatureNone;
        staticCacheContentHash_ = 0ull;
        staticCacheSourceCount_ = 0;
    } else if (buildingStaticBatches) {
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticBatchDrawIndices_.clear();
        staticBatchInstanceDrawIndices_.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        staticDecalParamsPacked_.clear();
        staticCacheSignature_ = cacheSignature;
        staticCacheContentHash_ = staticContentHash;
        staticCacheSourceCount_ = staticSourceCount;

        size_t drawIdx = 0;
        for (const auto& obj : draws) {
            ++drawIdx;
            if (!obj.mesh || !obj.isStatic) continue;

            BatchKey key;
            for (int i = 0; i < textureCount; ++i) {
                key.tex[i] = obj.tex[i];
            }
            key.receivesDecals = obj.receivesDecals;
            key.shaderHash = shaderHash;

            auto it = batches_.find(key);
            if (it == batches_.end()) {
                it = batches_.emplace(key, DrawBatch{key}).first;
            }
            DrawBatch& batch = it->second;
            batch.key = key;
            auto& staticCommands = staticBatchCommands[key];
            auto& staticDrawIdxs = staticBatchDrawIndices_[key];
            auto& staticInstanceDrawIdxs = staticBatchInstanceDrawIndices_[key];
            const uint32_t baseInstance = static_cast<uint32_t>(staticModelMatrices.size());
            bool matrixAllocated = false;
            auto ensureMatrix = [&]() {
                if (!matrixAllocated) {
                    staticModelMatrices.push_back(obj.worldMatrix);
                    matrixAllocated = true;
                }
            };

            const size_t srcIdx = drawIdx - 1;
            if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
                auto& g = obj.mesh->groups[obj.group];
                if (!IsValidGroupForDraw(obj, g)) continue;
                ensureMatrix();
                DrawCommand cmd{};
                cmd.count = g.indexCount();
                cmd.instanceCount = 0;
                cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                cmd.baseVertex = 0;
                cmd.baseInstance = baseInstance;
                staticCommands.push_back(cmd);
                staticDrawIdxs.push_back(srcIdx);
                staticInstanceDrawIdxs.emplace_back(std::vector<size_t>{srcIdx});
            } else {
                for (size_t gi = 0; gi < obj.mesh->groups.size(); ++gi) {
                    auto& g = obj.mesh->groups[gi];
                    if (!IsValidGroupForDraw(obj, g)) continue;
                    ensureMatrix();
                    DrawCommand cmd{};
                    cmd.count = g.indexCount();
                    cmd.instanceCount = 0;
                    cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                    cmd.baseVertex = 0;
                    cmd.baseInstance = baseInstance;
                    staticCommands.push_back(cmd);
                    staticDrawIdxs.push_back(srcIdx);
                    staticInstanceDrawIdxs.emplace_back(std::vector<size_t>{srcIdx});
                }
            }

            active_.push_back(&batch);
        }

        staticMatrixCount = staticModelMatrices.size();
        staticMatricesUploadedBySlot_.fill(false);
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        for (const auto& [k, staticCmds] : staticBatchCommands) {
            if (!staticCmds.empty()) {
                staticIndirectOffsets_[k] = staticIndirectCommandsPacked_.size();
                staticIndirectCommandsPacked_.insert(staticIndirectCommandsPacked_.end(),
                                                     staticCmds.begin(),
                                                     staticCmds.end());
                auto idxIt = staticBatchDrawIndices_.find(k);
                auto instIdxIt = staticBatchInstanceDrawIndices_.find(k);
                for (size_t cmdIdx = 0; cmdIdx < staticCmds.size(); ++cmdIdx) {
                    const size_t representativeIdx =
                        (idxIt != staticBatchDrawIndices_.end() && cmdIdx < idxIt->second.size())
                            ? idxIt->second[cmdIdx]
                            : 0u;
                    staticCmdDrawIndices_.push_back(representativeIdx);
                    staticCmdInstanceOffsets_.push_back(staticCmdInstanceDrawIndices_.size());

                    if (instIdxIt != staticBatchInstanceDrawIndices_.end() &&
                        cmdIdx < instIdxIt->second.size() &&
                        !instIdxIt->second[cmdIdx].empty()) {
                        const auto& refs = instIdxIt->second[cmdIdx];
                        staticCmdInstanceDrawIndices_.insert(staticCmdInstanceDrawIndices_.end(),
                                                             refs.begin(),
                                                             refs.end());
                        staticCmdInstanceCounts_.push_back(static_cast<uint32_t>(refs.size()));
                    } else {
                        staticCmdInstanceDrawIndices_.push_back(representativeIdx);
                        staticCmdInstanceCounts_.push_back(1u);
                    }
                }
                auto bit = batches_.find(k);
                if (bit != batches_.end()) {
                    bit->second.staticCommandCount = staticCmds.size();
                }
            }
        }
        staticIndirectUploadedBySlot_.fill(false);
    } else {
        staticMatrixCount = staticModelMatrices.size();
    }

    if (hasStaticCandidates &&
        staticCmdInstanceOffsets_.size() == staticIndirectCommandsPacked_.size() &&
        staticCmdInstanceCounts_.size() == staticIndirectCommandsPacked_.size()) {
        bool changed = false;
        for (size_t i = 0; i < staticIndirectCommandsPacked_.size(); ++i) {
            const size_t offset = staticCmdInstanceOffsets_[i];
            const size_t count = static_cast<size_t>(staticCmdInstanceCounts_[i]);
            uint32_t visibleCount = 0;

            if (offset < staticCmdInstanceDrawIndices_.size()) {
                const size_t available = staticCmdInstanceDrawIndices_.size() - offset;
                const size_t clampedCount = std::min(count, available);
                for (size_t j = 0; j < clampedCount; ++j) {
                    const size_t di = staticCmdInstanceDrawIndices_[offset + j];
                    if (di < draws.size() && !draws[di].disabled) {
                        ++visibleCount;
                    }
                }
            }

            const uint32_t want = visibleCount;
            if (staticIndirectCommandsPacked_[i].instanceCount != want) {
                staticIndirectCommandsPacked_[i].instanceCount = want;
                changed = true;
            }
        }
        if (changed) {
            staticIndirectUploadedBySlot_.fill(false);
        }
    }

    auto& groupedDynamics = dynamicGroupsCache_;
    for (auto& [groupKey, groups] : groupedDynamics) {
        (void)groupKey;
        for (auto& [geomKey, entry] : groups) {
            (void)geomKey;
            entry.count = 0;
            entry.firstIndex = 0;
            entry.alphaCutoff = 0.5f;
            entry.matrices.clear();
        }
    }

    for (const auto& obj : draws) {
        if (!obj.mesh) continue;
        if (obj.disabled) continue;
        if (hasStaticCandidates && obj.isStatic) continue;
        if (!PassesFrustum(obj, frustum)) continue;

        BatchKey key;
        for (int i = 0; i < textureCount; ++i) {
            key.tex[i] = obj.tex[i];
        }
        key.receivesDecals = obj.receivesDecals;
        key.shaderHash = shaderHash;

        auto appendGroup = [&](const Nvx2Group& g) {
            if (!IsValidGroupForDraw(obj, g)) return;
            const uint32_t count = g.indexCount();
            const uint32_t firstIndex = obj.megaIndexOffset + g.firstIndex();
            const uint64_t geomKey = (static_cast<uint64_t>(count) << 32) | firstIndex;
            auto& entry = groupedDynamics[key][geomKey];
            if (entry.matrices.empty()) {
                entry.count = count;
                entry.firstIndex = firstIndex;
                entry.alphaCutoff = obj.alphaCutoff;
            }
            entry.matrices.push_back(obj.worldMatrix);
        };

        if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
            appendGroup(obj.mesh->groups[obj.group]);
        } else {
            for (const auto& g : obj.mesh->groups) {
                appendGroup(g);
            }
        }
    }

    for (auto& [key, groups] : groupedDynamics) {
        auto it = batches_.find(key);
        if (it == batches_.end()) {
            it = batches_.emplace(key, DrawBatch{key}).first;
        }
        DrawBatch& batch = it->second;
        batch.key = key;

        for (auto& [geomKey, entry] : groups) {
            (void)geomKey;
            if (entry.matrices.empty()) continue;
            DrawCommand cmd{};
            cmd.count = entry.count;
            cmd.instanceCount = static_cast<uint32_t>(entry.matrices.size());
            cmd.firstIndex = entry.firstIndex;
            cmd.baseVertex = 0;
            cmd.baseInstance = static_cast<uint32_t>(staticMatrixCount + modelMatrices.size());

            modelMatrices.insert(modelMatrices.end(), entry.matrices.begin(), entry.matrices.end());
            batch.perObjectData.push_back(entry.alphaCutoff);
            batch.commands.push_back(cmd);
        }

        if (!batch.commands.empty()) {
            active_.push_back(&batch);
        }
    }

    if (hasStaticCandidates && !buildingStaticBatches) {
        for (auto& [k, batch] : batches_) {
            const bool hasDynamic = !batch.commands.empty();
            auto sit = staticBatchCommands.find(k);
            const bool hasStatic = sit != staticBatchCommands.end() && !sit->second.empty();
            if (hasStatic && !hasDynamic) {
                active_.push_back(&batch);
            }
        }
    }

    if (!active_.empty()) {
        std::sort(active_.begin(), active_.end(), BatchPtrLess);
        active_.erase(std::unique(active_.begin(), active_.end()), active_.end());
    }
}

void DrawBatchSystem::cullDecals(const std::vector<DrawCmd>& draws) {
    const bool allowStaticCache = UseStaticBatchCache();
    auto isRenderableDecal = [](const DrawCmd& obj) {
        if (!obj.mesh) return false;
        std::string shdrLower = obj.shdr;
        std::transform(shdrLower.begin(), shdrLower.end(), shdrLower.begin(), ::tolower);
        const bool isDecalReceiver =
            shdrLower.find("decalreceive") != std::string::npos ||
            shdrLower.find("decal_receive") != std::string::npos;
        return !isDecalReceiver &&
               ((shdrLower == "shd:decal") ||
                (shdrLower.find("decal") != std::string::npos) ||
                obj.isDecal);
    };

    size_t staticSourceCount = 0;
    if (allowStaticCache) {
        for (const DrawCmd& obj : draws) {
            if (obj.isStatic && isRenderableDecal(obj)) {
                ++staticSourceCount;
            }
        }
    }
    const uint64_t staticContentHash = ComputeDecalStaticCacheFingerprint(draws);
    const bool hasStaticCandidates = allowStaticCache && staticSourceCount > 0;
    const bool buildingStaticBatches =
        hasStaticCandidates && (
            staticModelMatrices.empty() ||
            staticCacheSignature_ != kStaticCacheSignatureDecal ||
            staticCacheContentHash_ != staticContentHash ||
            staticCacheSourceCount_ != staticSourceCount ||
            staticCmdDrawIndices_.size() != staticIndirectCommandsPacked_.size() ||
            staticCmdInstanceOffsets_.size() != staticIndirectCommandsPacked_.size() ||
            staticCmdInstanceCounts_.size() != staticIndirectCommandsPacked_.size() ||
            staticDecalParamsPacked_.size() != staticModelMatrices.size() * 2u);

    if (!allowStaticCache &&
        (!staticModelMatrices.empty() || !staticIndirectCommandsPacked_.empty() || staticMatrixCount > 0)) {
        invalidateStaticCache();
    }

    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.decalParams.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();
    decalParamsBuffer.clear();

    if (!hasStaticCandidates) {
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticBatchDrawIndices_.clear();
        staticBatchInstanceDrawIndices_.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        staticDecalParamsPacked_.clear();
        staticCacheSignature_ = kStaticCacheSignatureNone;
        staticCacheContentHash_ = 0ull;
        staticCacheSourceCount_ = 0;
    } else if (buildingStaticBatches) {
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticBatchDrawIndices_.clear();
        staticBatchInstanceDrawIndices_.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        staticDecalParamsPacked_.clear();
        staticCacheSignature_ = kStaticCacheSignatureDecal;
        staticCacheContentHash_ = staticContentHash;
        staticCacheSourceCount_ = staticSourceCount;

        size_t drawIdx = 0;
        for (const auto& obj : draws) {
            ++drawIdx;
            if (!obj.isStatic || !isRenderableDecal(obj)) continue;

            BatchKey key;
            key.tex[0] = obj.tex[0];
            key.tex[3] = obj.tex[3];
            key.receivesDecals = false;
            key.shaderHash = 0xDECAL;

            auto it = batches_.find(key);
            if (it == batches_.end()) {
                it = batches_.emplace(key, DrawBatch{key}).first;
            }
            DrawBatch& batch = it->second;
            batch.key = key;
            auto& staticCommands = staticBatchCommands[key];
            auto& staticDrawIdxs = staticBatchDrawIndices_[key];
            auto& staticInstanceDrawIdxs = staticBatchInstanceDrawIndices_[key];
            const uint32_t baseInstance = static_cast<uint32_t>(staticModelMatrices.size());
            bool matrixAllocated = false;
            auto ensureMatrix = [&]() {
                if (!matrixAllocated) {
                    staticModelMatrices.push_back(obj.worldMatrix);
                    glm::vec4 params;
                    params.x = obj.localBoxMin.x;
                    params.y = obj.localBoxMin.y;
                    params.z = obj.localBoxMin.z;
                    params.w = obj.localBoxMax.x;
                    staticDecalParamsPacked_.push_back(params);

                    glm::vec4 params2;
                    params2.x = obj.localBoxMax.y;
                    params2.y = obj.localBoxMax.z;
                    params2.z = obj.decalScale;
                    params2.w = static_cast<float>(obj.cullMode);
                    staticDecalParamsPacked_.push_back(params2);
                    matrixAllocated = true;
                }
            };

            const size_t srcIdx = drawIdx - 1;
            if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
                auto& g = obj.mesh->groups[obj.group];
                if (!IsValidGroupForDraw(obj, g)) continue;
                ensureMatrix();
                DrawCommand cmd{};
                cmd.count = g.indexCount();
                cmd.instanceCount = 0;
                cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                cmd.baseVertex = 0;
                cmd.baseInstance = baseInstance;
                staticCommands.push_back(cmd);
                staticDrawIdxs.push_back(srcIdx);
                staticInstanceDrawIdxs.emplace_back(std::vector<size_t>{srcIdx});
            } else {
                for (size_t gi = 0; gi < obj.mesh->groups.size(); ++gi) {
                    auto& g = obj.mesh->groups[gi];
                    if (!IsValidGroupForDraw(obj, g)) continue;
                    ensureMatrix();
                    DrawCommand cmd{};
                    cmd.count = g.indexCount();
                    cmd.instanceCount = 0;
                    cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                    cmd.baseVertex = 0;
                    cmd.baseInstance = baseInstance;
                    staticCommands.push_back(cmd);
                    staticDrawIdxs.push_back(srcIdx);
                    staticInstanceDrawIdxs.emplace_back(std::vector<size_t>{srcIdx});
                }
            }

            active_.push_back(&batch);
        }

        staticMatrixCount = staticModelMatrices.size();
        staticMatricesUploadedBySlot_.fill(false);
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
        staticCmdInstanceOffsets_.clear();
        staticCmdInstanceCounts_.clear();
        staticCmdInstanceDrawIndices_.clear();
        for (const auto& [k, staticCmds] : staticBatchCommands) {
            if (!staticCmds.empty()) {
                staticIndirectOffsets_[k] = staticIndirectCommandsPacked_.size();
                staticIndirectCommandsPacked_.insert(staticIndirectCommandsPacked_.end(),
                                                     staticCmds.begin(),
                                                     staticCmds.end());
                auto idxIt = staticBatchDrawIndices_.find(k);
                auto instIdxIt = staticBatchInstanceDrawIndices_.find(k);
                for (size_t cmdIdx = 0; cmdIdx < staticCmds.size(); ++cmdIdx) {
                    const size_t representativeIdx =
                        (idxIt != staticBatchDrawIndices_.end() && cmdIdx < idxIt->second.size())
                            ? idxIt->second[cmdIdx]
                            : 0u;
                    staticCmdDrawIndices_.push_back(representativeIdx);
                    staticCmdInstanceOffsets_.push_back(staticCmdInstanceDrawIndices_.size());

                    if (instIdxIt != staticBatchInstanceDrawIndices_.end() &&
                        cmdIdx < instIdxIt->second.size() &&
                        !instIdxIt->second[cmdIdx].empty()) {
                        const auto& refs = instIdxIt->second[cmdIdx];
                        staticCmdInstanceDrawIndices_.insert(staticCmdInstanceDrawIndices_.end(),
                                                             refs.begin(),
                                                             refs.end());
                        staticCmdInstanceCounts_.push_back(static_cast<uint32_t>(refs.size()));
                    } else {
                        staticCmdInstanceDrawIndices_.push_back(representativeIdx);
                        staticCmdInstanceCounts_.push_back(1u);
                    }
                }
                auto bit = batches_.find(k);
                if (bit != batches_.end()) {
                    bit->second.staticCommandCount = staticCmds.size();
                }
            }
        }
        staticIndirectUploadedBySlot_.fill(false);
    } else {
        staticMatrixCount = staticModelMatrices.size();
    }

    if (hasStaticCandidates &&
        staticCmdInstanceOffsets_.size() == staticIndirectCommandsPacked_.size() &&
        staticCmdInstanceCounts_.size() == staticIndirectCommandsPacked_.size()) {
        bool changed = false;
        for (size_t i = 0; i < staticIndirectCommandsPacked_.size(); ++i) {
            const size_t offset = staticCmdInstanceOffsets_[i];
            const size_t count = static_cast<size_t>(staticCmdInstanceCounts_[i]);
            uint32_t visibleCount = 0;

            if (offset < staticCmdInstanceDrawIndices_.size()) {
                const size_t available = staticCmdInstanceDrawIndices_.size() - offset;
                const size_t clampedCount = std::min(count, available);
                for (size_t j = 0; j < clampedCount; ++j) {
                    const size_t di = staticCmdInstanceDrawIndices_[offset + j];
                    if (di < draws.size()) {
                        const DrawCmd& obj = draws[di];
                        if (!obj.disabled && isRenderableDecal(obj)) {
                            ++visibleCount;
                        }
                    }
                }
            }

            const uint32_t want = visibleCount;
            if (staticIndirectCommandsPacked_[i].instanceCount != want) {
                staticIndirectCommandsPacked_[i].instanceCount = want;
                changed = true;
            }
        }
        if (changed) {
            staticIndirectUploadedBySlot_.fill(false);
        }
    }

    for (const auto& obj : draws) {
        if (!isRenderableDecal(obj)) continue;
        if (obj.disabled) continue;
        if (hasStaticCandidates && obj.isStatic) continue;

        BatchKey key;
        key.tex[0] = obj.tex[0];
        key.tex[3] = obj.tex[3];
        key.receivesDecals = false;
        key.shaderHash = 0xDECAL;

        auto it = batches_.find(key);
        if (it == batches_.end())
            it = batches_.emplace(key, DrawBatch{key}).first;
        DrawBatch& batch = it->second;
        batch.key = key;
        const uint32_t baseInstance = static_cast<uint32_t>(staticMatrixCount + modelMatrices.size());
        bool matrixAllocated = false;
        auto ensureMatrix = [&]() {
            if (!matrixAllocated) {
                modelMatrices.push_back(obj.worldMatrix);
                matrixAllocated = true;
            }
        };

        if (obj.group >= 0 && obj.group < static_cast<int>(obj.mesh->groups.size())) {
            auto& g = obj.mesh->groups[obj.group];
            if (!IsValidGroupForDraw(obj, g)) continue;
            ensureMatrix();
            DrawCommand cmd{};
            cmd.count = g.indexCount();
            cmd.instanceCount = 1;
            cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
            cmd.baseVertex = 0;
            cmd.baseInstance = baseInstance;

            glm::vec4 params;
            params.x = obj.localBoxMin.x;
            params.y = obj.localBoxMin.y;
            params.z = obj.localBoxMin.z;
            params.w = obj.localBoxMax.x;
            batch.decalParams.push_back(params);

            glm::vec4 params2;
            params2.x = obj.localBoxMax.y;
            params2.y = obj.localBoxMax.z;
            params2.z = obj.decalScale;
            params2.w = static_cast<float>(obj.cullMode);
            batch.decalParams.push_back(params2);

            batch.commands.push_back(cmd);
        } else {
            for (size_t gi = 0; gi < obj.mesh->groups.size(); ++gi) {
                auto& g = obj.mesh->groups[gi];
                if (!IsValidGroupForDraw(obj, g)) continue;
                ensureMatrix();
                DrawCommand cmd{};
                cmd.count = g.indexCount();
                cmd.instanceCount = 1;
                cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                cmd.baseVertex = 0;
                cmd.baseInstance = baseInstance;

                glm::vec4 params;
                params.x = obj.localBoxMin.x;
                params.y = obj.localBoxMin.y;
                params.z = obj.localBoxMin.z;
                params.w = obj.localBoxMax.x;
                batch.decalParams.push_back(params);

                glm::vec4 params2;
                params2.x = obj.localBoxMax.y;
                params2.y = obj.localBoxMax.z;
                params2.z = obj.decalScale;
                params2.w = static_cast<float>(obj.cullMode);
                batch.decalParams.push_back(params2);

                batch.commands.push_back(cmd);
            }
        }
        active_.push_back(&batch);
    }

    if (hasStaticCandidates && !buildingStaticBatches) {
        for (auto& [k, batch] : batches_) {
            const bool hasDynamic = !batch.commands.empty();
            auto sit = staticBatchCommands.find(k);
            const bool hasStatic = sit != staticBatchCommands.end() && !sit->second.empty();
            if (hasStatic && !hasDynamic) {
                active_.push_back(&batch);
            }
        }
    }

    if (!active_.empty()) {
        std::sort(active_.begin(), active_.end(), BatchPtrLess);
        active_.erase(std::unique(active_.begin(), active_.end()), active_.end());
    }
}

void DrawBatchSystem::flush(NDEVC::Graphics::ISampler* samplerRepeat, int numTexturesToBind, GLuint currentProgram) {
    const bool logThis = false;
    const int textureBindCount = std::min(numTexturesToBind, 12);
    GLuint samplerRepeatHandle = 0;
    if (samplerRepeat) {
        samplerRepeatHandle = *(GLuint*)samplerRepeat->GetNativeHandle();
    }

    if (logThis) {
        std::cout << "    [DrawBatchSystem] flush() called with " << active_.size() << " active batches, "
                  << modelMatrices.size() << " matrices\n";
    }

    GLint receivesDecalsLoc = -1;
    if (currentProgram != 0) {
        // Resolve per flush from the currently linked program to avoid stale
        // location reuse across program/context recreation or GL id reuse.
        receivesDecalsLoc = glGetUniformLocation(currentProgram, "ReceivesDecals");
    }

    MegaBuffer::instance().bind();

    const bool useStaticMatrixCache = staticMatrixCount > 0;
    auto& matrixSSBO = useStaticMatrixCache ? modelMatrixSSBO : transientModelMatrixSSBO;
    size_t& matrixCapacity = useStaticMatrixCache ? modelMatrixSSBOCapacity_ : transientModelMatrixSSBOCapacity_;
    uint32_t& matrixCursor = useStaticMatrixCache ? modelMatrixUploadCursor_ : transientModelMatrixUploadCursor_;

    if (!matrixSSBO) glGenBuffers(1, matrixSSBO.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, matrixSSBO);

    const size_t dynamicMatrixCount = modelMatrices.size();
    const size_t totalMatrixCount = staticMatrixCount + dynamicMatrixCount;
    size_t needed = totalMatrixCount * sizeof(glm::mat4);
    size_t uploadedMatrixBytes = 0;
    if (needed > matrixCapacity) {
        const size_t newCapRaw = needed ? (needed * 2) : sizeof(glm::mat4);
        const size_t newCap = AlignUp(newCapRaw, SsboOffsetAlignment());
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                     nullptr,
                     GL_STREAM_DRAW);
        if (glGetError() == GL_NO_ERROR) {
            matrixCapacity = newCap;
            if (useStaticMatrixCache) {
                staticMatricesUploadedBySlot_.fill(false);
            }
        }
    }
    if (needed > 0 && matrixCapacity > 0) {
        const uint32_t slot = NextUploadSlot(matrixCursor);
        const size_t slotBaseOffset = static_cast<size_t>(slot) * matrixCapacity;
        const size_t staticBytes = useStaticMatrixCache ? (staticMatrixCount * sizeof(glm::mat4)) : 0;

        if (staticBytes > 0 && !staticMatricesUploadedBySlot_[slot]) {
            UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                                  static_cast<GLintptr>(slotBaseOffset),
                                  staticBytes,
                                  staticModelMatrices.data());
            staticMatricesUploadedBySlot_[slot] = true;
            uploadedMatrixBytes += staticBytes;
        }

        if (dynamicMatrixCount > 0) {
            const size_t dynamicBytes = dynamicMatrixCount * sizeof(glm::mat4);
            const size_t dynamicOffset = slotBaseOffset + staticBytes;
            UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                                  static_cast<GLintptr>(dynamicOffset),
                                  dynamicBytes,
                                  modelMatrices.data());
            uploadedMatrixBytes += dynamicBytes;
        }

        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, matrixSSBO,
                          static_cast<GLintptr>(slotBaseOffset),
                          static_cast<GLsizeiptr>(needed));
    }

    if (!initialMatrixUploadLogged && uploadedMatrixBytes > 0) {
        const double uploadKB = static_cast<double>(uploadedMatrixBytes) / 1024.0;
        std::cout << "[DrawBatchSystem] Initial matrix upload: " << uploadKB << " KB\n";
        initialMatrixUploadLogged = true;
    }

    if (!perObjectSSBO) glGenBuffers(1, perObjectSSBO.put());
    if (!perObjectDataBuffer.empty()) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, perObjectSSBO);
        const size_t dataSize = perObjectDataBuffer.size() * sizeof(float);
        if (dataSize > perObjectSSBOCapacity_) {
            const size_t newCapRaw = dataSize * 2;
            const size_t newCap = AlignUp(newCapRaw, SsboOffsetAlignment());
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                         nullptr,
                         GL_STREAM_DRAW);
            if (glGetError() == GL_NO_ERROR) {
                perObjectSSBOCapacity_ = newCap;
            }
        }
        if (perObjectSSBOCapacity_ > 0) {
            const uint32_t slot = NextUploadSlot(perObjectUploadCursor_);
            const size_t slotBaseOffset = static_cast<size_t>(slot) * perObjectSSBOCapacity_;
            UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                                  static_cast<GLintptr>(slotBaseOffset),
                                  dataSize,
                                  perObjectDataBuffer.data());
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, perObjectSSBO,
                              static_cast<GLintptr>(slotBaseOffset),
                              static_cast<GLsizeiptr>(dataSize));
        }
    }

    if (logThis) {
        std::cout << "    [DrawBatchSystem] SSBO uploaded: " << modelMatrices.size()
                  << " matrices (" << (modelMatrices.size() * sizeof(glm::mat4)) << " bytes)\n";
    }

    const bool useStaticIndirect = useStaticMatrixCache && !staticIndirectCommandsPacked_.empty();
    const size_t staticPackedCount = useStaticIndirect ? staticIndirectCommandsPacked_.size() : 0;

    dynamicPackedCommands_.clear();
    packedRanges_.assign(active_.size(), PackedDrawRange{});

    for (size_t i = 0; i < active_.size(); ++i) {
        DrawBatch* batch = active_[i];
        if (!batch) continue;
        PackedDrawRange& range = packedRanges_[i];

        if (useStaticIndirect) {
            auto staticCmdIt = staticBatchCommands.find(batch->key);
            auto staticOffsetIt = staticIndirectOffsets_.find(batch->key);
            if (staticCmdIt != staticBatchCommands.end() &&
                staticOffsetIt != staticIndirectOffsets_.end() &&
                !staticCmdIt->second.empty()) {
                range.staticOffset = staticOffsetIt->second;
                range.staticCount = staticCmdIt->second.size();
            }
        }

        const size_t dynamicCount = batch->commands.size();
        if (dynamicCount > 0) {
            range.dynamicOffset = staticPackedCount + dynamicPackedCommands_.size();
            range.dynamicCount = dynamicCount;
            dynamicPackedCommands_.insert(dynamicPackedCommands_.end(), batch->commands.begin(), batch->commands.end());
        }
    }

    const size_t totalPackedCommands = staticPackedCount + dynamicPackedCommands_.size();
    size_t indirectSlotBaseOffset = 0;
    auto& passIndirectBuffer = useStaticIndirect ? indirectBuffer : transientIndirectBuffer;
    size_t& passIndirectCapacity = useStaticIndirect ? indirectBufferCapacity_ : transientIndirectBufferCapacity_;
    uint32_t& passIndirectCursor = useStaticIndirect ? indirectUploadCursor_ : transientIndirectUploadCursor_;
    if (totalPackedCommands > 0) {
        if (!passIndirectBuffer) glGenBuffers(1, passIndirectBuffer.put());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, passIndirectBuffer);

        const size_t neededIndirectBytes = totalPackedCommands * sizeof(DrawCommand);
        if (neededIndirectBytes > passIndirectCapacity) {
            const size_t newCap = neededIndirectBytes ? (neededIndirectBytes * 2) : sizeof(DrawCommand);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                         nullptr,
                         GL_STREAM_DRAW);
            if (glGetError() == GL_NO_ERROR) {
                passIndirectCapacity = newCap;
                if (useStaticIndirect) {
                    staticIndirectUploadedBySlot_.fill(false);
                }
            }
        }

        if (passIndirectCapacity > 0) {
            const uint32_t slot = NextUploadSlot(passIndirectCursor);
            indirectSlotBaseOffset = static_cast<size_t>(slot) * passIndirectCapacity;
            const size_t staticBytes = staticPackedCount * sizeof(DrawCommand);
            if (useStaticIndirect && staticBytes > 0 && !staticIndirectUploadedBySlot_[slot]) {
                UploadRangeOrFallback(GL_DRAW_INDIRECT_BUFFER,
                                      static_cast<GLintptr>(indirectSlotBaseOffset),
                                      staticBytes,
                                      staticIndirectCommandsPacked_.data());
                staticIndirectUploadedBySlot_[slot] = true;
            }

            if (!dynamicPackedCommands_.empty()) {
                UploadRangeOrFallback(GL_DRAW_INDIRECT_BUFFER,
                                      static_cast<GLintptr>(indirectSlotBaseOffset + staticBytes),
                                      dynamicPackedCommands_.size() * sizeof(DrawCommand),
                                      dynamicPackedCommands_.data());
            }
        }
    }

    GLuint lastBoundTextures[12];
    GLuint lastBoundSamplers[12];
    GLenum lastBoundTargets[12];
    for (int i = 0; i < 12; ++i) {
        lastBoundTextures[i] = std::numeric_limits<GLuint>::max();
        lastBoundSamplers[i] = std::numeric_limits<GLuint>::max();
        lastBoundTargets[i] = static_cast<GLenum>(std::numeric_limits<uint32_t>::max());
    }

    size_t totalDrawCalls = 0;
    GLint lastStencilRef = std::numeric_limits<GLint>::min();
    if (totalPackedCommands > 0) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, passIndirectBuffer);
    }

    for (size_t i = 0; i < active_.size(); ++i) {
        DrawBatch* batch = active_[i];
        if (!batch) continue;
        const PackedDrawRange& range = packedRanges_[i];
        if (range.staticCount == 0 && range.dynamicCount == 0) continue;

        for (int j = 0; j < textureBindCount; ++j) {
            GLuint textureHandle = 0;
            GLenum textureTarget = GL_TEXTURE_2D;
            if (batch->key.tex[j] &&
                batch->key.tex[j]->GetType() == NDEVC::Graphics::TextureType::TextureCube) {
                textureTarget = GL_TEXTURE_CUBE_MAP;
            }
            if (batch->key.tex[j]) {
                textureHandle = *(GLuint*)batch->key.tex[j]->GetNativeHandle();
            }
            if (textureHandle == 0) {
                const auto& fallback = GetFallbackTextures();
                if (textureTarget == GL_TEXTURE_CUBE_MAP || j == 9) {
                    textureHandle = fallback.blackCube;
                    textureTarget = GL_TEXTURE_CUBE_MAP;
                } else if (j == 0 || j == 4 || j == 8) {
                    textureHandle = fallback.white;
                } else if (j == 2 || j == 6) {
                    textureHandle = fallback.normal;
                } else {
                    textureHandle = fallback.black;
                }
            }

            if (lastBoundTextures[j] != textureHandle || lastBoundTargets[j] != textureTarget) {
                glActiveTexture(GL_TEXTURE0 + j);
                if (lastBoundTargets[j] != textureTarget) {
                    glBindTexture(lastBoundTargets[j] == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 0);
                }
                glBindTexture(textureTarget, textureHandle);
                lastBoundTextures[j] = textureHandle;
                lastBoundTargets[j] = textureTarget;
            }
            const GLuint desiredSampler = (textureTarget == GL_TEXTURE_CUBE_MAP) ? 0u : samplerRepeatHandle;
            if (lastBoundSamplers[j] != desiredSampler) {
                glBindSampler(j, desiredSampler);
                lastBoundSamplers[j] = desiredSampler;
            }
        }

        if (receivesDecalsLoc >= 0) {
            glUniform1i(receivesDecalsLoc, batch->key.receivesDecals ? 1 : 0);
        }

        const GLint stencilRef = batch->key.receivesDecals ? 1 : 0;
        if (lastStencilRef != stencilRef) {
            glStencilFunc(GL_ALWAYS, stencilRef, 0xFF);
            lastStencilRef = stencilRef;
        }

        if (range.staticCount > 0) {
            const uintptr_t byteOffset =
                static_cast<uintptr_t>(indirectSlotBaseOffset + range.staticOffset * sizeof(DrawCommand));
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                        reinterpret_cast<const void*>(byteOffset),
                                        static_cast<GLsizei>(range.staticCount),
                                        0);
            totalDrawCalls += range.staticCount;
        }
        if (range.dynamicCount > 0) {
            const uintptr_t byteOffset =
                static_cast<uintptr_t>(indirectSlotBaseOffset + range.dynamicOffset * sizeof(DrawCommand));
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                        reinterpret_cast<const void*>(byteOffset),
                                        static_cast<GLsizei>(range.dynamicCount),
                                        0);
            totalDrawCalls += range.dynamicCount;
        }
    }

    if (logThis) {
        std::cout << "    [DrawBatchSystem] Issued " << totalDrawCalls << " draw calls\n";
    }

    for (int i = 0; i < textureBindCount; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glBindSampler(i, 0);
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}

void DrawBatchSystem::flushDecals(NDEVC::Graphics::ISampler* samplerRepeat, NDEVC::Graphics::ISampler* samplerClamp) {
    GLuint samplerRepeatHandle = 0;
    GLuint samplerClampHandle = 0;
    if (samplerRepeat) {
        samplerRepeatHandle = *(GLuint*)samplerRepeat->GetNativeHandle();
    }
    if (samplerClamp) {
        samplerClampHandle = *(GLuint*)samplerClamp->GetNativeHandle();
    }
    if (active_.empty()) {
        return;
    }

    MegaBuffer::instance().bind();

    const bool useStaticMatrixCache = staticMatrixCount > 0;
    auto& matrixSSBO = useStaticMatrixCache ? modelMatrixSSBO : transientModelMatrixSSBO;
    size_t& matrixCapacity = useStaticMatrixCache ? modelMatrixSSBOCapacity_ : transientModelMatrixSSBOCapacity_;
    uint32_t& matrixCursor = useStaticMatrixCache ? modelMatrixUploadCursor_ : transientModelMatrixUploadCursor_;

    if (!matrixSSBO) glGenBuffers(1, matrixSSBO.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, matrixSSBO);

    const size_t dynamicMatrixCount = modelMatrices.size();
    const size_t totalMatrixCount = staticMatrixCount + dynamicMatrixCount;
    const size_t needed = totalMatrixCount * sizeof(glm::mat4);
    if (needed > matrixCapacity) {
        const size_t newCapRaw = needed ? (needed * 2) : sizeof(glm::mat4);
        const size_t newCap = AlignUp(newCapRaw, SsboOffsetAlignment());
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                     nullptr,
                     GL_STREAM_DRAW);
        if (glGetError() == GL_NO_ERROR) {
            matrixCapacity = newCap;
            if (useStaticMatrixCache) {
                staticMatricesUploadedBySlot_.fill(false);
            }
        }
    }
    if (needed > 0 && matrixCapacity > 0) {
        const uint32_t slot = NextUploadSlot(matrixCursor);
        const size_t slotBaseOffset = static_cast<size_t>(slot) * matrixCapacity;
        const size_t staticBytes = useStaticMatrixCache ? (staticMatrixCount * sizeof(glm::mat4)) : 0;

        if (staticBytes > 0 && !staticMatricesUploadedBySlot_[slot]) {
            UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                                  static_cast<GLintptr>(slotBaseOffset),
                                  staticBytes,
                                  staticModelMatrices.data());
            staticMatricesUploadedBySlot_[slot] = true;
        }

        if (dynamicMatrixCount > 0) {
            const size_t dynamicBytes = dynamicMatrixCount * sizeof(glm::mat4);
            UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                                  static_cast<GLintptr>(slotBaseOffset + staticBytes),
                                  dynamicBytes,
                                  modelMatrices.data());
        }

        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, matrixSSBO,
                          static_cast<GLintptr>(slotBaseOffset),
                          static_cast<GLsizeiptr>(needed));
    }

    decalParamsBuffer.clear();
    decalParamsBuffer.resize(totalMatrixCount * 2u, glm::vec4(0.0f));
    if (useStaticMatrixCache && staticDecalParamsPacked_.size() == staticMatrixCount * 2u) {
        std::copy(staticDecalParamsPacked_.begin(), staticDecalParamsPacked_.end(), decalParamsBuffer.begin());
    }
    for (DrawBatch* batch : active_) {
        if (!batch) continue;
        for (size_t i = 0; i < batch->commands.size(); ++i) {
            const size_t src = i * 2u;
            if (src + 1u >= batch->decalParams.size()) break;

            const size_t dst = static_cast<size_t>(batch->commands[i].baseInstance) * 2u;
            if (dst + 1u >= decalParamsBuffer.size()) continue;

            decalParamsBuffer[dst + 0u] = batch->decalParams[src + 0u];
            decalParamsBuffer[dst + 1u] = batch->decalParams[src + 1u];
        }
    }

    if (!decalParamsSSBO) glGenBuffers(1, decalParamsSSBO.put());
    if (!decalParamsBuffer.empty()) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, decalParamsSSBO);
        const size_t dataSize = decalParamsBuffer.size() * sizeof(glm::vec4);
        if (dataSize > decalParamsSSBOCapacity_) {
            const size_t newCapRaw = dataSize * 2;
            const size_t newCap = AlignUp(newCapRaw, SsboOffsetAlignment());
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                         nullptr,
                         GL_STREAM_DRAW);
            if (glGetError() == GL_NO_ERROR) {
                decalParamsSSBOCapacity_ = newCap;
            }
        }
        if (decalParamsSSBOCapacity_ > 0) {
            const uint32_t slot = NextUploadSlot(decalParamsUploadCursor_);
            const size_t slotBaseOffset = static_cast<size_t>(slot) * decalParamsSSBOCapacity_;
            UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                                  static_cast<GLintptr>(slotBaseOffset),
                                  dataSize,
                                  decalParamsBuffer.data());
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 3, decalParamsSSBO,
                              static_cast<GLintptr>(slotBaseOffset),
                              static_cast<GLsizeiptr>(dataSize));
        }
    }

    const bool useStaticIndirect = useStaticMatrixCache && !staticIndirectCommandsPacked_.empty();
    const size_t staticPackedCount = useStaticIndirect ? staticIndirectCommandsPacked_.size() : 0;
    size_t dynamicPackedReserve = 0;
    packedRanges_.assign(active_.size(), PackedDrawRange{});
    for (DrawBatch* batch : active_) {
        if (!batch) continue;
        bool hasStatic = false;
        if (useStaticIndirect) {
            auto staticCmdIt = staticBatchCommands.find(batch->key);
            auto staticOffsetIt = staticIndirectOffsets_.find(batch->key);
            hasStatic = staticCmdIt != staticBatchCommands.end() &&
                        staticOffsetIt != staticIndirectOffsets_.end() &&
                        !staticCmdIt->second.empty();
        }
        if (!hasStatic && batch->commands.empty()) continue;
        dynamicPackedReserve += batch->commands.size();
    }

    dynamicPackedCommands_.clear();
    dynamicPackedCommands_.reserve(dynamicPackedReserve);

    for (size_t i = 0; i < active_.size(); ++i) {
        DrawBatch* batch = active_[i];
        if (!batch) continue;
        PackedDrawRange& range = packedRanges_[i];
        if (useStaticIndirect) {
            auto staticCmdIt = staticBatchCommands.find(batch->key);
            auto staticOffsetIt = staticIndirectOffsets_.find(batch->key);
            if (staticCmdIt != staticBatchCommands.end() &&
                staticOffsetIt != staticIndirectOffsets_.end() &&
                !staticCmdIt->second.empty()) {
                range.staticOffset = staticOffsetIt->second;
                range.staticCount = staticCmdIt->second.size();
            }
        }

        const size_t dynamicCount = batch->commands.size();
        if (dynamicCount > 0) {
            range.dynamicOffset = staticPackedCount + dynamicPackedCommands_.size();
            range.dynamicCount = dynamicCount;
            dynamicPackedCommands_.insert(dynamicPackedCommands_.end(), batch->commands.begin(), batch->commands.end());
        }
    }

    const size_t totalPackedCommands = staticPackedCount + dynamicPackedCommands_.size();
    size_t indirectSlotBaseOffset = 0;
    auto& passIndirectBuffer = useStaticIndirect ? indirectBuffer : transientIndirectBuffer;
    size_t& passIndirectCapacity = useStaticIndirect ? indirectBufferCapacity_ : transientIndirectBufferCapacity_;
    uint32_t& passIndirectCursor = useStaticIndirect ? indirectUploadCursor_ : transientIndirectUploadCursor_;
    if (totalPackedCommands > 0) {
        if (!passIndirectBuffer) glGenBuffers(1, passIndirectBuffer.put());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, passIndirectBuffer);

        const size_t neededIndirectBytes = totalPackedCommands * sizeof(DrawCommand);
        if (neededIndirectBytes > passIndirectCapacity) {
            const size_t newCap = neededIndirectBytes ? (neededIndirectBytes * 2) : sizeof(DrawCommand);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                         nullptr,
                         GL_STREAM_DRAW);
            if (glGetError() == GL_NO_ERROR) {
                passIndirectCapacity = newCap;
                if (useStaticIndirect) {
                    staticIndirectUploadedBySlot_.fill(false);
                }
            }
        }

        if (passIndirectCapacity > 0) {
            const uint32_t slot = NextUploadSlot(passIndirectCursor);
            indirectSlotBaseOffset = static_cast<size_t>(slot) * passIndirectCapacity;
            const size_t staticBytes = staticPackedCount * sizeof(DrawCommand);
            if (useStaticIndirect && staticBytes > 0 && !staticIndirectUploadedBySlot_[slot]) {
                UploadRangeOrFallback(GL_DRAW_INDIRECT_BUFFER,
                                      static_cast<GLintptr>(indirectSlotBaseOffset),
                                      staticBytes,
                                      staticIndirectCommandsPacked_.data());
                staticIndirectUploadedBySlot_[slot] = true;
            }

            if (!dynamicPackedCommands_.empty()) {
                UploadRangeOrFallback(GL_DRAW_INDIRECT_BUFFER,
                                      static_cast<GLintptr>(indirectSlotBaseOffset + staticBytes),
                                      dynamicPackedCommands_.size() * sizeof(DrawCommand),
                                      dynamicPackedCommands_.data());
            }
        }
    }

    if (totalPackedCommands > 0) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, passIndirectBuffer);
    }

    for (size_t i = 0; i < active_.size(); ++i) {
        DrawBatch* batch = active_[i];
        if (!batch) continue;
        if (i >= packedRanges_.size()) continue;
        const PackedDrawRange& range = packedRanges_[i];
        if (range.staticCount == 0 && range.dynamicCount == 0) continue;

        GLuint tex0 = 0;
        if (batch->key.tex[0]) {
            tex0 = *(GLuint*)batch->key.tex[0]->GetNativeHandle();
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex0);
        glBindSampler(2, samplerRepeatHandle);

        GLuint tex3 = 0;
        if (batch->key.tex[3]) {
            tex3 = *(GLuint*)batch->key.tex[3]->GetNativeHandle();
        }
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, tex3);
        glBindSampler(3, samplerClampHandle);

        if (range.staticCount > 0) {
            const uintptr_t byteOffset =
                static_cast<uintptr_t>(indirectSlotBaseOffset + range.staticOffset * sizeof(DrawCommand));
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                        reinterpret_cast<const void*>(byteOffset),
                                        static_cast<GLsizei>(range.staticCount),
                                        0);
        }
        if (range.dynamicCount > 0) {
            const uintptr_t byteOffset =
                static_cast<uintptr_t>(indirectSlotBaseOffset + range.dynamicOffset * sizeof(DrawCommand));
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                        reinterpret_cast<const void*>(byteOffset),
                                        static_cast<GLsizei>(range.dynamicCount),
                                        0);
        }
    }

    glBindSampler(2, 0);
    glBindSampler(3, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}
