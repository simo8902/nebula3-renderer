// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"
#include "Rendering/OpenGL/OpenGLDevice.h"

#include "Assets/Map/MapHeader.h"
#include "Core/Logger.h"

#include "Rendering/RenderGraph.h"
#include "Rendering/RenderResourceRegistry.h"
#include "Rendering/StandardGBufferPass.h"

void DeferredRenderer::PrepareSceneForRenderPassSetup()
{
    if (!scenePrepared) {
        if (!scene_->currentMap) {
            NC::LOGGING::Error(NC::LOGGING::Category::Graphics, "Map load FAILED (scene_->currentMap is null)");
        } else {
            if (optRenderLOG) {
                NC::LOGGING::Debug(NC::LOGGING::Category::Graphics, "Map loaded: ", scene_->currentMap->instances.size(), " instances");
            }
            scene_->LoadMapInstances(scene_->currentMap);
        }
        scenePrepared = true;
    }
}

void DeferredRenderer::SetupModernRenderGraph()
{
    using namespace NDEVC::Graphics;

    renderResources_ = std::make_unique<RenderResourceRegistry>(device_.get(), width, height);
    renderGraph_ = std::make_unique<RenderGraph>();
    renderGraph_->AddPass(std::make_unique<StandardGBufferPass>());
    renderGraph_->Init(device_.get(), shaderManager.get(), renderResources_.get());

    renderResources_->Build();

}

void DeferredRenderer::CreateCachedRenderStatesForPasses()
{
    if (optRenderLOG) NC::LOGGING::Info(NC::LOGGING::Category::Graphics, "Frame graphs compiled successfully");

    if (device_) {
        NDEVC::Graphics::RenderStateDesc blitStateDesc;
        blitStateDesc.depth.depthTest = false;
        blitStateDesc.blend.blendEnable = false;
        cachedBlitState_ = device_->CreateRenderState(blitStateDesc);
    }
}

void DeferredRenderer::setupRenderPasses()
{
    PrepareSceneForRenderPassSetup();
    SetupModernRenderGraph();
    CreateCachedRenderStatesForPasses();
}