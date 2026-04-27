// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/RenderGraph.h"
#include "Rendering/RenderContext.h"
#include <cstring>

#include "glad/glad.h"

void RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass)
{
    passes_.push_back(std::move(pass));
}

void RenderGraph::Init(NDEVC::Graphics::IGraphicsDevice* device,
                       NDEVC::Graphics::IShaderManager* shaderManager,
                       RenderResourceRegistry* resources)
{
    for (auto& pass : passes_)
        pass->Init(device, shaderManager, resources);
}

void RenderGraph::ExecuteFrame(RenderContext& ctx)
{
    for (auto& pass : passes_) {
        if (pass->ShouldSkip(ctx)) continue;
        
        const char* passName = pass->GetName();
        if (passName) {
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, passName);
        }
        
        pass->Execute(ctx);
        
        if (passName) {
            glPopDebugGroup();
        }
    }
}

void RenderGraph::OnResize(int width, int height)
{
    for (auto& pass : passes_)
        pass->OnResize(width, height);
}

IRenderPass* RenderGraph::GetPass(const char* name) const
{
    for (const auto& pass : passes_) {
        if (pass && std::strcmp(pass->GetName(), name) == 0)
            return pass.get();
    }
    return nullptr;
}
