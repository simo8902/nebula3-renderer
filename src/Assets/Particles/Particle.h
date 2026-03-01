// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLE_H
#define NDEVC_PARTICLE_H

#include <array>
#include <cstdint>
#include "emitterattrs.h"
#include "glm.hpp"

namespace Particles {

    static constexpr size_t ParticleSystemNumEnvelopeSamples = 192;
    static constexpr size_t MaxNumRenderedParticles = 8192;

    struct alignas(16) Particle {
        glm::vec4 position;
        glm::vec4 startPosition;
        glm::vec4 stretchPosition;
        glm::vec4 velocity;
        glm::vec4 uvMinMax;
        glm::vec4 color;
        float rotation;
        float rotationVariation;
        float size;
        float sizeVariation;
        float oneDivLifeTime;
        float relAge;
        float age;
        float particleId;
    };

    struct alignas(16) JobUniformData {
        glm::vec4 gravity;
        glm::vec3 windVector;
        float stepTime;
        bool stretchToStart;
        float stretchTime;
        std::array<float, ParticleSystemNumEnvelopeSamples * EmitterAttrs::NumEnvelopeAttrs> sampleBuffer;

        JobUniformData() : windVector(0.0f, 0.0f, 0.0f), stepTime(0.0f),
                                       stretchToStart(false), stretchTime(0.0f) {
            gravity = glm::vec4(0.0f, -9.81f, 0.0f, 0.0f);
            sampleBuffer.fill(0.0f);
        }
    };

    struct alignas(16) JobSliceOutputData {
        glm::vec4 bboxMin;
        glm::vec4 bboxMax;
        uint32_t numLivingParticles;
    };

} // namespace Particles

#endif //NDEVC_PARTICLE_H
