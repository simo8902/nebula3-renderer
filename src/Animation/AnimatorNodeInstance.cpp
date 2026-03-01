// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Animation/AnimatorNodeInstance.h"
#include "Animation/AnimationSystem.h"
#include "Rendering/DeferredRendererAnimation.h"
#include <iostream>
#include <cmath>
#include <unordered_map>
#define GLM_ENABLE_EXPERIMENTAL
#include "gtx/euler_angles.hpp"
#include "gtx/matrix_decompose.hpp"

template<typename T>
bool SampleLinearKeyArray(const std::vector<AnimKey<T>>& keys, AnimLoopType loopType, float sampleTime, T& outValue) {
    if (keys.empty()) return false;
    if (keys.size() == 1) {
        outValue = keys[0].value;
        return true;
    }

    float t = sampleTime;
    const float minTime = keys.front().time;
    const float maxTime = keys.back().time;
    if (maxTime > 0.0f) {
        if (loopType == AnimLoopType::Loop) {
            t = t - (std::floor(t / maxTime) * maxTime);
        }
        if (t < minTime) t = minTime;
        else if (t >= maxTime) t = maxTime - 0.001f;
    }

    int idx0 = 0;
    int idx1 = static_cast<int>(keys.size()) - 1;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i + 1].time >= t) {
            idx0 = static_cast<int>(i);
            idx1 = static_cast<int>(i + 1);
            break;
        }
    }

    const float t0 = keys[idx0].time;
    const float t1 = keys[idx1].time;
    const float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    outValue = keys[idx0].value + (keys[idx1].value - keys[idx0].value) * alpha;
    return true;
}

bool SampleStepIntKeyArray(const std::vector<AnimKey<int32_t>>& keys, AnimLoopType loopType, float sampleTime, int32_t& outValue) {
    if (keys.empty()) return false;
    if (keys.size() == 1) {
        outValue = keys[0].value;
        return true;
    }

    float t = sampleTime;
    const float minTime = keys.front().time;
    const float maxTime = keys.back().time;
    if (maxTime > 0.0f) {
        if (loopType == AnimLoopType::Loop) {
            t = t - (std::floor(t / maxTime) * maxTime);
        }
        if (t < minTime) t = minTime;
        else if (t >= maxTime) t = maxTime - 0.001f;
    }

    int idx0 = static_cast<int>(keys.size()) - 1;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i + 1].time >= t) {
            idx0 = static_cast<int>(i);
            break;
        }
    }
    outValue = keys[idx0].value;
    return true;
}

void AnimatorNodeInstance::Setup(const Node* animatorNode,
                                   const std::unordered_map<std::string, Node*>& nodeMap,
                                   const void* owner) {
    animatorNode_ = animatorNode;
    owner_ = owner;
    if (!animatorNode_) return;

    animSections_.clear();
    animSections_.reserve(animatorNode_->animSections.size());

    std::unordered_multimap<std::string, Node*> leafMap;
    leafMap.reserve(nodeMap.size());
    for (const auto& [name, node] : nodeMap) {
        if (!node) continue;
        const std::string leaf = DeferredRendererAnimation::LeafNodeName(name);
        if (!leaf.empty()) {
            leafMap.emplace(leaf, node);
        }
    }

    for (size_t secIdx = 0; secIdx < animatorNode_->animSections.size(); ++secIdx) {
        const AnimSection& section = animatorNode_->animSections[secIdx];
        std::vector<AnimatedNode> sectionNodes;

        auto appendTarget = [&](const std::string& targetPath) {
            if (targetPath.empty()) return;

            Node* targetNode = nullptr;
            auto itFull = nodeMap.find(targetPath);
            if (itFull != nodeMap.end()) {
                targetNode = itFull->second;
            } else {
                const std::string targetLeaf = DeferredRendererAnimation::LeafNodeName(targetPath);
                if (targetLeaf.empty()) return;

                auto itLeaf = nodeMap.find(targetLeaf);
                if (itLeaf != nodeMap.end()) {
                    targetNode = itLeaf->second;
                } else {
                    auto range = leafMap.equal_range(targetLeaf);
                    if (range.first != range.second) {
                        auto next = range.first;
                        ++next;
                        if (next == range.second) {
                            targetNode = range.first->second;
                        } else {
                            std::cout << "[ANIMATOR] Warning: Ambiguous target leaf '" << targetLeaf
                                      << "' for path '" << targetPath << "'\n";
                        }
                    }
                }
            }

            if (!targetNode) {
                std::cout << "[ANIMATOR] Warning: Target node '" << targetPath << "' not found in nodeMap\n";
                return;
            }

            AnimatedNode animNode;
            animNode.targetNode = targetNode;
            animNode.shaderVarSemantic = section.shaderVarSemantic;
            sectionNodes.push_back(animNode);
        };

        if (section.animatedNodesPath.empty()) {
            appendTarget(animatorNode_->node_name);
        } else {
            for (const auto& animPath : section.animatedNodesPath) {
                appendTarget(animPath);
            }
        }

        animSections_.push_back(std::move(sectionNodes));
    }
}

void AnimatorNodeInstance::OnShow(double time) {
    startTime_ = time;
    Animate(time);
}

void AnimatorNodeInstance::Animate(double time) {
    if (!animatorNode_) return;

    double animTime = (overwrittenAnimTime_ >= 0.0) ? overwrittenAnimTime_ : (time - startTime_);
    if (animTime < 0.0) animTime = 0.0;

    for (size_t secIdx = 0; secIdx < animatorNode_->animSections.size() && secIdx < animSections_.size(); ++secIdx) {
        const AnimSection& section = animatorNode_->animSections[secIdx];
        const std::vector<AnimatedNode>& targetNodes = animSections_[secIdx];

        if (targetNodes.empty()) continue;

        AnimNodeType nodeType = section.animationNodeType;

        if (nodeType == FloatAnimator) {
            float sampledValue = 0.0f;
            if (!SampleLinearKeyArray(section.floatKeyArray, section.loopType, static_cast<float>(animTime), sampledValue)) continue;

            for (const AnimatedNode& animNode : targetNodes) {
                if (!animNode.targetNode || animNode.shaderVarSemantic.empty()) continue;
                animNode.targetNode->shader_params_float[animNode.shaderVarSemantic] = sampledValue;
            }
        }
        else if (nodeType == Float4Animator) {
            glm::vec4 sampledValue(0.0f);
            if (!SampleLinearKeyArray(section.float4KeyArray, section.loopType, static_cast<float>(animTime), sampledValue)) continue;

            for (const AnimatedNode& animNode : targetNodes) {
                if (!animNode.targetNode || animNode.shaderVarSemantic.empty()) continue;
                animNode.targetNode->shader_params_vec4[animNode.shaderVarSemantic] = sampledValue;
            }
        }
        else if (nodeType == IntAnimator) {
            int32_t sampledValue = 0;
            if (!SampleStepIntKeyArray(section.intKeyArray, section.loopType, static_cast<float>(animTime), sampledValue)) continue;

            for (const AnimatedNode& animNode : targetNodes) {
                if (!animNode.targetNode || animNode.shaderVarSemantic.empty()) continue;
                animNode.targetNode->shader_params_int[animNode.shaderVarSemantic] = sampledValue;
            }
        }
        else if (nodeType == TransformAnimator) {
            for (const AnimatedNode& animNode : targetNodes) {
                if (!animNode.targetNode) continue;

                AnimPose pose{};
                pose.pos = glm::vec3(animNode.targetNode->position);
                pose.rot = glm::normalize(glm::quat(animNode.targetNode->rotation.w,
                                                    animNode.targetNode->rotation.x,
                                                    animNode.targetNode->rotation.y,
                                                    animNode.targetNode->rotation.z));
                pose.scl = glm::vec3(animNode.targetNode->scale);

                glm::vec4 pos(0.0f), euler(0.0f), scl(1.0f);
                if (SampleLinearKeyArray(section.posArray, section.loopType, static_cast<float>(animTime), pos)) {
                    pose.pos = glm::vec3(pos);
                }
                if (SampleLinearKeyArray(section.eulerArray, section.loopType, static_cast<float>(animTime), euler)) {
                    const glm::mat4 rotM = glm::yawPitchRoll(euler.y, euler.x, euler.z);
                    pose.rot = glm::normalize(glm::quat_cast(rotM));
                }
                if (SampleLinearKeyArray(section.scaleArray, section.loopType, static_cast<float>(animTime), scl)) {
                    pose.scl = glm::max(glm::vec3(1e-6f), glm::vec3(scl));
                }

                SetAnimatedPose(owner_, animNode.targetNode->node_name, pose);
            }
        }
        else if (nodeType == TransformCurveAnimator) {
            for (const AnimatedNode& animNode : targetNodes) {
                if (!animNode.targetNode) continue;

                const std::string& targetName = animNode.targetNode->node_name;
                const glm::mat4 transform = MakeLocal(owner_, targetName);

                glm::vec3 scale, skew, translation;
                glm::vec4 perspective;
                glm::quat rotation;
                if (glm::decompose(transform, scale, rotation, translation, skew, perspective)) {
                    AnimPose pose;
                    pose.pos = translation;
                    pose.rot = glm::normalize(rotation);
                    pose.scl = glm::max(glm::vec3(1e-6f), scale);
                    SetAnimatedPose(owner_, targetName, pose);
                }
            }
        }
        else if (nodeType == UvAnimator) {
            for (const AnimatedNode& animNode : targetNodes) {
                if (!animNode.targetNode) continue;

                glm::mat4 uvTransform = glm::mat4(1.0f);
                glm::vec4 pos(0.0f), euler(0.0f), scl(1.0f);

                if (SampleLinearKeyArray(section.posArray, section.loopType, static_cast<float>(animTime), pos)) {
                    uvTransform = glm::translate(uvTransform, glm::vec3(pos.x, pos.y, 0.0f));
                }
                if (SampleLinearKeyArray(section.eulerArray, section.loopType, static_cast<float>(animTime), euler)) {
                    uvTransform = glm::rotate(uvTransform, euler.x, glm::vec3(0.0f, 0.0f, 1.0f));
                }
                if (SampleLinearKeyArray(section.scaleArray, section.loopType, static_cast<float>(animTime), scl)) {
                    uvTransform = glm::scale(uvTransform, glm::vec3(scl.x, scl.y, 1.0f));
                }

                animNode.targetNode->shader_params_vec4["TextureTransform0_Row0"] = uvTransform[0];
                animNode.targetNode->shader_params_vec4["TextureTransform0_Row1"] = uvTransform[1];
                animNode.targetNode->shader_params_vec4["TextureTransform0_Row2"] = uvTransform[2];
                animNode.targetNode->shader_params_vec4["TextureTransform0_Row3"] = uvTransform[3];
            }
        }
    }
}
