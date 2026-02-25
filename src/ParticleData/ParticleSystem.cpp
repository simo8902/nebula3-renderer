// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "ParticleSystem.h"

#include "Particle.h"

namespace Particles {

float ParticleSystem::StepTime = ParticleSystem::DefaultStepTime;

ParticleSystem::~ParticleSystem() {
    if (IsValid()) {
        Discard();
    }
}

void ParticleSystem::Setup(const EmitterMesh& mesh, const EmitterAttrs& attrs) {
    if (IsValid()) return;

    emitterAttrs = attrs;
    envelopeSampleBuffer.Setup(attrs, ParticleSystemNumEnvelopeSamples);
    emitterMesh = mesh;
    isValid = true;
}

void ParticleSystem::Discard() {
    if (!IsValid()) return;

    isValid = false;
    emitterMesh.Discard();
    envelopeSampleBuffer.Discard();
}

size_t ParticleSystem::GetMaxNumParticles() const {
    const float maxFreq = emitterAttrs.GetEnvelope(EmitterAttrs::EmissionFrequency).GetMaxValue();
    const float maxLifeTime = emitterAttrs.GetEnvelope(EmitterAttrs::LifeTime).GetMaxValue();
    return 1 + static_cast<size_t>(maxFreq * maxLifeTime);
}

void ParticleSystem::ModulateStepTime(float val) {
    ParticleSystem::StepTime = val * ParticleSystem::DefaultStepTime;
}

} // namespace Particles
