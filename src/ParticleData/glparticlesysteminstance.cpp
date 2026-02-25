// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "glparticlesysteminstance.h"
#include "glparticlerenderer.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <iostream>
#include "../Errors.h"

namespace Particles {

    GLParticleSystemInstance::GLParticleSystemInstance() = default;

    void GLParticleSystemInstance::SetParticleTexture(GLuint tex) {
        particleTexture = tex;
        if (tex == 0) {
            SetTextureSize(0, 0);
            return;
        }

        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glBindTexture(GL_TEXTURE_2D, tex);

        GLint w = 0;
        GLint h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        SetTextureSize((int)w, (int)h);

        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    }

    void GLParticleSystemInstance::UpdateVertexStreams() {
        if (!renderer || !IsValid()) return;

        const uint32_t startIdx = renderer->GetCurParticleVertexIndex();
        uint8_t* ptr = (uint8_t*)renderer->GetCurVertexPtr();
        if (!ptr) return;

        const size_t count = particles.Size();
        const EmitterAttrs& emAttrs = GetParticleSystem()->GetEmitterAttrs();
        const bool renderOldestFirst = emAttrs.GetBool(EmitterAttrs::RenderOldestFirst);
        const auto emitOne = [&](const Particle& particle) {
            if (particle.relAge >= 1.0f) return;
            auto* vdata = (ParticleVertexData*)ptr;
            vdata->position = particle.position;
            vdata->stretchPosition = particle.stretchPosition;
            vdata->color = particle.color;
            vdata->uvMinMax = particle.uvMinMax;
            vdata->rotSizeId = glm::vec4(particle.rotation, particle.size, particle.particleId, 0.0f);

            ptr += sizeof(ParticleVertexData);
            renderer->AddCurParticleVertexIndex(1);
        };

        if (renderOldestFirst && count > 1) {
            std::vector<size_t> indices;
            indices.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (particles[i].relAge < 1.0f) indices.push_back(i);
            }
            std::stable_sort(indices.begin(), indices.end(),
                [&](size_t a, size_t b) { return particles[a].relAge > particles[b].relAge; });
            for (size_t idx : indices) {
                if (renderer->GetCurParticleVertexIndex() >= MaxNumRenderedParticles) break;
                emitOne(particles[idx]);
            }
        } else {
            for (size_t i = 0; i < count && renderer->GetCurParticleVertexIndex() < MaxNumRenderedParticles; ++i) {
                const auto& particle = particles[i];
                emitOne(particle);
            }
        }

        renderer->SetCurVertexPtr(ptr);
        baseVertexIndex = startIdx;
        numParticlesThisFrame = renderer->GetCurParticleVertexIndex() - startIdx;
        renderInfo = ParticleRenderInfo(baseVertexIndex, numParticlesThisFrame);
        PLOG("Inst=%p tex=%u live=%zu wroteThis=%u curIdx=%u ptr=%p",
         (void*)this, (unsigned)particleTexture, numLiveParticles, (unsigned)numParticlesThisFrame,
         (unsigned)renderer->GetCurParticleVertexIndex(), renderer->GetCurVertexPtr());
        GLC("UpdateVertexStreams");
    }

    void GLParticleSystemInstance::Render(const glm::mat4& viewProj, const glm::mat4& view, const glm::mat4& invView,
                                          const glm::vec3& eyePos, const glm::vec2& fogDistances, const glm::vec4& fogColor,
                                          uint32_t gPositionVSTex, const glm::vec2& invViewport,
                                          int numAnimPhases, float animFramesPerSecond, float time, int colorMode,
                                          float intensity0, float emissiveIntensity) {
        if (!renderer || !IsValid()) return;
        const EmitterAttrs& emAttrs = GetParticleSystem()->GetEmitterAttrs();
        if (numParticlesThisFrame > 0) {
            static bool once = true;
            if (once) {
                std::cerr << "[PARTICLE RENDER DEBUG] particleTexture=" << particleTexture << " numParticles=" << numParticlesThisFrame << "\n";
                once = false;
            }
            renderer->Render(viewProj, view, invView, eyePos, fogDistances, fogColor,
                             transform, numParticlesThisFrame, baseVertexIndex,
                             particleTexture, static_cast<GLuint>(gPositionVSTex), invViewport,
                             numAnimPhases, animFramesPerSecond, time,
                             emAttrs.GetBool(EmitterAttrs::Billboard), colorMode,
                             intensity0, emissiveIntensity);
        }
    }

} // namespace Particles
