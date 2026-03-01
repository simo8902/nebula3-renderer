// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLESYSTEM_H
#define NDEVC_PARTICLESYSTEM_H

#include <cstddef>

#include "EmitterAttrs.h"
#include "EnvelopeSampleBuffer.h"
#include "EmitterMesh.h"

namespace Particles {

class ParticleSystem {
public:
    static constexpr float DefaultStepTime = 1.0f / 60.0f;
    static float StepTime;
    static void ModulateStepTime(float val);

    ParticleSystem() = default;
    ~ParticleSystem();

    void Setup(const EmitterMesh& emitterMesh, const EmitterAttrs& emitterAttrs);
    void Discard();
    bool IsValid() const { return isValid; }

    const EmitterMesh& GetEmitterMesh() const { return emitterMesh; }
    const EmitterAttrs& GetEmitterAttrs() const { return emitterAttrs; }
    const EnvelopeSampleBuffer& GetEnvelopeSampleBuffer() const { return envelopeSampleBuffer; }
    size_t GetMaxNumParticles() const;

private:
    EmitterAttrs emitterAttrs;
    EnvelopeSampleBuffer envelopeSampleBuffer;
    EmitterMesh emitterMesh;
    bool isValid = false;
};

} // namespace Particles

#endif // NDEVC_PARTICLESYSTEM_H
