// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MODELSERVER_H
#define NDEVC_MODELSERVER_H

#include "ModelInstance.h"
#include "Platform/NDEVcHeaders.h"

class ModelServer {
public:
    static ModelServer& instance() {
        static ModelServer srv;
        return srv;
    }

    std::shared_ptr<Model> loadModel(const std::string& filepath, Reporter& rep, const Options& opt) {
        auto it = modelCache.find(filepath);
        if (it != modelCache.end()) return it->second;

        auto model = std::make_shared<Model>();
        if (!model->loadFromFile(filepath, rep, opt)) {
            std::cerr << "[MODEL][ERROR] Failed to load '" << filepath << "'\n";
            return nullptr;
        }

        modelCache[filepath] = model;
        return model;
    }

    std::shared_ptr<ModelInstance> createInstance(std::shared_ptr<Model> model) {
        return std::make_shared<ModelInstance>(model);
    }

    void clearCache() { modelCache.clear(); }

private:
    ModelServer() = default;
    std::unordered_map<std::string, std::shared_ptr<Model>> modelCache;
};

#endif //NDEVC_MODELSERVER_H
