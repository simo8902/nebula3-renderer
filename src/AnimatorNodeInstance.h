// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ANIMATORNODEINSTANCE_H
#define NDEVC_ANIMATORNODEINSTANCE_H

#include "NDEVcStructure.h"
#include "glm.hpp"
#include <vector>
#include <memory>
#include <unordered_map>

class ModelInstance;

struct AnimatedNode {
    Node* targetNode = nullptr;
    std::string shaderVarSemantic;
};

class AnimatorNodeInstance {
public:
    AnimatorNodeInstance() = default;
    ~AnimatorNodeInstance() = default;

    void Setup(const Node* animatorNode,
               const std::unordered_map<std::string, Node*>& nodeMap,
               const void* owner);

    void OnShow(double time);

    void Animate(double time);
    const void* GetOwner() const { return owner_; }

    void OverwriteAnimTime(double time) { overwrittenAnimTime_ = time; }
    void ClearOverwriteAnimTime() { overwrittenAnimTime_ = -1.0; }

private:
    const Node* animatorNode_ = nullptr;
    const void* owner_ = nullptr;
    std::vector<std::vector<AnimatedNode>> animSections_;

    double startTime_ = -1.0;
    double overwrittenAnimTime_ = -1.0;
};
#endif