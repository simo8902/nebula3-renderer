// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.
//
// Nebula parity: Per-instance validity gate and deferred update queue.
// Maps to Nebula's internalmodelentity async readiness / deferred message replay.

#ifndef NDEVC_MODELINSTANCE_H
#define NDEVC_MODELINSTANCE_H

#include "Platform/NDEVcHeaders.h"
#include "Model.h"
#include <vector>

class ModelInstance {
public:
    // ── Deferred update payloads (Nebula parity) ───────────────────────
    // Queued while the instance is not yet render-valid and replayed on
    // readiness transition (≡ internalmodelentity deferred message replay).
    struct DeferredUpdate {
        enum class Type : int { Transform, Visibility };
        Type      type;
        glm::mat4 transformMatrix{1.0f};  // Transform payload
        bool      visible = true;          // Visibility payload
    };

    explicit ModelInstance(std::shared_ptr<Model> m) : model(m) {
        transform = glm::mat4(1.0f);
    }

    void setTransform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        glm::mat4 R = glm::mat4_cast(rot);
        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
        transform = T * R * S;
        if (!validForRender_) {
            DeferredUpdate u;
            u.type = DeferredUpdate::Type::Transform;
            u.transformMatrix = transform;
            deferredUpdates_.push_back(u);
        }
    }

    void setVisible(bool v) {
        visible_ = v;
        if (!validForRender_) {
            DeferredUpdate u;
            u.type = DeferredUpdate::Type::Visibility;
            u.visible = v;
            deferredUpdates_.push_back(u);
        }
    }

    // ── Validity gate ──────────────────────────────────────────────────
    bool isValidForRender() const { return validForRender_; }
    bool isVisible() const { return visible_; }

    // Transitions to valid and returns queued updates for the caller to act on.
    // Queue is consumed exactly once; subsequent calls return empty.
    std::vector<DeferredUpdate> markValidAndReplay() {
        validForRender_ = true;
        std::vector<DeferredUpdate> pending;
        pending.swap(deferredUpdates_);
        return pending;
    }

    void markInvalid() {
        validForRender_ = false;
    }

    bool hasDeferredUpdates() const { return !deferredUpdates_.empty(); }
    size_t deferredUpdateCount() const { return deferredUpdates_.size(); }

    const glm::mat4& getTransform() const { return transform; }
    std::shared_ptr<Model> getModel() const { return model; }

private:
    std::shared_ptr<Model> model;
    glm::mat4 transform;
    bool validForRender_ = false;
    bool visible_ = true;
    std::vector<DeferredUpdate> deferredUpdates_;
};

#endif //NDEVC_MODELINSTANCE_H