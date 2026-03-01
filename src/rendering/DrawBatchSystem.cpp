// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DrawBatchSystem.h"
#include "Rendering/Rendering.h"
#include "Rendering/MegaBuffer.h"
#include "glad/glad.h"
#include <algorithm>
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

inline void EnsureCullBounds(const DrawCmd& obj, glm::vec3& worldCenter, float& radius) {
    if (obj.cullBoundsValid) {
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

    if (invalidateStaticCache) {
        batches_.clear();
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
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
    staticIndirectCommandsPacked_.clear();
    staticIndirectOffsets_.clear();
    staticCmdDrawIndices_.clear();
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
    if (staticCmdDrawIndices_.size() != staticIndirectCommandsPacked_.size()) return;
    bool changed = false;
    for (size_t i = 0; i < staticCmdDrawIndices_.size(); ++i) {
        const size_t di = staticCmdDrawIndices_[i];
        if (di >= solidDraws.size()) continue;
        const uint32_t want = solidDraws[di].disabled ? 0 : 1;
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
    const bool buildingStaticBatches = staticModelMatrices.empty();

    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();

    if (!buildingStaticBatches) {
        staticMatrixCount = staticModelMatrices.size();
    } else {
        staticMatrixCount = 0;
        staticModelMatrices.clear();
        staticBatchCommands.clear();
        staticBatchDrawIndices_.clear();
        staticIndirectCommandsPacked_.clear();
        staticIndirectOffsets_.clear();
        staticCmdDrawIndices_.clear();
    }

    if (buildingStaticBatches) {
        size_t drawIdx = 0;
        for (const auto& obj : solidDraws) {
            ++drawIdx;
            if (!obj.mesh) continue;
            // NOTE: do NOT skip disabled here — all statics go into cache.
            // Visibility is handled by updateStaticVisibility() toggling instanceCount.
            if (!obj.isStatic) continue;

            // Static cache must contain all static solids; frustum-culling here
            // causes permanently missing geometry based on first-view camera.
            EnsureCullBounds(obj, obj.cullWorldCenter, obj.cullWorldRadius);

            BatchKey key;
            key.tex[0] = obj.tex[0];
            key.tex[1] = obj.tex[1];
            key.tex[2] = obj.tex[2];
            key.tex[3] = obj.tex[3];
            key.receivesDecals = obj.receivesDecals;
            key.shaderHash = 0;

            auto it = batches_.find(key);
            if (it == batches_.end()) {
                it = batches_.emplace(key, DrawBatch{key}).first;
            }
            DrawBatch& batch = it->second;
            batch.key = key;
            auto& staticCommands = staticBatchCommands[key];
            auto& staticDrawIdxs = staticBatchDrawIndices_[key];
            const uint32_t baseInstance = static_cast<uint32_t>(staticModelMatrices.size());
            bool matrixAllocated = false;
            auto ensureMatrix = [&]() {
                if (!matrixAllocated) {
                    staticModelMatrices.push_back(obj.worldMatrix);
                    matrixAllocated = true;
                }
            };

            const size_t srcIdx = drawIdx - 1; // drawIdx was pre-incremented
            const uint32_t instCount = obj.disabled ? 0 : 1;
            if (obj.group >= 0 && obj.group < (int)obj.mesh->groups.size()) {
                auto& g = obj.mesh->groups[obj.group];
                if (!IsValidGroupForDraw(obj, g)) continue;
                ensureMatrix();
                DrawCommand cmd{};
                cmd.count = g.indexCount();
                cmd.instanceCount = instCount;
                cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                cmd.baseVertex = 0;
                cmd.baseInstance = baseInstance;
                staticCommands.push_back(cmd);
                staticDrawIdxs.push_back(srcIdx);
            } else {
                for (size_t gi = 0; gi < obj.mesh->groups.size(); ++gi) {
                    auto& g = obj.mesh->groups[gi];
                    if (!IsValidGroupForDraw(obj, g)) continue;
                    ensureMatrix();
                    DrawCommand cmd{};
                    cmd.count = g.indexCount();
                    cmd.instanceCount = instCount;
                    cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
                    cmd.baseVertex = 0;
                    cmd.baseInstance = baseInstance;
                    staticCommands.push_back(cmd);
                    staticDrawIdxs.push_back(srcIdx);
                }
            }

            active_.push_back(&batch);
        }
        staticMatrixCount = staticModelMatrices.size();
    }

    for (const auto& obj : solidDraws) {
        if (!obj.mesh) continue;
        if (obj.disabled) continue;
        if (obj.isStatic) continue;

        glm::vec3 worldCenter;
        float radius = 0.0f;
        EnsureCullBounds(obj, worldCenter, radius);

        bool visible = true;
        if (!IsFrustumCullingDisabled()) {
            for (int i = 0; i < 6; ++i) {
                float dist = glm::dot(frustum.planes[i].normal, worldCenter) + frustum.planes[i].d;
                if (dist < -radius * 1.2f) {
                    visible = false;
                    break;
                }
            }
        }
        if (!visible) continue;

        BatchKey key;
        key.tex[0] = obj.tex[0];
        key.tex[1] = obj.tex[1];
        key.tex[2] = obj.tex[2];
        key.tex[3] = obj.tex[3];
        key.receivesDecals = obj.receivesDecals;
        key.shaderHash = 0;

        auto it = batches_.find(key);
        if (it == batches_.end()) {
            it = batches_.emplace(key, DrawBatch{key}).first;
        }
        DrawBatch& batch = it->second;
        batch.key = key;
        const uint32_t baseInstance =
            static_cast<uint32_t>(staticMatrixCount + modelMatrices.size());
        bool matrixAllocated = false;
        auto ensureMatrix = [&]() {
            if (!matrixAllocated) {
                modelMatrices.push_back(obj.worldMatrix);
                matrixAllocated = true;
            }
        };

        if (obj.group >= 0 && obj.group < (int)obj.mesh->groups.size()) {
            auto& g = obj.mesh->groups[obj.group];
            if (!IsValidGroupForDraw(obj, g)) continue;
            ensureMatrix();
            DrawCommand cmd{};
            cmd.count = g.indexCount();
            cmd.instanceCount = 1;
            cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
            cmd.baseVertex = 0;
            cmd.baseInstance = baseInstance;
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
                batch.commands.push_back(cmd);
            }
        }

        active_.push_back(&batch);
    }

    if (!buildingStaticBatches) {
        for (auto& [k, batch] : batches_) {
            const bool hasDynamic = !batch.commands.empty();
            auto sit = staticBatchCommands.find(k);
            const bool hasStatic = sit != staticBatchCommands.end() && !sit->second.empty();
            if (hasDynamic || hasStatic) {
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
        for (const auto& [k, staticCmds] : staticBatchCommands) {
            if (!staticCmds.empty()) {
                staticIndirectOffsets_[k] = staticIndirectCommandsPacked_.size();
                staticIndirectCommandsPacked_.insert(staticIndirectCommandsPacked_.end(),
                                                     staticCmds.begin(),
                                                     staticCmds.end());
                auto idxIt = staticBatchDrawIndices_.find(k);
                if (idxIt != staticBatchDrawIndices_.end()) {
                    staticCmdDrawIndices_.insert(staticCmdDrawIndices_.end(),
                                                 idxIt->second.begin(),
                                                 idxIt->second.end());
                }
                auto bit = batches_.find(k);
                if (bit != batches_.end()) {
                    bit->second.staticCommandCount = staticCmds.size();
                }
            }
        }
        staticIndirectUploadedBySlot_.fill(false);
    }
}

void DrawBatchSystem::cullGeneric(const std::vector<DrawCmd>& draws, const Camera::Frustum& frustum,
                                    uint32_t shaderHash, int numTextures) {
    staticMatrixCount = 0;
    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.perObjectData.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();
    perObjectDataBuffer.clear();

    for (const auto& obj : draws) {
        if (!obj.mesh) continue;
        if (obj.disabled) continue;

        glm::vec3 worldCenter;
        float radius = 0.0f;
        EnsureCullBounds(obj, worldCenter, radius);

        bool visible = true;
        if (!IsFrustumCullingDisabled()) {
            for (int i = 0; i < 6; ++i) {
                float dist = glm::dot(frustum.planes[i].normal, worldCenter) + frustum.planes[i].d;
                if (dist < -radius * 1.2f) {
                    visible = false;
                    break;
                }
            }
        }
        if (!visible) continue;

        BatchKey key;
        for (int i = 0; i < numTextures && i < 12; ++i) {
            key.tex[i] = obj.tex[i];
        }
        key.receivesDecals = obj.receivesDecals;
        key.shaderHash = shaderHash;

        auto it = batches_.find(key);
        if (it == batches_.end())
            it = batches_.emplace(key, DrawBatch{key}).first;
        DrawBatch& batch = it->second;
        const uint32_t baseInstance = static_cast<uint32_t>(modelMatrices.size());
        bool matrixAllocated = false;
        auto ensureMatrix = [&]() {
            if (!matrixAllocated) {
                modelMatrices.push_back(obj.worldMatrix);
                matrixAllocated = true;
            }
        };

        if (obj.group >= 0 && obj.group < (int)obj.mesh->groups.size()) {
            auto& g = obj.mesh->groups[obj.group];
            if (!IsValidGroupForDraw(obj, g)) continue;
            ensureMatrix();
            DrawCommand cmd{};
            cmd.count = g.indexCount();
            cmd.instanceCount = 1;
            cmd.firstIndex = obj.megaIndexOffset + g.firstIndex();
            cmd.baseVertex = 0;
            cmd.baseInstance = baseInstance;

            batch.perObjectData.push_back(obj.alphaCutoff);
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

                batch.perObjectData.push_back(obj.alphaCutoff);
                batch.commands.push_back(cmd);
            }
        }

        active_.push_back(&batch);
    }

    if (!active_.empty()) {
        std::sort(active_.begin(), active_.end(), BatchPtrLess);
        active_.erase(std::unique(active_.begin(), active_.end()), active_.end());
    }
}

void DrawBatchSystem::cullDecals(const std::vector<DrawCmd>& draws) {
    staticMatrixCount = 0;
    for (auto& [k, batch] : batches_) {
        batch.commands.clear();
        batch.decalParams.clear();
        batch.staticCommandCount = 0;
    }
    active_.clear();
    modelMatrices.clear();
    decalParamsBuffer.clear();

    int validCount = 0;
    for (auto& obj : draws) {
        if (!obj.mesh) continue;
        if (obj.disabled) continue;

        std::string shdrLower = obj.shdr;
        std::transform(shdrLower.begin(), shdrLower.end(), shdrLower.begin(), ::tolower);
        const bool isDecalReceiver =
            shdrLower.find("decalreceive") != std::string::npos ||
            shdrLower.find("decal_receive") != std::string::npos;
        bool isValidDecal = !isDecalReceiver &&
                            ((shdrLower == "shd:decal") ||
                             (shdrLower.find("decal") != std::string::npos) ||
                             obj.isDecal);
        if (!isValidDecal) continue;
        validCount++;

        BatchKey key;
        key.tex[0] = obj.tex[0];
        key.tex[3] = obj.tex[3];
        key.receivesDecals = false;
        key.shaderHash = 0xDECAL;

        auto it = batches_.find(key);
        if (it == batches_.end())
            it = batches_.emplace(key, DrawBatch{key}).first;
        DrawBatch& batch = it->second;
        const uint32_t baseInstance = static_cast<uint32_t>(modelMatrices.size());
        bool matrixAllocated = false;
        auto ensureMatrix = [&]() {
            if (!matrixAllocated) {
                modelMatrices.push_back(obj.worldMatrix);
                matrixAllocated = true;
            }
        };

        if (obj.group >= 0 && obj.group < (int)obj.mesh->groups.size()) {
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
            params2.w = (float)obj.cullMode;
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
                params2.w = (float)obj.cullMode;
                batch.decalParams.push_back(params2);

                batch.commands.push_back(cmd);
            }
        }

        active_.push_back(&batch);
    }

    if (!active_.empty()) {
        std::sort(active_.begin(), active_.end(), BatchPtrLess);
        active_.erase(std::unique(active_.begin(), active_.end()), active_.end());
    }

    (void)validCount;
}

void DrawBatchSystem::flush(NDEVC::Graphics::ISampler* samplerRepeat, int numTexturesToBind) {
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
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram != 0) {
        static std::unordered_map<GLuint, GLint> receivesDecalsLocCache;
        const GLuint program = static_cast<GLuint>(currentProgram);
        auto it = receivesDecalsLocCache.find(program);
        if (it != receivesDecalsLocCache.end()) {
            receivesDecalsLoc = it->second;
        } else {
            receivesDecalsLoc = glGetUniformLocation(program, "ReceivesDecals");
            receivesDecalsLocCache.emplace(program, receivesDecalsLoc);
        }
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

    struct PackedDrawRange {
        size_t staticOffset = 0;
        size_t staticCount = 0;
        size_t dynamicOffset = 0;
        size_t dynamicCount = 0;
    };

    const bool useStaticIndirect = useStaticMatrixCache && !staticIndirectCommandsPacked_.empty();
    const size_t staticPackedCount = useStaticIndirect ? staticIndirectCommandsPacked_.size() : 0;
    size_t dynamicPackedReserve = 0;
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

    std::vector<DrawCommand> dynamicPackedCommands;
    dynamicPackedCommands.reserve(dynamicPackedReserve);
    std::unordered_map<DrawBatch*, PackedDrawRange> packedRanges;
    packedRanges.reserve(active_.size());

    for (DrawBatch* batch : active_) {
        if (!batch) continue;

        PackedDrawRange range{};
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
            range.dynamicOffset = staticPackedCount + dynamicPackedCommands.size();
            range.dynamicCount = dynamicCount;
            dynamicPackedCommands.insert(dynamicPackedCommands.end(), batch->commands.begin(), batch->commands.end());
        }

        if (range.staticCount == 0 && range.dynamicCount == 0) {
            continue;
        }

        packedRanges.emplace(batch, range);
    }

    const size_t totalPackedCommands = staticPackedCount + dynamicPackedCommands.size();
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

            if (!dynamicPackedCommands.empty()) {
                UploadRangeOrFallback(GL_DRAW_INDIRECT_BUFFER,
                                      static_cast<GLintptr>(indirectSlotBaseOffset + staticBytes),
                                      dynamicPackedCommands.size() * sizeof(DrawCommand),
                                      dynamicPackedCommands.data());
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
    if (totalPackedCommands > 0) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, passIndirectBuffer);
    }

    for (DrawBatch* batch : active_) {
        if (!batch) continue;
        auto rangeIt = packedRanges.find(batch);
        if (rangeIt == packedRanges.end()) continue;
        const PackedDrawRange& range = rangeIt->second;

        for (int i = 0; i < textureBindCount; ++i) {
            GLuint textureHandle = 0;
            GLenum textureTarget = GL_TEXTURE_2D;
            if (batch->key.tex[i] &&
                batch->key.tex[i]->GetType() == NDEVC::Graphics::TextureType::TextureCube) {
                textureTarget = GL_TEXTURE_CUBE_MAP;
            }
            if (batch->key.tex[i]) {
                textureHandle = *(GLuint*)batch->key.tex[i]->GetNativeHandle();
            }
            if (textureHandle == 0) {
                const auto& fallback = GetFallbackTextures();
                if (textureTarget == GL_TEXTURE_CUBE_MAP || i == 9) {
                    textureHandle = fallback.blackCube;
                    textureTarget = GL_TEXTURE_CUBE_MAP;
                } else if (i == 0 || i == 4 || i == 8) {
                    textureHandle = fallback.white;
                } else if (i == 2 || i == 6) {
                    textureHandle = fallback.normal;
                } else {
                    textureHandle = fallback.black;
                }
            }

            if (lastBoundTextures[i] != textureHandle || lastBoundTargets[i] != textureTarget) {
                glActiveTexture(GL_TEXTURE0 + i);
                if (lastBoundTargets[i] != textureTarget) {
                    glBindTexture(lastBoundTargets[i] == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 0);
                }
                glBindTexture(textureTarget, textureHandle);
                lastBoundTextures[i] = textureHandle;
                lastBoundTargets[i] = textureTarget;
            }
            if (lastBoundSamplers[i] != samplerRepeatHandle) {
                glBindSampler(i, samplerRepeatHandle);
                lastBoundSamplers[i] = samplerRepeatHandle;
            }
        }

        if (receivesDecalsLoc >= 0) {
            glUniform1i(receivesDecalsLoc, batch->key.receivesDecals ? 1 : 0);
        }

        glStencilFunc(GL_ALWAYS, batch->key.receivesDecals ? 1 : 0, 0xFF);

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
    constexpr bool kDecalDebugLog = false;
    GLuint samplerRepeatHandle = 0;
    GLuint samplerClampHandle = 0;
    if (samplerRepeat) {
        samplerRepeatHandle = *(GLuint*)samplerRepeat->GetNativeHandle();
    }
    if (samplerClamp) {
        samplerClampHandle = *(GLuint*)samplerClamp->GetNativeHandle();
    }
    if (active_.empty()) {
        if (kDecalDebugLog) {
            std::cout << "[flushDecals] No active batches\n";
        }
        return;
    }

    if (kDecalDebugLog) {
        std::cout << "[flushDecals] Active batches: " << active_.size() << ", Model matrices: " << modelMatrices.size() << "\n";
    }

    if (!transientModelMatrixSSBO) glGenBuffers(1, transientModelMatrixSSBO.put());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transientModelMatrixSSBO);

    size_t needed = modelMatrices.size() * sizeof(glm::mat4);
    if (needed > transientModelMatrixSSBOCapacity_) {
        const size_t newCapRaw = needed ? (needed * 2) : sizeof(glm::mat4);
        const size_t newCap = AlignUp(newCapRaw, SsboOffsetAlignment());
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                     nullptr,
                     GL_STREAM_DRAW);
        if (glGetError() == GL_NO_ERROR) {
            transientModelMatrixSSBOCapacity_ = newCap;
        }
    }
    if (needed > 0 && transientModelMatrixSSBOCapacity_ > 0) {
        const uint32_t slot = NextUploadSlot(transientModelMatrixUploadCursor_);
        const size_t slotBaseOffset = static_cast<size_t>(slot) * transientModelMatrixSSBOCapacity_;
        UploadRangeOrFallback(GL_SHADER_STORAGE_BUFFER,
                              static_cast<GLintptr>(slotBaseOffset),
                              needed,
                              modelMatrices.data());

        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, transientModelMatrixSSBO,
                          static_cast<GLintptr>(slotBaseOffset),
                          static_cast<GLsizeiptr>(needed));
    }

    decalParamsBuffer.clear();
    decalParamsBuffer.resize(modelMatrices.size() * 2u, glm::vec4(0.0f));
    for (DrawBatch* batch : active_) {
        if (batch->commands.empty()) continue;
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
        if (kDecalDebugLog) {
            std::cout << "[flushDecals] DecalParams buffer size: " << decalParamsBuffer.size() << "\n";
        }
        if (kDecalDebugLog && decalParamsBuffer.size() >= 2) {
            std::cout << "  First decal params[0]: " << decalParamsBuffer[0].x << ", " << decalParamsBuffer[0].y << ", " << decalParamsBuffer[0].z << ", " << decalParamsBuffer[0].w << "\n";
            std::cout << "  First decal params[1]: " << decalParamsBuffer[1].x << ", " << decalParamsBuffer[1].y << ", " << decalParamsBuffer[1].z << ", " << decalParamsBuffer[1].w << "\n";
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, decalParamsSSBO);
        size_t dataSize = decalParamsBuffer.size() * sizeof(glm::vec4);
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

    struct PackedDrawRange {
        size_t offset = 0;
        size_t count = 0;
    };

    std::vector<DrawCommand> packedCommands;
    size_t totalCommands = 0;
    for (DrawBatch* batch : active_) {
        if (!batch) continue;
        totalCommands += batch->commands.size();
    }
    packedCommands.reserve(totalCommands);
    std::unordered_map<DrawBatch*, PackedDrawRange> packedRanges;
    packedRanges.reserve(active_.size());

    for (DrawBatch* batch : active_) {
        if (!batch || batch->commands.empty()) continue;
        PackedDrawRange range{};
        range.offset = packedCommands.size();
        range.count = batch->commands.size();
        packedCommands.insert(packedCommands.end(), batch->commands.begin(), batch->commands.end());
        packedRanges.emplace(batch, range);
    }

    size_t indirectSlotBaseOffset = 0;
    if (!packedCommands.empty()) {
        if (!transientIndirectBuffer) glGenBuffers(1, transientIndirectBuffer.put());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, transientIndirectBuffer);

        const size_t neededBytes = packedCommands.size() * sizeof(DrawCommand);
        if (neededBytes > transientIndirectBufferCapacity_) {
            const size_t newCap = neededBytes ? (neededBytes * 2) : sizeof(DrawCommand);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(newCap * kUploadRingSize),
                         nullptr,
                         GL_STREAM_DRAW);
            if (glGetError() == GL_NO_ERROR) {
                transientIndirectBufferCapacity_ = newCap;
            }
        }

        if (transientIndirectBufferCapacity_ > 0) {
            const uint32_t slot = NextUploadSlot(transientIndirectUploadCursor_);
            indirectSlotBaseOffset = static_cast<size_t>(slot) * transientIndirectBufferCapacity_;
            UploadRangeOrFallback(GL_DRAW_INDIRECT_BUFFER,
                                  static_cast<GLintptr>(indirectSlotBaseOffset),
                                  neededBytes,
                                  packedCommands.data());
        }
    }

    if (!packedCommands.empty()) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, transientIndirectBuffer);
    }

    for (DrawBatch* batch : active_) {
        if (!batch || batch->commands.empty()) continue;
        auto rangeIt = packedRanges.find(batch);
        if (rangeIt == packedRanges.end()) continue;
        const PackedDrawRange& range = rangeIt->second;

        if (kDecalDebugLog) {
            std::cout << "[flushDecals] Batch: " << batch->commands.size() << " draws, tex[0]=" << batch->key.tex[0] << ", tex[3]=" << batch->key.tex[3] << "\n";
        }

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

        const uintptr_t byteOffset =
            static_cast<uintptr_t>(indirectSlotBaseOffset + range.offset * sizeof(DrawCommand));
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                    reinterpret_cast<const void*>(byteOffset),
                                    static_cast<GLsizei>(range.count),
                                    0);
    }

    glBindSampler(2, 0);
    glBindSampler(3, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}
