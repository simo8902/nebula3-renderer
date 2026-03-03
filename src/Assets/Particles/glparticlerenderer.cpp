// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "glparticlerenderer.h"
#include "Rendering/OpenGL/OpenGLShaderManager.h"
#include <iostream>
#include "Core/Errors.h"

namespace Particles {

    struct CornerVertex {
        float x, y;
    };

using UniformID = NDEVC::Graphics::IShader::UniformID;
static constexpr UniformID U_VIEW_PROJ = NDEVC::Graphics::IShader::MakeUniformID("viewProj");
static constexpr UniformID U_VIEW = NDEVC::Graphics::IShader::MakeUniformID("view");
static constexpr UniformID U_INV_VIEW = NDEVC::Graphics::IShader::MakeUniformID("invView");
static constexpr UniformID U_EYE_POS = NDEVC::Graphics::IShader::MakeUniformID("eyePos");
static constexpr UniformID U_EMITTER_TRANSFORM = NDEVC::Graphics::IShader::MakeUniformID("emitterTransform");
static constexpr UniformID U_BILLBOARD = NDEVC::Graphics::IShader::MakeUniformID("billboard");
static constexpr UniformID U_NUM_ANIM_PHASES = NDEVC::Graphics::IShader::MakeUniformID("numAnimPhases");
static constexpr UniformID U_ANIM_FPS = NDEVC::Graphics::IShader::MakeUniformID("animFramesPerSecond");
static constexpr UniformID U_TIME = NDEVC::Graphics::IShader::MakeUniformID("time");
static constexpr UniformID U_COLOR_MODE = NDEVC::Graphics::IShader::MakeUniformID("colorMode");
static constexpr UniformID U_INTENSITY0 = NDEVC::Graphics::IShader::MakeUniformID("Intensity0");
static constexpr UniformID U_MAT_EMISSIVE_INTENSITY = NDEVC::Graphics::IShader::MakeUniformID("MatEmissiveIntensity");
static constexpr UniformID U_INV_VIEWPORT = NDEVC::Graphics::IShader::MakeUniformID("invViewport");
static constexpr UniformID U_FOG_DISTANCES = NDEVC::Graphics::IShader::MakeUniformID("fogDistances");
static constexpr UniformID U_FOG_COLOR = NDEVC::Graphics::IShader::MakeUniformID("fogColor");
static constexpr UniformID U_HDR_SCALE = NDEVC::Graphics::IShader::MakeUniformID("hdrScale");
static constexpr UniformID U_PARTICLE_TEXTURE = NDEVC::Graphics::IShader::MakeUniformID("particleTexture");
static constexpr UniformID U_GPOSITION_WS_TEX = NDEVC::Graphics::IShader::MakeUniformID("gPositionWSTex");


GLParticleRenderer::GLParticleRenderer() = default;

GLParticleRenderer::~GLParticleRenderer() {
    Discard();
}

void GLParticleRenderer::Setup(NDEVC::Graphics::IShaderManager* sharedShaderManager) {
    if (inAttach) EndAttach();
    particleShader.reset();
    if (ownedShaderManager) {
        ownedShaderManager->Shutdown();
        ownedShaderManager.reset();
    }

    if (sharedShaderManager) {
        shaderManager = sharedShaderManager;
    } else {
        ownedShaderManager = std::make_unique<NDEVC::Graphics::OpenGL::OpenGLShaderManager>();
        shaderManager = ownedShaderManager.get();
        if (!shaderManager) {
            std::cerr << "[GLParticleRenderer] No ShaderManager provided\n";
            return;
        }
        shaderManager->Initialize();
    }

    particleShader = shaderManager->GetShader("particle");
    if (!particleShader || !particleShader->IsValid()) {
        std::cerr << "[GLParticleRenderer] Particle shader not found or invalid\n";
        return;
    }
    particleShader->PrecacheUniform(U_VIEW_PROJ, "viewProj");
    particleShader->PrecacheUniform(U_VIEW, "view");
    particleShader->PrecacheUniform(U_INV_VIEW, "invView");
    particleShader->PrecacheUniform(U_EYE_POS, "eyePos");
    particleShader->PrecacheUniform(U_EMITTER_TRANSFORM, "emitterTransform");
    particleShader->PrecacheUniform(U_BILLBOARD, "billboard");
    particleShader->PrecacheUniform(U_NUM_ANIM_PHASES, "numAnimPhases");
    particleShader->PrecacheUniform(U_ANIM_FPS, "animFramesPerSecond");
    particleShader->PrecacheUniform(U_TIME, "time");
    particleShader->PrecacheUniform(U_COLOR_MODE, "colorMode");
    particleShader->PrecacheUniform(U_INTENSITY0, "Intensity0");
    particleShader->PrecacheUniform(U_MAT_EMISSIVE_INTENSITY, "MatEmissiveIntensity");
    particleShader->PrecacheUniform(U_INV_VIEWPORT, "invViewport");
    particleShader->PrecacheUniform(U_FOG_DISTANCES, "fogDistances");
    particleShader->PrecacheUniform(U_FOG_COLOR, "fogColor");
    particleShader->PrecacheUniform(U_HDR_SCALE, "hdrScale");
    particleShader->PrecacheUniform(U_PARTICLE_TEXTURE, "particleTexture");
    particleShader->PrecacheUniform(U_GPOSITION_WS_TEX, "gPositionWSTex");

    glGenSamplers(1, sampler.put());
    // Particle textures are often authored without full mip chains.
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    CreateCornerMesh();
    CreateParticleMesh();
    SetupVertexAttributes();

    valid = true;
}

void GLParticleRenderer::Discard() {
    if (inAttach) EndAttach();

    vao.reset();
    cornerVBO.reset();
    particleVBO.reset();
    cornerIBO.reset();
    sampler.reset();

    mappedParticleBuffer = nullptr;
    curVertexPtr = nullptr;
    curParticleVertexIndex = 0;
    particleShader.reset();
    if (ownedShaderManager) {
        ownedShaderManager->Shutdown();
        ownedShaderManager.reset();
    }
    shaderManager = nullptr;
    valid = false;
}

void GLParticleRenderer::CreateCornerMesh() {
    CornerVertex cornerData[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    uint16_t indexData[6] = {0, 2, 1, 2, 0, 3};

    glGenBuffers(1, cornerVBO.put());
    glBindBuffer(GL_ARRAY_BUFFER, cornerVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cornerData), cornerData, GL_STATIC_DRAW);

    glGenBuffers(1, cornerIBO.put());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cornerIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indexData), indexData, GL_STATIC_DRAW);
}

void GLParticleRenderer::CreateParticleMesh() {
    glGenBuffers(1, particleVBO.put());
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
    glBufferData(GL_ARRAY_BUFFER, MaxNumRenderedParticles * sizeof(ParticleVertexData), nullptr, GL_DYNAMIC_DRAW);
}

void GLParticleRenderer::SetupVertexAttributes() {
    glGenVertexArrays(1, vao.put());
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, cornerVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CornerVertex), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertexData), (void*)offsetof(ParticleVertexData, position));
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertexData), (void*)offsetof(ParticleVertexData, stretchPosition));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertexData), (void*)offsetof(ParticleVertexData, color));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertexData), (void*)offsetof(ParticleVertexData, uvMinMax));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertexData), (void*)offsetof(ParticleVertexData, rotSizeId));
    glVertexAttribDivisor(5, 1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cornerIBO);
    glBindVertexArray(0);
}

void GLParticleRenderer::BeginAttach() {
    if (!valid) return;
    inAttach = true;
    curParticleVertexIndex = 0;

    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
    mappedParticleBuffer = (uint8_t*)glMapBufferRange(
        GL_ARRAY_BUFFER, 0,
        MaxNumRenderedParticles * sizeof(ParticleVertexData),
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT
    );
    curVertexPtr = mappedParticleBuffer;

    PLOG("BeginAttach mapPtr=%p vbo=%u max=%u", (void*)mappedParticleBuffer, (unsigned)particleVBO, (unsigned)MaxNumRenderedParticles);
    GLC("BeginAttach");
}

void GLParticleRenderer::EndAttach() {
    if (!inAttach || !mappedParticleBuffer) return;

    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
    glUnmapBuffer(GL_ARRAY_BUFFER);

    PLOG("EndAttach writtenInstances=%u bytes=%zu", (unsigned)curParticleVertexIndex, (size_t)curParticleVertexIndex * sizeof(ParticleVertexData));
    GLC("EndAttach");

    mappedParticleBuffer = nullptr;
    curVertexPtr = nullptr;
    curParticleVertexIndex = 0;
    inAttach = false;
}

void GLParticleRenderer::Render(const glm::mat4& viewProj, const glm::mat4& view, const glm::mat4& invView,
                                 const glm::vec3& eyePos, const glm::vec2& fogDistances, const glm::vec4& fogColor,
                                 const glm::mat4& emitterTransform, uint32_t numParticles, uint32_t baseInstance,
                                 GLuint texture, GLuint gPositionWSTex, const glm::vec2& invViewport,
                                 int numAnimPhases, float animFramesPerSecond, float time,
                                 bool billboard, int colorMode,
                                 float intensity0, float emissiveIntensity) {
    if (numParticles == 0 || !particleShader || !particleShader->IsValid()) return;
    if (!texture) return;

#ifdef ENABLE_PARTICLE_LOG
    GLint drawFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    GLint vp[4] = {}; glGetIntegerv(GL_VIEWPORT, vp);
    GLboolean cm[4] = {}; glGetBooleanv(GL_COLOR_WRITEMASK, cm);
    GLint sc[4] = {}; glGetIntegerv(GL_SCISSOR_BOX, sc);
    GLboolean scOn = glIsEnabled(GL_SCISSOR_TEST);
    PLOG("Render num=%u fbo=%d tex=%u vp=%d,%d,%d,%d scissor=%d box=%d,%d,%d,%d colormask=%d%d%d%d",
         (unsigned)numParticles, drawFbo, (unsigned)texture,
         vp[0],vp[1],vp[2],vp[3], (int)scOn, sc[0],sc[1],sc[2],sc[3],
         (int)cm[0],(int)cm[1],(int)cm[2],(int)cm[3]);

    GLint mappedVBO = 0;
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &mappedVBO);
    if (mappedVBO) {
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
#endif

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    particleShader->Use();

    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(0, sampler);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gPositionWSTex);
    glBindSampler(1, 0);

    particleShader->SetMat4(U_VIEW_PROJ, viewProj);
    particleShader->SetMat4(U_VIEW, view);
    particleShader->SetMat4(U_INV_VIEW, invView);
    particleShader->SetVec3(U_EYE_POS, eyePos);
    particleShader->SetMat4(U_EMITTER_TRANSFORM, emitterTransform);
    particleShader->SetInt(U_BILLBOARD, billboard ? 1 : 0);
    particleShader->SetInt(U_NUM_ANIM_PHASES, numAnimPhases);
    particleShader->SetFloat(U_ANIM_FPS, animFramesPerSecond);
    particleShader->SetFloat(U_TIME, time);
    particleShader->SetInt(U_COLOR_MODE, colorMode);
    particleShader->SetFloat(U_INTENSITY0, intensity0);
    particleShader->SetFloat(U_MAT_EMISSIVE_INTENSITY, emissiveIntensity);
    particleShader->SetVec2(U_INV_VIEWPORT, invViewport);
    particleShader->SetVec2(U_FOG_DISTANCES, fogDistances);
    particleShader->SetVec4(U_FOG_COLOR, fogColor);
    particleShader->SetFloat(U_HDR_SCALE, 1.0f);
    particleShader->SetInt(U_PARTICLE_TEXTURE, 0);
    particleShader->SetInt(U_GPOSITION_WS_TEX, 1);

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glDisable(GL_CULL_FACE);

    glDrawElementsInstancedBaseInstance(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr,
                                        (GLsizei)numParticles, baseInstance);

    glDepthMask(GL_TRUE);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(1, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(0, 0);
    GLC("ParticleRender");
}
} // namespace Particles
