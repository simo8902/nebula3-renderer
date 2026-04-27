// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERGRAPH_H
#define NDEVC_RENDERGRAPH_H

#include <vector>
#include <memory>
#include <string>
#include "Rendering/IRenderPass.h"

struct RenderContext;
class RenderResourceRegistry;

namespace NDEVC::Graphics {
    class IGraphicsDevice;
    class IShaderManager;
}

class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Add a pass to the ordered execution list. Passes execute in insertion order.
    void AddPass(std::unique_ptr<IRenderPass> pass);

    // Initialise all registered passes (call once after all AddPass calls).
    void Init(NDEVC::Graphics::IGraphicsDevice* device,
              NDEVC::Graphics::IShaderManager* shaderManager,
              RenderResourceRegistry* resources);

    // Execute all passes in order, skipping those whose ShouldSkip() returns true.
    void ExecuteFrame(RenderContext& ctx);

    // Notify all passes of a viewport resize.
    void OnResize(int width, int height);

    // Retrieve a pass by name (returns nullptr if not found).
    IRenderPass* GetPass(const char* name) const;

private:
    std::vector<std::unique_ptr<IRenderPass>> passes_;
};

#endif
