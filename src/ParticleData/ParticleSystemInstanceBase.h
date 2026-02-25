// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLESYSTEMINSTANCEBASE_H
#define NDEVC_PARTICLESYSTEMINSTANCEBASE_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include "Particle.h"
#include "RingBuffer.h"
#include "ParticleRenderInfo.h"
#include "ParticleSystem.h"
#include "ParticleSystemState.h"

namespace Particles {

    class ParticleSystemInstanceBase {
    public:
        ParticleSystemInstanceBase();
        virtual ~ParticleSystemInstanceBase() = default;

        void Setup(const std::shared_ptr<ParticleSystem>& particleSystem);
        void Discard();
        bool IsValid() const { return particleSystem != nullptr && particleSystem->IsValid(); }

        void Start();
        void Stop();
        bool IsPlaying() const { return (stateMask & ParticleSystemState::Playing) != 0; }
        bool IsStopping() const { return (stateMask & ParticleSystemState::Stopping) != 0; }
        bool IsStopped() const { return (stateMask & ParticleSystemState::Stopped) != 0; }

        void Update(float time);
        virtual void UpdateVertexStreams() = 0;
        virtual void Render(const glm::mat4& viewProj, const glm::mat4& view, const glm::mat4& invView,
                            const glm::vec3& eyePos, const glm::vec2& fogDistances, const glm::vec4& fogColor,
                            uint32_t gPositionVSTex, const glm::vec2& invViewport,
                            int numAnimPhases, float animFramesPerSecond, float time, int colorMode,
                            float intensity0, float emissiveIntensity) = 0;

        const RingBuffer<Particle>& GetParticles() const { return particles; }
        size_t GetNumLiveParticles() const { return numLiveParticles; }
        const ParticleRenderInfo& GetParticleRenderInfo() const { return renderInfo; }

        void SetTransform(const glm::mat4& transform) { this->transform = transform; }
        const glm::mat4& GetTransform() const { return transform; }
        void SetTextureSize(int width, int height) { textureWidth = width; textureHeight = height; }
        void SetWindVector(const glm::vec3& v) { windVector = v; }
        const glm::vec3& GetWindVector() const { return windVector; }
        const std::shared_ptr<ParticleSystem>& GetParticleSystem() const { return particleSystem; }

    protected:
        void EmitParticles(float stepTime);
        void EmitParticle(size_t emissionSampleIndex, float initialAge);
        void UpdateParticles(float stepTime);
        void UpdateState(float time);

        RingBuffer<Particle> particles;
        size_t numLiveParticles = 0;
        glm::mat4 transform{1.0f};
        glm::vec3 windVector{0.0f, 0.0f, 0.0f};
        int textureWidth = 0;
        int textureHeight = 0;

        std::shared_ptr<ParticleSystem> particleSystem;
        ParticleRenderInfo renderInfo;
        float curStepTime = 0.0f;
        float lastEmissionTime = 0.0f;
        float timeSinceEmissionStart = 0.0f;
        bool firstEmissionFrame = true;
        uint32_t emissionCounter = 0;

        ParticleSystemState::Mask stateMask = ParticleSystemState::Initial;
        ParticleSystemState::Mask stateChangeMask = 0;

    };

} // namespace Particles

#endif //NDEVC_PARTICLESYSTEMINSTANCEBASE_H
