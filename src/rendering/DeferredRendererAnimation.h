// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_DR_ANIMATIONSYSTEM_H
#define NDEVC_DR_ANIMATIONSYSTEM_H

#include "glm.hpp"
#include "Rendering/Camera.h"

#include <string>
#include <unordered_map>
#include <vector>

struct Node;
struct DrawCmd;

namespace DeferredRendererAnimation {

bool IsAnimationEnabled();

std::string LeafNodeName(const std::string& path);
bool LooksLikeAnimationResourcePath(const std::string& value);

void BeginFrameAnimationCaches();
bool HasAnimatedPoseForNode(const std::string& nodeName, const void* owner = nullptr);

void SampleShaderVarAnimations(const Node* node, float time, std::unordered_map<std::string, float>& outResult);
std::unordered_map<std::string, float> SampleShaderVarAnimationsForTarget(const std::string& targetNodeName, float time, const void* owner = nullptr);

glm::mat4 ComposeAnimatedHierarchyLocal(const Node* leaf, const glm::mat4& fallbackLocal, const void* owner = nullptr);
void RebuildShaderVarTargetBindings(const std::unordered_map<std::string, Node*>& nodeMap);

void ApplyAnimatedDrawWorldTransforms(const std::vector<DrawCmd*>& animatedDraws, const Camera::Frustum& frustum);
void TickAnimationFrame(double deltaTime, const std::vector<DrawCmd*>& animatedDraws, const Camera::Frustum& frustum);

} // namespace DeferredRendererAnimation
#endif