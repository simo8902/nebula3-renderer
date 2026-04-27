// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_STANDARDGBUFFERPASS_H
#define NDEVC_STANDARDGBUFFERPASS_H

#include "Rendering/IRenderPass.h"
#include "Rendering/DrawCmd.h"
#include "Rendering/MaterialGPU.h"
#include "Rendering/OpenGL/GLHandles.h"
#include "glm.hpp"

#include <array>
#include <memory>
#include <vector>

namespace NDEVC::Graphics {
class IShader;
}

class StandardGBufferPass final : public IRenderPass {
public:
    const char* GetName() const override { return "StandardGBuffer"; }
    void Init(NDEVC::Graphics::IGraphicsDevice* device,
              NDEVC::Graphics::IShaderManager* shaderManager,
              RenderResourceRegistry* resources) override;
    void Execute(RenderContext& ctx) override;
    bool ShouldSkip(const RenderContext& ctx) const override;
    void OnResize(int width, int height) override;

private:
    std::shared_ptr<NDEVC::Graphics::IShader> solidShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> alphaClipShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> simpleLayerShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> simpleLayerAlphaClipShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> lightAccumShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> composeShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> litCompositeShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> litCompositeAlphaTestShader_;
    std::shared_ptr<NDEVC::Graphics::IShader> presentShader_;

    NDEVC::GL::GLBufHandle vsRegisters_;
    NDEVC::GL::GLBufHandle psRegisters_;
    NDEVC::GL::GLBufHandle materialBuffer_;
    NDEVC::GL::GLBufHandle directMatrixBuffer_;
    NDEVC::GL::GLBufHandle directMaterialIndexBuffer_;

    std::vector<DrawCmd> solidDraws_;
    std::vector<DrawCmd> alphaTestDraws_;
    std::vector<DrawCmd> decalDraws_;
    std::vector<DrawCmd> environmentDraws_;
    std::vector<DrawCmd> environmentAlphaDraws_;
    std::vector<DrawCmd> simpleLayerDraws_;
    std::vector<DrawCmd> refractionDraws_;
    std::vector<DrawCmd> postAlphaUnlitDraws_;
    std::vector<DrawCmd> waterDraws_;
    std::vector<MaterialGPU> materials_;
    std::vector<glm::mat4> directMatrices_;
    std::vector<uint32_t> directMaterialIndices_;
    bool loggedInit_ = false;
    mutable bool loggedSkip_ = false;
    bool loggedFrame_ = false;
    bool loggedDirectDraw_ = false;

    void EnsureBuffers();
    void UploadVSRegisters(const RenderContext& ctx);
    void UploadPSRegisters(float depthScale, float alphaClipRef);
    void UploadDirectDrawBuffers();
    void UploadMaterials(RenderContext& ctx, std::vector<DrawCmd>& draws);
    MaterialGPU BuildMaterial(RenderContext& ctx, const DrawCmd& draw) const;
    void RenderDraws(RenderContext& ctx, std::vector<DrawCmd>& draws, NDEVC::Graphics::IShader* shader, const char* label);
    void RenderDrawsWithoutMaterialUpload(RenderContext& ctx, std::vector<DrawCmd>& draws, NDEVC::Graphics::IShader* shader, const char* label);
    void RenderLightAccumulation(RenderContext& ctx, float depthScale);
    void ComposePackedGBuffer(RenderContext& ctx, float depthScale);
    void RenderStandardLighting(RenderContext& ctx, float depthScale);
    void PresentLitColor(RenderContext& ctx);
};

#endif
