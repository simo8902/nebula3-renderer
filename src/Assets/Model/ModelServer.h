// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.
//
// Nebula parity: Singleton model cache with managed load states.
// Maps to Nebula's loaderserver / managed resource subsystem.
// Provides readiness queries consumed by SceneManager's lifecycle gate.

#ifndef NDEVC_MODELSERVER_H
#define NDEVC_MODELSERVER_H

#include "ModelInstance.h"
#include "Platform/NDEVcHeaders.h"

class ModelServer {
public:
    // ── Managed model load states (Nebula parity) ──────────────────────
    // Maps to Nebula's resource loading lifecycle:
    //   Initial → Loading → Loaded | Failed
    enum class ModelLoadState : int {
        Initial  = 0,  // Not yet requested
        Loading  = 1,  // Load in progress (currently synchronous)
        Loaded   = 2,  // Successfully loaded and cached
        Failed   = 3,  // Load attempted and failed
        Count
    };

    static const char* ModelLoadStateName(ModelLoadState s) {
        static const char* names[] = { "Initial", "Loading", "Loaded", "Failed" };
        const int idx = static_cast<int>(s);
        if (idx >= 0 && idx < static_cast<int>(ModelLoadState::Count))
            return names[idx];
        return "Unknown";
    }

    static ModelServer& instance() {
        static ModelServer srv;
        return srv;
    }

    std::shared_ptr<Model> loadModel(const std::string& filepath, Reporter& rep, const Options& opt) {
        auto it = modelCache.find(filepath);
        if (it != modelCache.end()) return it->second;

        modelStates_[filepath] = ModelLoadState::Loading;

        auto model = std::make_shared<Model>();
        if (!model->loadFromFile(filepath, rep, opt)) {
            std::cerr << "[MODEL][ERROR] Failed to load '" << filepath << "'\n";
            modelStates_[filepath] = ModelLoadState::Failed;
            return nullptr;
        }

        modelCache[filepath] = model;
        modelStates_[filepath] = ModelLoadState::Loaded;
        return model;
    }

    std::shared_ptr<ModelInstance> createInstance(std::shared_ptr<Model> model) {
        return std::make_shared<ModelInstance>(model);
    }

    // ── Readiness query API ────────────────────────────────────────────
    ModelLoadState GetModelState(const std::string& filepath) const {
        auto it = modelStates_.find(filepath);
        return (it != modelStates_.end()) ? it->second : ModelLoadState::Initial;
    }

    bool IsModelLoaded(const std::string& filepath) const {
        return GetModelState(filepath) == ModelLoadState::Loaded;
    }

    bool IsModelFailed(const std::string& filepath) const {
        return GetModelState(filepath) == ModelLoadState::Failed;
    }

    bool IsModelRenderReady(const std::string& filepath) const {
        return IsModelLoaded(filepath) && modelCache.count(filepath) > 0;
    }

    int CountModelsInState(ModelLoadState s) const {
        int n = 0;
        for (const auto& [_, state] : modelStates_) {
            if (state == s) ++n;
        }
        return n;
    }

    void clearCache() {
        modelCache.clear();
        modelStates_.clear();
    }

private:
    ModelServer() = default;
    std::unordered_map<std::string, std::shared_ptr<Model>> modelCache;
    std::unordered_map<std::string, ModelLoadState> modelStates_;
};

#endif //NDEVC_MODELSERVER_H
