// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ANIMATION_SYSTEM_H
#define NDEVC_ANIMATION_SYSTEM_H

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include "glm.hpp"
#include "gtc/quaternion.hpp"
#include "gtc/matrix_transform.hpp"

struct Node;

struct AnimPose {
    glm::vec3 pos;
    glm::quat rot;
    glm::vec3 scl;
};

struct ClipInstance {
    std::string source;
    std::string node;
    std::string clipKeyNode;
    const void* owner = nullptr;
    float t = 0;
    float dur = 0;
    float tps = 30;
    float ticks = 0;
    float speed = 1.0f;
    float blendWeight = 1.0f;
    float fadeInTime = 0.0f;
    float fadeOutTime = 0.0f;
    int trackIndex = 0;
    uint64_t serial = 0;
    bool loop = true;
    bool active = false;
};

extern float gAnimTimeScale;
extern std::unordered_map<std::string, AnimPose> gStaticPose;
extern std::unordered_map<std::string, AnimPose> gAnimPose;
extern std::vector<ClipInstance> gClips;
extern std::unordered_map<std::string, std::vector<std::string>> gCharacterJoints;

struct AnimEventInfoLite {
    std::string source;
    std::string node;
    std::string clip;
    std::string name;
    std::string category;
};
extern std::vector<AnimEventInfoLite> gFrameAnimEvents;
using AnimEventCallbackFn = void(*)(const AnimEventInfoLite&);

using Nax3SampleFn = bool(*)(const std::string& source, float t, const std::string& node, const void* owner, int joint_index, AnimPose& out, float& outDur, float& outTicksPerSec);

void SetNax3Provider(Nax3SampleFn fn);
void SetAnimEventCallback(AnimEventCallbackFn fn);
void InstallNax3Provider();
void StopClip(const std::string& source, const std::string& node, const void* owner = nullptr);
void PlayClip(const std::string& source, const std::string& node, int clipIndex, bool loop, const void* owner = nullptr);
void PlayClip(const std::string& source, const std::string& node, const std::string& clipName, bool loop, const void* owner = nullptr);
void PlayClipAdvanced(const std::string& source, const std::string& node, int clipIndex, bool loop, int trackIndex, float blendWeight, float fadeInTime, float fadeOutTime, const void* owner = nullptr);
void PlayClipAdvanced(const std::string& source, const std::string& node, const std::string& clipName, bool loop, int trackIndex, float blendWeight, float fadeInTime, float fadeOutTime, const void* owner = nullptr);
void RegisterAnimationOwnerNodes(const void* owner, const std::unordered_map<std::string, Node*>& nodeMap);
void ClearAnimationOwnerData();
void ClearAnimationOwnerData(const void* owner);
void UpdateAnimations(float dt);

glm::mat4 MakeLocal(const std::string& nodeName);
glm::mat4 MakeLocal(const void* owner, const std::string& nodeName);
void SetAnimatedPose(const void* owner, const std::string& nodeName, const AnimPose& pose);
bool HasAnimatedPose(const void* owner, const std::string& nodeName);
#endif