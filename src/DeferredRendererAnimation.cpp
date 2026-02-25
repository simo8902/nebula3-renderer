// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "DeferredRendererAnimation.h"

#include "AnimationSystem.h"
#include "glad/glad.h"
#include "DrawCmd.h"
#include "Model/ModelInstance.h"

#include "gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "gtx/matrix_decompose.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <unordered_set>

namespace DeferredRendererAnimation {
namespace {
constexpr bool kAnimationEnabled = true;

struct PairHash {
    std::size_t operator()(const std::pair<uintptr_t, uintptr_t>& p) const {
        return std::hash<uintptr_t>()(p.first) ^ (std::hash<uintptr_t>()(p.second) << 1);
    }
};

// Generation counter incremented each frame instead of clearing maps.
// Entries with a stale frameId are treated as cache misses — O(1) invalidation
// instead of O(N) clear+dealloc every frame.
static uint32_t gAnimFrameId = 0;

struct CachedBool { bool value; uint32_t frameId; };
struct CachedMat4 { glm::mat4 value; uint32_t frameId; };

std::unordered_map<std::pair<uintptr_t, uintptr_t>, CachedBool, PairHash> gAnimPoseNodeQueryCache;
std::unordered_map<std::pair<uintptr_t, uintptr_t>, CachedBool, PairHash> gNodeHasAnimatedAncestorCache;
std::unordered_map<std::pair<uintptr_t, uintptr_t>, CachedMat4, PairHash> gAnimatedHierarchyLocalCache;

struct ShaderVarTargetBinding {
    std::string sourceName;
    const AnimSection* section = nullptr;
};
std::unordered_map<std::string, std::vector<ShaderVarTargetBinding>> gShaderVarTargetBindings;

glm::mat4 ComposeTRSWithPivots(const glm::vec3& pos,
                               const glm::quat& rot,
                               const glm::vec3& scale,
                               const glm::vec3& rotatePivot,
                               const glm::vec3& scalePivot) {
    return glm::translate(glm::mat4(1.0f), pos)
        * glm::translate(glm::mat4(1.0f), rotatePivot)
        * glm::mat4_cast(rot)
        * glm::translate(glm::mat4(1.0f), -rotatePivot)
        * glm::translate(glm::mat4(1.0f), scalePivot)
        * glm::scale(glm::mat4(1.0f), scale)
        * glm::translate(glm::mat4(1.0f), -scalePivot);
}

glm::mat4 ComposeNodeStaticLocal(const Node* node) {
    if (!node) return glm::mat4(1.0f);
    const glm::vec3 pPos(node->position.x, node->position.y, node->position.z);
    const glm::quat pRot(node->rotation.w, node->rotation.x, node->rotation.y, node->rotation.z);
    const glm::vec3 pScale(node->scale.x, node->scale.y, node->scale.z);
    const glm::vec3 rPivot(node->rotate_pivot.x, node->rotate_pivot.y, node->rotate_pivot.z);
    const glm::vec3 sPivot(node->scale_pivot.x, node->scale_pivot.y, node->scale_pivot.z);
    return ComposeTRSWithPivots(pPos, pRot, pScale, rPivot, sPivot);
}

std::pair<uintptr_t, uintptr_t> OwnerNodeCacheKey(const void* owner, const std::string& nodeName) {
    return {reinterpret_cast<uintptr_t>(owner), std::hash<std::string>()(nodeName)};
}

std::pair<uintptr_t, uintptr_t> OwnerPtrCacheKey(const void* owner, const Node* node) {
    return {reinterpret_cast<uintptr_t>(owner), reinterpret_cast<uintptr_t>(node)};
}

glm::mat4 ComposeNodeAnimatedLocal(const Node* node, const void* owner) {
    if (!node) return glm::mat4(1.0f);
    const glm::mat4 animTRS = MakeLocal(owner, node->node_name);

    glm::vec3 skew(0.0f);
    glm::vec4 perspective(0.0f);
    glm::vec3 pos(0.0f), scale(1.0f);
    glm::quat rot(1, 0, 0, 0);
    if (!glm::decompose(animTRS, scale, rot, pos, skew, perspective)) {
        return animTRS;
    }

    rot = glm::normalize(rot);
    const glm::vec3 rPivot(node->rotate_pivot.x, node->rotate_pivot.y, node->rotate_pivot.z);
    const glm::vec3 sPivot(node->scale_pivot.x, node->scale_pivot.y, node->scale_pivot.z);
    return ComposeTRSWithPivots(pos, rot, scale, rPivot, sPivot);
}

bool HasAnimatedPoseForNodeInternal(const std::string& nodeName, const void* owner) {
    if (nodeName.empty()) return false;
    const auto cacheKey = OwnerNodeCacheKey(owner, nodeName);
    auto cached = gAnimPoseNodeQueryCache.find(cacheKey);
    if (cached != gAnimPoseNodeQueryCache.end() && cached->second.frameId == gAnimFrameId) {
        return cached->second.value;
    }

    const bool hasPose = HasAnimatedPose(owner, nodeName);

    gAnimPoseNodeQueryCache[cacheKey] = {hasPose, gAnimFrameId};
    return hasPose;
}

bool HasAnimatedAncestor(const Node* node, const void* owner) {
    if (!node) return false;
    const auto cacheKey = OwnerPtrCacheKey(owner, node);
    auto it = gNodeHasAnimatedAncestorCache.find(cacheKey);
    if (it != gNodeHasAnimatedAncestorCache.end() && it->second.frameId == gAnimFrameId) {
        return it->second.value;
    }

    const bool has = HasAnimatedPoseForNodeInternal(node->node_name, owner) || HasAnimatedAncestor(node->node_parent, owner);
    gNodeHasAnimatedAncestorCache[cacheKey] = {has, gAnimFrameId};
    return has;
}

glm::mat4 ComposeAnimatedHierarchyLocalCached(const Node* leaf, const void* owner) {
    if (!leaf) return glm::mat4(1.0f);
    const auto cacheKey = OwnerPtrCacheKey(owner, leaf);
    auto it = gAnimatedHierarchyLocalCache.find(cacheKey);
    if (it != gAnimatedHierarchyLocalCache.end() && it->second.frameId == gAnimFrameId) {
        return it->second.value;
    }

    glm::mat4 parentLocal = glm::mat4(1.0f);
    if (leaf->node_parent) {
        parentLocal = ComposeAnimatedHierarchyLocalCached(leaf->node_parent, owner);
    }

    const glm::mat4 nodeLocal = HasAnimatedPoseForNodeInternal(leaf->node_name, owner)
        ? ComposeNodeAnimatedLocal(leaf, owner)
        : ComposeNodeStaticLocal(leaf);

    const glm::mat4 combined = parentLocal * nodeLocal;
    gAnimatedHierarchyLocalCache[cacheKey] = {combined, gAnimFrameId};
    return combined;
}

float ResolveClipTimeForSourceNode(const std::string& sourceNodeName, float fallbackTime, const void* owner) {
    for (auto it = gClips.rbegin(); it != gClips.rend(); ++it) {
        if (it->active && it->owner == owner && it->node == sourceNodeName) {
            if (it->t > 0.0f) return it->t;
            return fallbackTime;
        }
    }
    return fallbackTime;
}

float ResolveClipTimeForSection(const std::string& sourceNodeName, const AnimSection& section, float fallbackTime, const void* owner) {
    const float sourceTime = ResolveClipTimeForSourceNode(sourceNodeName, fallbackTime, owner);
    if (sourceTime != fallbackTime) return sourceTime;

    for (const auto& animPath : section.animatedNodesPath) {
        const std::string targetNodeName = LeafNodeName(animPath);
        if (targetNodeName.empty()) continue;
        for (auto it = gClips.rbegin(); it != gClips.rend(); ++it) {
            if (!it->active || it->owner != owner || it->node != targetNodeName) continue;
            if (it->t > 0.0f) return it->t;
            return fallbackTime;
        }
    }
    return fallbackTime;
}

} // namespace

bool IsAnimationEnabled() {
    return kAnimationEnabled;
}

std::string LeafNodeName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

bool LooksLikeAnimationResourcePath(const std::string& value) {
    if (value.empty()) return false;

    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.rfind("ani:", 0) == 0) return true;
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".nax3") == 0) return true;
    return lower.find('/') != std::string::npos || lower.find('\\') != std::string::npos;
}

void BeginFrameAnimationCaches() {
    // Advance the generation counter — O(1) invalidation of all cache entries.
    // Entries are reused across frames; stale entries are simply overwritten on next access.
    ++gAnimFrameId;
}

bool HasAnimatedPoseForNode(const std::string& nodeName, const void* owner) {
    if (!kAnimationEnabled) return false;
    return HasAnimatedPoseForNodeInternal(nodeName, owner);
}

std::unordered_map<std::string, float> SampleShaderVarAnimations(const Node* node, float time) {
    if (!kAnimationEnabled) return {};
    std::unordered_map<std::string, float> result;
    if (!node || node->animSections.empty()) return result;

    for (const auto& section : node->animSections) {
        if (section.shaderVarSemantic.empty() || section.floatKeyArray.empty()) continue;

        const auto& keys = section.floatKeyArray;
        if (keys.size() == 1) {
            result[section.shaderVarSemantic] = keys[0].value;
            continue;
        }

        float t = time;
        const float minTime = keys.front().time;
        const float maxTime = keys.back().time;

        if (maxTime > 0.0f) {
            t = t - (std::floor(t / maxTime) * maxTime);
            if (t < minTime) t = minTime;
            else if (t >= maxTime) t = maxTime - 0.001f;
        }

        int idx0 = 0;
        int idx1 = 0;
        for (size_t i = 0; i < keys.size() - 1; ++i) {
            if (keys[i + 1].time >= t) {
                idx0 = static_cast<int>(i);
                idx1 = static_cast<int>(i + 1);
                break;
            }
        }

        const float t0 = keys[idx0].time;
        const float t1 = keys[idx1].time;
        const float v0 = keys[idx0].value;
        const float v1 = keys[idx1].value;
        const float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
        result[section.shaderVarSemantic] = glm::mix(v0, v1, alpha);
    }

    return result;
}

glm::mat4 ComposeAnimatedHierarchyLocal(const Node* leaf, const glm::mat4& fallbackLocal, const void* owner) {
    if (!kAnimationEnabled) return fallbackLocal;
    if (!leaf) return fallbackLocal;
    if (!HasAnimatedAncestor(leaf, owner)) return fallbackLocal;
    return ComposeAnimatedHierarchyLocalCached(leaf, owner);
}

void RebuildShaderVarTargetBindings(const std::unordered_map<std::string, Node*>& nodeMap) {
    if (!kAnimationEnabled) {
        gShaderVarTargetBindings.clear();
        return;
    }
    gShaderVarTargetBindings.clear();

    for (const auto& [sourceName, sourceNode] : nodeMap) {
        if (!sourceNode) continue;
        if (sourceNode->animSections.empty()) continue;

        for (const auto& section : sourceNode->animSections) {
            if (section.shaderVarSemantic.empty() || section.floatKeyArray.empty()) continue;

            if (section.animatedNodesPath.empty()) {
                gShaderVarTargetBindings[sourceName].push_back(ShaderVarTargetBinding{sourceName, &section});
                continue;
            }

            for (const auto& path : section.animatedNodesPath) {
                const std::string targetLeaf = LeafNodeName(path);
                if (targetLeaf.empty()) continue;
                gShaderVarTargetBindings[targetLeaf].push_back(ShaderVarTargetBinding{sourceName, &section});
            }
        }
    }
}

std::unordered_map<std::string, float> SampleShaderVarAnimationsForTarget(const std::string& targetNodeName, float time, const void* owner) {
    if (!kAnimationEnabled) return {};
    std::unordered_map<std::string, float> result;
    if (targetNodeName.empty()) return result;

    auto targetIt = gShaderVarTargetBindings.find(targetNodeName);
    if (targetIt == gShaderVarTargetBindings.end()) {
        return result;
    }

    for (const ShaderVarTargetBinding& binding : targetIt->second) {
        if (!binding.section) continue;
        const AnimSection& section = *binding.section;
        const float sourceTime = ResolveClipTimeForSection(binding.sourceName, section, time, owner);

        const auto& keys = section.floatKeyArray;
        if (keys.size() == 1) {
            result[section.shaderVarSemantic] = keys[0].value;
            continue;
        }

        float t = sourceTime;
        const float minTime = keys.front().time;
        const float maxTime = keys.back().time;

        if (maxTime > 0.0f) {
            t = t - (std::floor(t / maxTime) * maxTime);
            if (t < minTime) t = minTime;
            else if (t >= maxTime) t = maxTime - 0.001f;
        }

        int idx0 = 0;
        int idx1 = 0;
        for (size_t i = 0; i < keys.size() - 1; ++i) {
            if (keys[i + 1].time >= t) {
                idx0 = static_cast<int>(i);
                idx1 = static_cast<int>(i + 1);
                break;
            }
        }

        const float t0 = keys[idx0].time;
        const float t1 = keys[idx1].time;
        const float v0 = keys[idx0].value;
        const float v1 = keys[idx1].value;
        const float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
        float finalValue = glm::mix(v0, v1, alpha);
        result[section.shaderVarSemantic] = finalValue;
    }

    return result;
}

void ApplyAnimatedDrawWorldTransforms(const std::vector<DrawCmd*>& animatedDraws, const Camera::Frustum& frustum) {
    if (!kAnimationEnabled) return;
    if (animatedDraws.empty()) return;

    for (DrawCmd* dc : animatedDraws) {
        if (!dc || !dc->instance) continue;

        if (dc->cullBoundsValid) {
            dc->frustumCulled = false;
            const float radius = dc->cullWorldRadius;
            for (int i = 0; i < 6; ++i) {
                const float dist = glm::dot(frustum.planes[i].normal, dc->cullWorldCenter) + frustum.planes[i].d;
                if (dist < -radius * 1.2f) {
                    dc->frustumCulled = true;
                    break;
                }
            }
            if (dc->frustumCulled) continue;
        }

        auto* modelInst = static_cast<ModelInstance*>(dc->instance);
        glm::mat4 localTransform = dc->rootMatrix;
        if (dc->sourceNode) {
            localTransform = ComposeAnimatedHierarchyLocal(dc->sourceNode, dc->rootMatrix, dc->instance);
        }
        dc->worldMatrix = modelInst->getTransform() * localTransform;
        dc->cullBoundsValid = false;
    }
}

void TickAnimationFrame(double deltaTime, const std::vector<DrawCmd*>& animatedDraws, const Camera::Frustum& frustum) {
    if (!kAnimationEnabled) return;
    (void)frustum;
    (void)animatedDraws;
    for (auto& c : gClips) {
        c.speed = 1.0f;
    }

    UpdateAnimations(static_cast<float>(deltaTime));
}

} // namespace DeferredRendererAnimation
