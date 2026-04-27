// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"
#include "Rendering/RenderGraph.h"
#include "Core/Logger.h"

#include "glad/glad.h"

#include <exception>

void DeferredRenderer::ExecuteModernRenderPasses(bool shouldExecuteRenderPasses,
                                                  std::function<void()> onDrawlistsReady) {
    if (!shouldExecuteRenderPasses) return;

    RenderContext ctx = BuildRenderContext();
    ctx.onDrawlistsReady = std::move(onDrawlistsReady);
    try {
        renderGraph_->ExecuteFrame(ctx);
    } catch (const std::exception& e) {
        NC::LOGGING::Error(NC::LOGGING::Category::Graphics, "RenderGraph execution failed: ", e.what());
        throw;
    } catch (...) {
        NC::LOGGING::Error(NC::LOGGING::Category::Graphics, "RenderGraph execution failed with unknown exception");
        throw;
    }

    // Safety net: fire the signal even if no pass called PrepareDrawLists
    // (e.g. render graph variant without a GBuffer pass).
    if (ctx.onDrawlistsReady) { ctx.onDrawlistsReady(); }

    ApplyRenderContextResults(ctx);
}
