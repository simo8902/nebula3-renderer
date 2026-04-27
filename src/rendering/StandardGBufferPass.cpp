// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/StandardGBufferPass.h"

#include "Assets/Servers/TextureServer.h"
#include "Core/Logger.h"
#include "Rendering/DrawBatchSystem.h"
#include "Rendering/Interfaces/IGraphicsDevice.h"
#include "Rendering/Interfaces/IShader.h"
#include "Rendering/Interfaces/IShaderManager.h"
#include "Rendering/Interfaces/ITexture.h"
#include "Rendering/MegaBuffer.h"
#include "Rendering/RenderContext.h"
#include "Rendering/RenderResourceRegistry.h"
#include "glad/glad.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ios>

namespace {

bool UseDirectStandardDraws()
{
    static const bool enabled = []() {
        const char* value = std::getenv("NDEVC_DIRECT_STANDARD_DRAWS");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

glm::vec4 Row(const glm::mat4& m, int r)
{
    return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]);
}

void StoreRows(std::array<glm::vec4, 228>& regs, int first, const glm::mat4& m, int count)
{
    for (int i = 0; i < count; ++i) {
        regs[static_cast<size_t>(first + i)] = Row(m, i);
    }
}

GLuint TextureHandle(const NDEVC::Graphics::ITexture* texture, GLuint fallback)
{
    if (!texture) return fallback;
    void* native = texture->GetNativeHandle();
    if (!native) return fallback;
    const GLuint handle = *reinterpret_cast<const GLuint*>(native);
    return handle != 0 ? handle : fallback;
}

uint64_t BindlessHandle(const NDEVC::Graphics::ITexture* texture, GLuint fallback)
{
    const GLuint handle = TextureHandle(texture, fallback);
    if (texture) {
        const uint64_t existing = texture->GetBindlessHandle();
        if (existing != 0) return existing;
    }
    return TextureServer::instance().getOrCreateBindlessHandle(handle);
}

float AlphaRefFromCutoff(float cutoff)
{
    if (!std::isfinite(cutoff)) return 128.0f;
    return std::clamp(cutoff * 256.0f, 0.0f, 255.0f);
}

} // namespace

void StandardGBufferPass::Init(NDEVC::Graphics::IGraphicsDevice*,
                               NDEVC::Graphics::IShaderManager* shaderManager,
                               RenderResourceRegistry* resources)
{
    if (resources) {
        resources->DeclareTexture("standardPackedGBuffer", NDEVC::Graphics::Format::RGBA8_UNORM);
        resources->DeclareTexture("standardAlbedo", NDEVC::Graphics::Format::RGBA8_UNORM);
        resources->DeclareTexture("standardLightBuffer", NDEVC::Graphics::Format::RGBA16F);
        resources->DeclareTexture("standardLitColor", NDEVC::Graphics::Format::RGBA8_UNORM);
        resources->DeclareTexture("standardDepth", NDEVC::Graphics::Format::D32_FLOAT_S8_UINT, true);
        resources->DeclareFramebuffer("standardGBuffer", {"standardPackedGBuffer", "standardAlbedo"}, "standardDepth");
        resources->DeclareFramebuffer("standardLight", {"standardLightBuffer"});
        resources->DeclareFramebuffer("standardLit", {"standardLitColor"}, "standardDepth");
    }

    if (shaderManager) {
        solidShader_ = shaderManager->GetShader("standard_bump_gbuffer");
        alphaClipShader_ = shaderManager->GetShader("standard_bump_gbuffer_alpha_test");
        if (!solidShader_) {
            solidShader_ = shaderManager->GetShader("NDEVCdeferred");
        }
        if (!alphaClipShader_) {
            alphaClipShader_ = shaderManager->GetShader("NDEVCdeferred_alpha_clip");
        }
        if (!alphaClipShader_) {
            alphaClipShader_ = solidShader_;
        }

        simpleLayerShader_ = shaderManager->GetShader("NDEVCsimplelayer");
        if (!simpleLayerShader_) {
            simpleLayerShader_ = solidShader_;
        }
        simpleLayerAlphaClipShader_ = shaderManager->GetShader("NDEVCsimplelayer_alpha_clip");
        if (!simpleLayerAlphaClipShader_) {
            simpleLayerAlphaClipShader_ = simpleLayerShader_;
        }

        lightAccumShader_ = shaderManager->GetShader("standard_light_accum");
        composeShader_ = shaderManager->GetShader("standard_gbuffer_compose");
        litCompositeShader_ = shaderManager->GetShader("standard_lit_composite");
        litCompositeAlphaTestShader_ = shaderManager->GetShader("standard_lit_composite_alpha_test");
        if (!litCompositeAlphaTestShader_) {
            litCompositeAlphaTestShader_ = litCompositeShader_;
        }
        presentShader_ = shaderManager->GetShader("compose");
        if (!presentShader_) {
            presentShader_ = shaderManager->GetShader("standard_present");
        }
    }

    EnsureBuffers();

    if (!loggedInit_) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                          "[STD_GBUFFER] init solid=", solidShader_ ? 1 : 0,
                          " alphaClip=", alphaClipShader_ ? 1 : 0,
                          " lightAccum=", lightAccumShader_ ? 1 : 0,
                          " compose=", composeShader_ ? 1 : 0,
                          " litComposite=", litCompositeShader_ ? 1 : 0,
                          " present=", presentShader_ ? 1 : 0,
                          " bindless=", TextureServer::sBindlessSupported ? 1 : 0);
        loggedInit_ = true;
    }
}

bool StandardGBufferPass::ShouldSkip(const RenderContext& ctx) const
{
    const bool skip = ctx.scene == nullptr || ctx.camera == nullptr || ctx.resources == nullptr ||
                      !solidShader_ || !lightAccumShader_ || !composeShader_ || !presentShader_ ||
                      !TextureServer::sBindlessSupported;
    if (skip && !loggedSkip_) {
        NC::LOGGING::Warning("[STD_GBUFFER] skipped scene=", ctx.scene ? 1 : 0,
                             " camera=", ctx.camera ? 1 : 0,
                             " resources=", ctx.resources ? 1 : 0,
                             " solidShader=", solidShader_ ? 1 : 0,
                             " lightAccum=", lightAccumShader_ ? 1 : 0,
                             " composeShader=", composeShader_ ? 1 : 0,
                             " present=", presentShader_ ? 1 : 0,
                             " bindless=", TextureServer::sBindlessSupported ? 1 : 0);
        loggedSkip_ = true;
    }
    return skip;
}

void StandardGBufferPass::OnResize(int, int)
{
}

void StandardGBufferPass::EnsureBuffers()
{
    if (!vsRegisters_) glCreateBuffers(1, vsRegisters_.put());
    if (!psRegisters_) glCreateBuffers(1, psRegisters_.put());
    if (!materialBuffer_) glCreateBuffers(1, materialBuffer_.put());
    if (!directMatrixBuffer_) glCreateBuffers(1, directMatrixBuffer_.put());
    if (!directMaterialIndexBuffer_) glCreateBuffers(1, directMaterialIndexBuffer_.put());
}

void StandardGBufferPass::UploadVSRegisters(const RenderContext& ctx)
{
    std::array<glm::vec4, 228> regs{};
    StoreRows(regs, 0, ctx.viewProjection, 4);
    StoreRows(regs, 4, ctx.view, 3);
    
    // AAA Strategy: Correct Normal Matrix (Inverse-Transpose of View)
    glm::mat4 invView = glm::transpose(glm::inverse(ctx.view));
    StoreRows(regs, 7, invView, 3);

    glNamedBufferData(vsRegisters_, static_cast<GLsizeiptr>(sizeof(regs)), regs.data(), GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, vsRegisters_);
}

void StandardGBufferPass::UploadPSRegisters(float depthScale, float alphaClipRef)
{
    std::array<glm::vec4, 4> regs{};
    regs[0].x = depthScale;
    regs[1].x = alphaClipRef;
    glNamedBufferData(psRegisters_, static_cast<GLsizeiptr>(sizeof(regs)), regs.data(), GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, psRegisters_);
}

void StandardGBufferPass::UploadDirectDrawBuffers()
{
    if (directMatrices_.empty() || directMaterialIndices_.empty()) return;

    glNamedBufferData(directMatrixBuffer_,
                      static_cast<GLsizeiptr>(directMatrices_.size() * sizeof(glm::mat4)),
                      directMatrices_.data(),
                      GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, directMatrixBuffer_);

    glNamedBufferData(directMaterialIndexBuffer_,
                      static_cast<GLsizeiptr>(directMaterialIndices_.size() * sizeof(uint32_t)),
                      directMaterialIndices_.data(),
                      GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, directMaterialIndexBuffer_);
}

MaterialGPU StandardGBufferPass::BuildMaterial(RenderContext& ctx, const DrawCmd& draw) const
{
    MaterialGPU material{};
    material.diffuseHandle = BindlessHandle(draw.tex[0], ctx.whiteTex);
    material.specularHandle = BindlessHandle(draw.tex[1], ctx.blackTex);
    material.normalHandle = BindlessHandle(draw.tex[2], ctx.normalTex);

    material.emissiveHandle = BindlessHandle(draw.tex[3], ctx.blackTex);
    material.emissiveIntensity = draw.cachedMatEmissiveIntensity;
    material.specularIntensity = draw.cachedMatSpecularIntensity;
    material.specularPower = draw.cachedMatSpecularPower;
    material.alphaCutoff = AlphaRefFromCutoff(draw.alphaCutoff);
    material.flags = draw.alphaTest ? MATFLAG_ALPHA_TEST : 0u;
    if (draw.cachedTwoSided != 0) material.flags |= MATFLAG_TWO_SIDED;
    if (draw.cachedIsFlatNormal != 0) material.flags |= MATFLAG_FLAT_NORMAL;
    if (draw.receivesDecals) material.flags |= MATFLAG_RECEIVES_DECALS;
    if (draw.cachedIsAdditive) material.flags |= MATFLAG_ADDITIVE;
    if (draw.cachedHasSpecMap) material.flags |= MATFLAG_HAS_SPEC_MAP;
    material.bumpScale = draw.cachedBumpScale;
    material.intensity0 = draw.cachedIntensity0;
    material.alphaBlendFactor = draw.cachedAlphaBlendFactor;
    material.diffMap1Handle = BindlessHandle(draw.tex[4], ctx.whiteTex);
    material.specMap1Handle = BindlessHandle(draw.tex[5], ctx.blackTex);
    material.bumpMap1Handle = BindlessHandle(draw.tex[6], ctx.normalTex);
    material.maskMapHandle = BindlessHandle(draw.tex[7], ctx.whiteTex);
    material.alphaMapHandle = BindlessHandle(draw.tex[8], ctx.whiteTex);
    material.cubeMapHandle = BindlessHandle(draw.tex[9], ctx.blackCubeTex);
    material.velocityX = draw.cachedVelocity.x;
    material.velocityY = draw.cachedVelocity.y;
    material.scale = draw.cachedScale;
    material.mayaAnimableAlpha = draw.cachedMayaAnimableAlpha;
    material.encodefactor = draw.cachedEncodefactor;
    material.customColor2[0] = draw.cachedCustomColor2.x;
    material.customColor2[1] = draw.cachedCustomColor2.y;
    material.customColor2[2] = draw.cachedCustomColor2.z;
    material.customColor2[3] = draw.cachedCustomColor2.w;
    material.tintingColour[0] = draw.cachedTintingColour.x;
    material.tintingColour[1] = draw.cachedTintingColour.y;
    material.tintingColour[2] = draw.cachedTintingColour.z;
    material.tintingColour[3] = draw.cachedTintingColour.w;
    material.highlightColor[0] = draw.cachedHighlightColor.x;
    material.highlightColor[1] = draw.cachedHighlightColor.y;
    material.highlightColor[2] = draw.cachedHighlightColor.z;
    material.highlightColor[3] = draw.cachedHighlightColor.w;
    material.luminance[0] = draw.cachedLuminance.x;
    material.luminance[1] = draw.cachedLuminance.y;
    material.luminance[2] = draw.cachedLuminance.z;
    material.luminance[3] = draw.cachedLuminance.w;
    return material;
}

void StandardGBufferPass::UploadMaterials(RenderContext& ctx, std::vector<DrawCmd>& draws)
{
    materials_.clear();
    materials_.reserve(draws.size());
    for (DrawCmd& draw : draws) {
        draw.gpuMaterialIndex = static_cast<uint32_t>(materials_.size());
        materials_.push_back(BuildMaterial(ctx, draw));
    }

    if (materials_.empty()) return;
    const GLsizeiptr byteSize = static_cast<GLsizeiptr>(materials_.size() * sizeof(MaterialGPU));
    glNamedBufferData(materialBuffer_, byteSize, materials_.data(), GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialBuffer_);
}

void StandardGBufferPass::RenderDrawsWithoutMaterialUpload(RenderContext& ctx,
                                                           std::vector<DrawCmd>& draws,
                                                           NDEVC::Graphics::IShader* shader,
                                                           const char* label)
{
    if (draws.empty() || !shader) return;

    using ProfileClock = std::chrono::high_resolution_clock;
    const auto start = ProfileClock::now();
    auto last = start;
    auto lapMs = [&]() -> double {
        const auto now = ProfileClock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        return ms;
    };

    shader->Use();
    const GLuint program = shader->GetNativeHandle()
        ? *reinterpret_cast<GLuint*>(shader->GetNativeHandle())
        : 0u;
    const double shaderUseMs = lapMs();

    if (UseDirectStandardDraws()) {
        // AAA Strategy: Ensure shader data parity in direct path (Matrices + Material Indices)
        directMatrices_.clear();
        directMaterialIndices_.clear();
        for (const auto& draw : draws) {
            if (!draw.mesh || draw.disabled) continue;
            directMatrices_.push_back(draw.worldMatrix);
            directMaterialIndices_.push_back(draw.gpuMaterialIndex);
        }
        
        if (!directMatrices_.empty()) {
            glNamedBufferData(directMatrixBuffer_, 
                             static_cast<GLsizeiptr>(directMatrices_.size() * sizeof(glm::mat4)), 
                             directMatrices_.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, directMatrixBuffer_);

            glNamedBufferData(directMaterialIndexBuffer_,
                             static_cast<GLsizeiptr>(directMaterialIndices_.size() * sizeof(uint32_t)),
                             directMaterialIndices_.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, directMaterialIndexBuffer_);
        }

        MegaBuffer::instance().bind();
        uint32_t currentMatrixIdx = 0;
        size_t commands = 0;
        for (const DrawCmd& draw : draws) {
            if (!draw.mesh || draw.disabled || draw.gpuMaterialIndex == UINT32_MAX) continue;

            auto drawGroup = [&](const Nvx2Group& group) {
                if (group.indexCount() == 0) return;
                const uint64_t first = static_cast<uint64_t>(group.firstIndex());
                const uint64_t count = static_cast<uint64_t>(group.indexCount());
                const uint64_t total = static_cast<uint64_t>(draw.mesh->idx.size());
                if (first >= total || first + count > total) return;

                glDrawElementsInstancedBaseVertexBaseInstance(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(group.indexCount()),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(
                        (draw.megaIndexOffset + group.firstIndex()) * sizeof(uint32_t))),
                    1,
                    0,
                    currentMatrixIdx);
                ++commands;
            };

            if (draw.group >= 0 && draw.group < static_cast<int>(draw.mesh->groups.size())) {
                drawGroup(draw.mesh->groups[draw.group]);
            } else {
                for (const auto& group : draw.mesh->groups) {
                    drawGroup(group);
                }
            }
            currentMatrixIdx++;
        }

        if (ctx.frameDrawCalls) {
            *ctx.frameDrawCalls += static_cast<int>(commands);
        }

        glBindVertexArray(0);
        const double drawMs = lapMs();
        const double totalMs = std::chrono::duration<double, std::milli>(
            ProfileClock::now() - start).count();
        if (totalMs > 4.0) {
            NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                              "[FRAME_TRACE][DIRECT_DRAW_SLOW] label=", label ? label : "?",
                              " totalMs=", totalMs,
                              " shaderUseMs=", shaderUseMs,
                              " drawMs=", drawMs,
                              " inputDraws=", draws.size(),
                              " commands=", commands);
        }
        return;
    }

    DrawBatchSystem& batches = DrawBatchSystem::instance();
    batches.reset(true);
    const double resetMs = lapMs();
    batches.cullGeneric(draws, program, 4);
    const double cullGenericMs = lapMs();
    batches.flush(ctx.samplerRepeat, 4, program, true);
    const double flushMs = lapMs();
    const double totalMs = std::chrono::duration<double, std::milli>(
        ProfileClock::now() - start).count();

    if (ctx.frameDrawCalls) {
        *ctx.frameDrawCalls += static_cast<int>(batches.lastFlushMetrics().commandCount);
    }

    if (totalMs > 4.0) {
        const auto& metrics = batches.lastFlushMetrics();
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                          "[FRAME_TRACE][DRAW_BATCH_SLOW] label=", label ? label : "?",
                          " totalMs=", totalMs,
                          " shaderUseMs=", shaderUseMs,
                          " resetMs=", resetMs,
                          " cullGenericMs=", cullGenericMs,
                          " flushMs=", flushMs,
                          " matrixUploadMs=", metrics.matrixUploadMs,
                          " materialIndexUploadMs=", metrics.materialIndexUploadMs,
                          " indirectPackMs=", metrics.indirectPackMs,
                          " indirectUploadMs=", metrics.indirectUploadMs,
                          " bindlessPartitionMs=", metrics.bindlessPartitionMs,
                          " drawSubmitMs=", metrics.drawSubmitMs,
                          " cleanupMs=", metrics.cleanupMs,
                          " inputDraws=", draws.size(),
                          " batches=", metrics.batchCount,
                          " commands=", metrics.commandCount,
                          " instances=", metrics.instanceCount,
                          " zeroInstances=", metrics.zeroInstanceCommandCount);
    }
}

void StandardGBufferPass::RenderDraws(RenderContext& ctx, std::vector<DrawCmd>& draws, NDEVC::Graphics::IShader* shader, const char* label)
{
    if (draws.empty() || !shader) return;

    UploadMaterials(ctx, draws);
    RenderDrawsWithoutMaterialUpload(ctx, draws, shader, label);
}

void StandardGBufferPass::RenderLightAccumulation(RenderContext& ctx, float depthScale)
{
    const GLuint packedTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardPackedGBuffer") : 0u;
    const GLuint lightFbo = ctx.resources ? ctx.resources->GetFramebufferHandle("standardLight") : 0u;
    if (packedTexture == 0 || lightFbo == 0 || !lightAccumShader_) return;

    glBindFramebuffer(GL_FRAMEBUFFER, lightFbo);
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const GLfloat clearLight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, clearLight);

    lightAccumShader_->Use();
    const GLuint program = lightAccumShader_->GetNativeHandle()
        ? *reinterpret_cast<GLuint*>(lightAccumShader_->GetNativeHandle())
        : 0u;
    if (program != 0) {
        glUniform1i(0, 0); // packedGBuffer
        glUniform1f(1, depthScale); // depthScale
    }

    glBindVertexArray(ctx.screenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, packedTexture);
    glBindSampler(0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
}

void StandardGBufferPass::ComposePackedGBuffer(RenderContext& ctx, float depthScale)
{
    const GLuint packedTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardPackedGBuffer") : 0u;
    const GLuint albedoTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardAlbedo") : 0u;
    const GLuint lightTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardLightBuffer") : 0u;
    const GLuint litFbo = ctx.resources ? ctx.resources->GetFramebufferHandle("standardLit") : 0u;
    if (packedTexture == 0 || albedoTexture == 0 || lightTexture == 0 || litFbo == 0 || !composeShader_) return;

    glBindFramebuffer(GL_FRAMEBUFFER, litFbo);
    const GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    composeShader_->Use();
    const GLuint program = composeShader_->GetNativeHandle()
        ? *reinterpret_cast<GLuint*>(composeShader_->GetNativeHandle())
        : 0u;
    if (program != 0) {
        glUniform1i(0, 0); // packedGBuffer
        glUniform1i(1, 1); // albedoBuffer
        glUniform1i(2, 2); // lightBuffer
        glUniform1f(3, depthScale); // depthScale
    }

    glBindVertexArray(ctx.screenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, packedTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, albedoTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lightTexture);
    glBindSampler(0, 0);
    glBindSampler(1, 0);
    glBindSampler(2, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
}

void StandardGBufferPass::RenderStandardLighting(RenderContext& ctx, float depthScale)
{
    const GLuint litFbo = ctx.resources ? ctx.resources->GetFramebufferHandle("standardLit") : 0u;
    const GLuint lightTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardLightBuffer") : 0u;
    if (litFbo == 0 || lightTexture == 0 || !litCompositeShader_) return;

    glBindFramebuffer(GL_FRAMEBUFFER, litFbo);
    const GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL);
    glDepthMask(GL_FALSE);
    if (ctx.flags.faceCullingDisabled) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
    }

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, lightTexture);
    glBindSampler(4, 0);

    UploadPSRegisters(depthScale, 0.0f);
    RenderDrawsWithoutMaterialUpload(ctx, solidDraws_, litCompositeShader_.get(), "lightingSolid");

    if (litCompositeAlphaTestShader_) {
        for (DrawCmd& draw : alphaTestDraws_) {
            std::vector<DrawCmd> singleDraw;
            singleDraw.push_back(draw);
            UploadPSRegisters(depthScale, AlphaRefFromCutoff(draw.alphaCutoff));
            RenderDrawsWithoutMaterialUpload(ctx, singleDraw, litCompositeAlphaTestShader_.get(), "lightingAlphaSingle");
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
}

void StandardGBufferPass::PresentLitColor(RenderContext& ctx)
{
    using ProfileClock = std::chrono::high_resolution_clock;
    const auto start = ProfileClock::now();
    auto last = start;
    auto lapMs = [&]() -> double {
        const auto now = ProfileClock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        return ms;
    };

    const GLuint litTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardLitColor") : 0u;
    const GLuint lightTexture = ctx.resources ? ctx.resources->GetTextureHandle("standardLightBuffer") : 0u;
    const GLuint defaultFbo = ctx.device ? ctx.device->GetDefaultFramebuffer() : 0u;
    if (litTexture == 0 || !presentShader_) return;

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    const double setupMs = lapMs();

    presentShader_->Use();
    const GLuint program = presentShader_->GetNativeHandle()
        ? *reinterpret_cast<GLuint*>(presentShader_->GetNativeHandle())
        : 0u;
    if (program != 0) {
        glUniform4f(glGetUniformLocation(program, "focalLength"), 1.0f, 1.0f, 0.0f, 0.0f);
        glUniform4f(glGetUniformLocation(program, "halfPixelSize"),
                    0.5f / std::max(ctx.viewportWidth, 1),
                    0.5f / std::max(ctx.viewportHeight, 1),
                    0.0f,
                    0.0f);
        glUniform1f(glGetUniformLocation(program, "hdrBloomScale"), 0.0f);
        glUniform4f(glGetUniformLocation(program, "satBrightGamma"), 1.0f, 0.0f, 1.0f, 0.0f);
        glUniform4f(glGetUniformLocation(program, "balance"), 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform4f(glGetUniformLocation(program, "luminance"), 0.299f, 0.587f, 0.114f, 1.0f);
        glUniform1f(glGetUniformLocation(program, "brightnessOffset"), 0.12f);
        glUniform1f(glGetUniformLocation(program, "contrastScale"), 1.35f);
        glUniform1f(glGetUniformLocation(program, "fadeValue"), 1.0f);
        glUniform4f(glGetUniformLocation(program, "vignetteColor"), 0.0f, 0.0f, 0.0f, 1.0f);
        glUniform4f(glGetUniformLocation(program, "vignetteControl"), 1.0f, 0.0f, 0.0f, 0.0f);
    }
    const double shaderUniformMs = lapMs();

    glBindVertexArray(ctx.screenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.blackTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, litTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lightTexture != 0 ? lightTexture : ctx.blackTex);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ctx.blackTex);
    glBindSampler(0, 0);
    glBindSampler(1, 0);
    glBindSampler(2, 0);
    glBindSampler(3, 0);
    const double bindMs = lapMs();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    const double drawMs = lapMs();
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    const double cleanupMs = lapMs();
    const double totalMs = std::chrono::duration<double, std::milli>(ProfileClock::now() - start).count();
    if (totalMs > 4.0) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                          "[FRAME_TRACE][PRESENT_SLOW] totalMs=", totalMs,
                          " setupMs=", setupMs,
                          " shaderUniformMs=", shaderUniformMs,
                          " bindMs=", bindMs,
                          " drawMs=", drawMs,
                          " cleanupMs=", cleanupMs,
                          " litTex=", litTexture,
                          " lightTex=", lightTexture,
                          " defaultFbo=", defaultFbo,
                          " program=", program);
    }
}

void StandardGBufferPass::Execute(RenderContext& ctx)
{
    using ProfileClock = std::chrono::high_resolution_clock;
    const auto passStart = ProfileClock::now();
    auto lastMark = passStart;
    auto lapMs = [&]() -> double {
        const auto now = ProfileClock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - lastMark).count();
        lastMark = now;
        return ms;
    };

    ctx.scene->PrepareDrawLists(*ctx.camera,
                                solidDraws_,
                                alphaTestDraws_,
                                decalDraws_,
                                environmentDraws_,
                                environmentAlphaDraws_,
                                simpleLayerDraws_,
                                refractionDraws_,
                                postAlphaUnlitDraws_,
                                waterDraws_);
    if (ctx.onDrawlistsReady) { ctx.onDrawlistsReady(); ctx.onDrawlistsReady = nullptr; }
    const double prepareDrawListsMs = lapMs();

    const GLuint fbo = ctx.resources->GetFramebufferHandle("standardGBuffer");
    const GLuint packedTexture = ctx.resources->GetTextureHandle("standardPackedGBuffer");
    if (!loggedFrame_) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                          "[STD_GBUFFER] lists solid=", solidDraws_.size(),
                          " alphaTest=", alphaTestDraws_.size(),
                          " decals=", decalDraws_.size(),
                          " env=", environmentDraws_.size(),
                          " simple=", simpleLayerDraws_.size(),
                          " fbo=", fbo,
                          " packedTex=", packedTexture,
                          " viewport=", ctx.viewportWidth, "x", ctx.viewportHeight);
    }
    if (fbo == 0) {
        if (!loggedSkip_) {
            NC::LOGGING::Warning("[STD_GBUFFER] missing framebuffer standardGBuffer");
            loggedSkip_ = true;
        }
        return;
    }

    const float farPlane = std::max(ctx.cameraFar, 1.0f);
    const float depthScale = 65535.0f / farPlane;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (!loggedFrame_) {
        const GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                          "[STD_GBUFFER] fboStatus=0x", std::hex,
                          static_cast<unsigned int>(fboStatus), std::dec,
                          " far=", farPlane,
                          " depthScale=", depthScale);
    }
    glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
    const GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);
    
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_TRUE);
    if (ctx.flags.faceCullingDisabled) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
    }

    const GLfloat clearColor[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, clearColor);
    const GLfloat clearDepth = 0.0f;
    glClearBufferfv(GL_DEPTH, 0, &clearDepth);

    UploadVSRegisters(ctx);
    UploadPSRegisters(depthScale, 0.0f);
    const double setupClearRegsMs = lapMs();

    // CRITICAL: Collect ALL draws and upload materials ONCE before rendering any batches.
    // If we call UploadMaterials multiple times, later calls will overwrite the materials buffer,
    // corrupting the material indices for earlier batches.

    // Build materials and assign indices to standard draws only
    materials_.clear();
    directMatrices_.clear();
    directMaterialIndices_.clear();
    materials_.reserve(solidDraws_.size() + alphaTestDraws_.size() + simpleLayerDraws_.size() +
                       environmentDraws_.size() + environmentAlphaDraws_.size());
    directMatrices_.reserve(solidDraws_.size() + alphaTestDraws_.size() + simpleLayerDraws_.size() +
                            environmentDraws_.size() + environmentAlphaDraws_.size());
    directMaterialIndices_.reserve(solidDraws_.size() + alphaTestDraws_.size() + simpleLayerDraws_.size() +
                                   environmentDraws_.size() + environmentAlphaDraws_.size());

    uint32_t matIdx = 0;
    auto appendMaterial = [&](DrawCmd& draw) {
        draw.gpuMaterialIndex = matIdx++;
        materials_.push_back(BuildMaterial(ctx, draw));
        directMatrices_.push_back(draw.worldMatrix);
        directMaterialIndices_.push_back(draw.gpuMaterialIndex);
    };
    for (auto& draw : solidDraws_) {
        appendMaterial(draw);
    }
    for (auto& draw : alphaTestDraws_) {
        appendMaterial(draw);
    }
    for (auto& draw : simpleLayerDraws_) {
        appendMaterial(draw);
    }
    for (auto& draw : environmentDraws_) {
        appendMaterial(draw);
    }
    for (auto& draw : environmentAlphaDraws_) {
        appendMaterial(draw);
    }

    // Upload all materials at once so they remain in the buffer across all rendering calls
    if (!materials_.empty()) {
        const GLsizeiptr byteSize = static_cast<GLsizeiptr>(materials_.size() * sizeof(MaterialGPU));
        glNamedBufferData(materialBuffer_, byteSize, materials_.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialBuffer_);
    }
    if (UseDirectStandardDraws()) {
        UploadDirectDrawBuffers();
        if (!loggedDirectDraw_) {
            NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                              "[STD_GBUFFER] directDrawPath=1 matrices=", directMatrices_.size(),
                              " materialIndices=", directMaterialIndices_.size());
            loggedDirectDraw_ = true;
        }
    }
    const double materialBuildUploadMs = lapMs();

    std::vector<DrawCmd> solidSimpleDraws;
    std::vector<DrawCmd> alphaTestSimpleDraws;
    for (const auto& draw : simpleLayerDraws_) {
        if (draw.alphaTest) alphaTestSimpleDraws.push_back(draw);
        else solidSimpleDraws.push_back(draw);
    }
    const double splitSimpleMs = lapMs();

    RenderDrawsWithoutMaterialUpload(ctx, solidDraws_, solidShader_.get(), "gbufferSolid");
    const double solidDrawMs = lapMs();

    if (alphaClipShader_) {
        for (DrawCmd& draw : alphaTestDraws_) {
            std::vector<DrawCmd> singleDraw;
            singleDraw.push_back(draw);
            UploadPSRegisters(depthScale, AlphaRefFromCutoff(draw.alphaCutoff));
            RenderDrawsWithoutMaterialUpload(ctx, singleDraw, alphaClipShader_.get(), "gbufferAlphaSingle");
        }
    }
    const double alphaDrawMs = lapMs();

    if (simpleLayerShader_) {
        RenderDrawsWithoutMaterialUpload(ctx, solidSimpleDraws, simpleLayerShader_.get(), "simpleSolid");
    }
    const double simpleDrawMs = lapMs();

    if (simpleLayerAlphaClipShader_) {
        for (DrawCmd& draw : alphaTestSimpleDraws) {
            std::vector<DrawCmd> singleDraw;
            singleDraw.push_back(draw);
            UploadPSRegisters(depthScale, AlphaRefFromCutoff(draw.alphaCutoff));
            RenderDrawsWithoutMaterialUpload(ctx, singleDraw, simpleLayerAlphaClipShader_.get(), "simpleAlphaSingle");
        }
    }
    const double simpleAlphaDrawMs = lapMs();

    if (!environmentDraws_.empty() && solidShader_) {
        RenderDrawsWithoutMaterialUpload(ctx, environmentDraws_, solidShader_.get(), "environmentSolid");
    }
    const double environmentDrawMs = lapMs();

    if (!environmentAlphaDraws_.empty() && alphaClipShader_) {
        for (DrawCmd& draw : environmentAlphaDraws_) {
            std::vector<DrawCmd> singleDraw;
            singleDraw.push_back(draw);
            UploadPSRegisters(depthScale, AlphaRefFromCutoff(draw.alphaCutoff));
            RenderDrawsWithoutMaterialUpload(ctx, singleDraw, alphaClipShader_.get(), "environmentAlphaSingle");
        }
    }
    const double environmentAlphaDrawMs = lapMs();

    RenderLightAccumulation(ctx, depthScale);
    const double lightAccumMs = lapMs();
    ComposePackedGBuffer(ctx, depthScale);
    const double composeMs = lapMs();
    PresentLitColor(ctx);
    const double presentMs = lapMs();
    glFrontFace(GL_CCW);
    const double totalMs = std::chrono::duration<double, std::milli>(
        ProfileClock::now() - passStart).count();
    if (totalMs > 8.0) {
        NC::LOGGING::Info(NC::LOGGING::Category::Graphics,
                          "[FRAME_TRACE][STD_GBUFFER_SLOW] totalMs=", totalMs,
                          " prepareListsMs=", prepareDrawListsMs,
                          " setupClearRegsMs=", setupClearRegsMs,
                          " materialBuildUploadMs=", materialBuildUploadMs,
                          " splitSimpleMs=", splitSimpleMs,
                          " solidDrawMs=", solidDrawMs,
                          " alphaDrawMs=", alphaDrawMs,
                          " simpleDrawMs=", simpleDrawMs,
                          " simpleAlphaDrawMs=", simpleAlphaDrawMs,
                          " envDrawMs=", environmentDrawMs,
                          " envAlphaDrawMs=", environmentAlphaDrawMs,
                          " lightAccumMs=", lightAccumMs,
                          " composeMs=", composeMs,
                          " presentMs=", presentMs,
                          " materials=", materials_.size(),
                          " solid=", solidDraws_.size(),
                          " alpha=", alphaTestDraws_.size(),
                          " simple=", simpleLayerDraws_.size(),
                          " simpleAlpha=", alphaTestSimpleDraws.size(),
                          " env=", environmentDraws_.size(),
                          " envAlpha=", environmentAlphaDraws_.size());
    }
    loggedFrame_ = true;
}
