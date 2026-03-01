// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MODELINSTANCE_H
#define NDEVC_MODELINSTANCE_H

#include "Platform/NDEVcHeaders.h"
#include "Model.h"

class ModelInstance {
public:
    explicit ModelInstance(std::shared_ptr<Model> m) : model(m) {
        transform = glm::mat4(1.0f);
    }

    void setTransform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        glm::mat4 R = glm::mat4_cast(rot);
        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
        transform = T * R * S;
    }

    const glm::mat4& getTransform() const { return transform; }
    std::shared_ptr<Model> getModel() const { return model; }

private:
    std::shared_ptr<Model> model;
    glm::mat4 transform;
};

#endif //NDEVC_MODELINSTANCE_H