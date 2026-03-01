// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.


#include "ParticleSystemInstanceBase.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>

#include "gtc/matrix_transform.hpp"
#include "Core/Errors.h"
namespace Particles {

static std::random_device rd;
static std::mt19937 gen(rd());

ParticleSystemInstanceBase::ParticleSystemInstanceBase() = default;

void ParticleSystemInstanceBase::Setup(const std::shared_ptr<ParticleSystem>& psys) {
    if (IsValid()) return;
    if (!psys || !psys->IsValid()) return;

    particleSystem = psys;

    size_t maxNumParticles = particleSystem->GetMaxNumParticles();
    if (maxNumParticles < 1) maxNumParticles = 1;
    particles.SetCapacity(maxNumParticles);
    numLiveParticles = 0;
    curStepTime = 0.0f;
    lastEmissionTime = 0.0f;
    timeSinceEmissionStart = 0.0f;
    firstEmissionFrame = true;
    emissionCounter = 0;
    stateMask = ParticleSystemState::Initial;
    stateChangeMask = 0;
    renderInfo.Clear();
}

void ParticleSystemInstanceBase::Discard() {
    if (!IsValid()) return;

    particles.Clear();
    numLiveParticles = 0;
    curStepTime = 0.0f;
    lastEmissionTime = 0.0f;
    timeSinceEmissionStart = 0.0f;
    firstEmissionFrame = true;
    emissionCounter = 0;
    stateMask = ParticleSystemState::Initial;
    stateChangeMask = 0;
    renderInfo.Clear();
    particleSystem.reset();
}

void ParticleSystemInstanceBase::Start() {
    if (!IsValid()) return;
    stateChangeMask = ParticleSystemState::Playing;
}

void ParticleSystemInstanceBase::Stop() {
    if (!IsValid()) return;
    stateChangeMask = ParticleSystemState::Stopped;
}

void ParticleSystemInstanceBase::Update(float time) {
    if (!IsValid()) return;

    UpdateState(time);

    float timeDiff = time - curStepTime;
    assert(timeDiff >= 0.0f);
    if (timeDiff < 0.0f) return;
    if (timeDiff > 0.5f) {
        curStepTime = time - ParticleSystem::StepTime;
        timeDiff = ParticleSystem::StepTime;
    }

    if (IsPlaying()) {
        EmitParticles(timeDiff);
        UpdateParticles(timeDiff);
        curStepTime += timeDiff;
    }
}

namespace {
constexpr float kTiny = 1.0e-7f;
constexpr float kTwoPi = 6.28318530718f;

float Rand01() {
    static std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    return u01(gen);
}
} // namespace

void ParticleSystemInstanceBase::EmitParticles(float stepTime) {
    const EmitterAttrs& emAttrs = particleSystem->GetEmitterAttrs();
    const EnvelopeSampleBuffer& sampleBuffer = particleSystem->GetEnvelopeSampleBuffer();

    const float startDelay = emAttrs.GetFloat(EmitterAttrs::StartDelay);
    const float rawDuration = emAttrs.GetFloat(EmitterAttrs::EmissionDuration);
    const float emDuration = (std::isfinite(rawDuration) && rawDuration > kTiny) ? rawDuration : 1.0f;
    const bool looping = emAttrs.GetBool(EmitterAttrs::Looping);

    timeSinceEmissionStart += stepTime;
    const float loopTime = emDuration + startDelay;

    if (looping && loopTime > kTiny && timeSinceEmissionStart > loopTime) {
        timeSinceEmissionStart = std::fmod(timeSinceEmissionStart, loopTime);
    }

    if (timeSinceEmissionStart < startDelay) {
        return;
    }
    if (!looping && (timeSinceEmissionStart > loopTime)) {
        if ((stateMask & (ParticleSystemState::Stopping | ParticleSystemState::Stopped)) == 0) {
            Stop();
        }
        return;
    }

    const float relEmissionTime = (timeSinceEmissionStart - startDelay) / emDuration;
    const float relEmissionTimeClamped = glm::clamp(relEmissionTime, 0.0f, 1.0f);
    const size_t emSampleIndex =
        static_cast<size_t>(relEmissionTimeClamped * (ParticleSystemNumEnvelopeSamples - 1));
    const float* emissionEnvSamples = sampleBuffer.LookupSamples(emSampleIndex);
    const float emFrequency = emissionEnvSamples[EmitterAttrs::EmissionFrequency];
    if (emFrequency <= kTiny) {
        return;
    }

    const float emTimeStep = 1.0f / emFrequency;

    if (firstEmissionFrame) {
        firstEmissionFrame = false;

        const float preCalcTime = emAttrs.GetFloat(EmitterAttrs::PrecalcTime);
        if (preCalcTime > kTiny) {
            lastEmissionTime = 0.0f;
            float updateTime = 0.0f;
            float updateStep = 0.05f;
            if (updateStep < emTimeStep) updateStep = emTimeStep;

            while (lastEmissionTime < preCalcTime) {
                EmitParticle(emSampleIndex, 0.0f);
                lastEmissionTime += emTimeStep;
                updateTime += emTimeStep;
                if (updateTime >= updateStep) {
                    UpdateParticles(updateStep);
                    updateTime = 0.0f;
                }
            }
        }

        lastEmissionTime = curStepTime;
        if (stepTime > emTimeStep) {
            lastEmissionTime -= (stepTime + kTiny);
        } else {
            lastEmissionTime -= (emTimeStep + kTiny);
        }
    }

    while ((lastEmissionTime + emTimeStep) <= curStepTime) {
        lastEmissionTime += emTimeStep;
        EmitParticle(emSampleIndex, (float)(curStepTime - lastEmissionTime));
    }
}

void ParticleSystemInstanceBase::EmitParticle(size_t emissionSampleIndex, float initialAge) {
    const EmitterMesh& emMesh = particleSystem->GetEmitterMesh();
    const EmitterAttrs emAttrs = particleSystem->GetEmitterAttrs();
    const EnvelopeSampleBuffer& sampleBuffer = particleSystem->GetEnvelopeSampleBuffer();
    const EmitterMesh::EmitterPoint& emitterPt = emMesh.GetEmitterPoint(emissionCounter++);
    const float* particleEnvSamples = sampleBuffer.LookupSamples(0);
    const float* emissionEnvSamples = sampleBuffer.LookupSamples(emissionSampleIndex);

    Particle particle{};
    particle.position = transform * emitterPt.position;
    particle.startPosition = particle.position;
    particle.stretchPosition = particle.position;

    const float minSpread = emissionEnvSamples[EmitterAttrs::SpreadMin];
    const float maxSpread = emissionEnvSamples[EmitterAttrs::SpreadMax];
    const float theta = glm::radians(glm::mix(minSpread, maxSpread, Rand01()));
    const float rho = kTwoPi * Rand01();

    const glm::mat4 rot =
        glm::rotate(glm::mat4(1.0f), theta, glm::vec3(emitterPt.tangent)) *
        glm::rotate(glm::mat4(1.0f), rho, glm::vec3(emitterPt.normal));
    const glm::vec3 emNormal =
        glm::vec3(rot * glm::vec4(glm::vec3(emitterPt.normal), 0.0f));

    const float velocityVariation =
        1.0f - (Rand01() * emAttrs.GetFloat(EmitterAttrs::VelocityRandomize));
    const float startVelocity = emissionEnvSamples[EmitterAttrs::StartVelocity] * velocityVariation;
    particle.velocity = glm::vec4(emNormal * startVelocity, 0.0f);

    const float texTile = glm::clamp(emAttrs.GetFloat(EmitterAttrs::TextureTile), 1.0f, 16.0f);
    const float step = 1.0f / texTile;
    const float tileIndex = std::floor(Rand01() * texTile);
    const float vMin = step * tileIndex;
    const float vMax = vMin + step;
    particle.uvMinMax = glm::vec4(1.0f, 1.0f - vMin, 0.0f, 1.0f - vMax);

    particle.color.x = particleEnvSamples[EmitterAttrs::Red];
    particle.color.y = particleEnvSamples[EmitterAttrs::Green];
    particle.color.z = particleEnvSamples[EmitterAttrs::Blue];
    particle.color.w = particleEnvSamples[EmitterAttrs::Alpha];

    const float startRotMin = emAttrs.GetFloat(EmitterAttrs::StartRotationMin);
    const float startRotMax = emAttrs.GetFloat(EmitterAttrs::StartRotationMax);
    particle.rotation = glm::mix(startRotMin, startRotMax, Rand01());
    float rotVar = 1.0f - (Rand01() * emAttrs.GetFloat(EmitterAttrs::RotationRandomize));
    if (emAttrs.GetBool(EmitterAttrs::RandomizeRotation) && (Rand01() < 0.5f)) {
        rotVar = -rotVar;
    }
    particle.rotationVariation = rotVar;

    particle.size = particleEnvSamples[EmitterAttrs::Size];
    particle.sizeVariation = 1.0f - (Rand01() * emAttrs.GetFloat(EmitterAttrs::SizeRandomize));

    float lifetime = emissionEnvSamples[EmitterAttrs::LifeTime];
    if (!std::isfinite(lifetime) || lifetime < kTiny) {
        lifetime = kTiny;
    }
    particle.oneDivLifeTime = 1.0f / lifetime;
    particle.relAge = initialAge * particle.oneDivLifeTime;
    particle.age = initialAge;

    if (particle.relAge < 0.25f) {
        particle.particleId = 4.0f;
    } else if (particle.relAge < 0.5f) {
        particle.particleId = 3.0f;
    } else if (particle.relAge < 0.75f) {
        particle.particleId = 2.0f;
    } else {
        particle.particleId = 1.0f;
    }

    particles.Add(particle);
}

void ParticleSystemInstanceBase::UpdateParticles(float stepTime) {
    const EmitterAttrs& emAttrs = particleSystem->GetEmitterAttrs();
    const EnvelopeSampleBuffer& sampleBuffer = particleSystem->GetEnvelopeSampleBuffer();
    const float gravity = emAttrs.GetFloat(EmitterAttrs::Gravity);
    const float stretchTime = emAttrs.GetFloat(EmitterAttrs::ParticleStretch);
    const bool stretchToStart = emAttrs.GetBool(EmitterAttrs::StretchToStart);

    const size_t count = particles.Size();
    Particle* buffer = particles.GetBuffer();
    size_t liveCount = 0;

    for (size_t i = 0; i < count; ++i) {
        Particle& p = buffer[i];
        const Particle in = p;

        p.age = in.age + stepTime;
        p.relAge = in.relAge + stepTime * in.oneDivLifeTime;

        if (p.relAge < 1.0f) {
            const size_t sampleIndex =
                static_cast<size_t>(p.relAge * (ParticleSystemNumEnvelopeSamples - 1));
            const float* samples = sampleBuffer.LookupSamples(sampleIndex);
            const float airResist = samples[EmitterAttrs::AirResistance];
            const float velFactor = samples[EmitterAttrs::VelocityFactor];
            const float mass = samples[EmitterAttrs::Mass];
            const float rotVel = samples[EmitterAttrs::RotationVelocity];

            glm::vec3 accel = windVector * airResist + glm::vec3(0.0f, gravity, 0.0f);
            accel *= mass;

            const glm::vec3 pos = glm::vec3(in.position) + glm::vec3(in.velocity) * velFactor * stepTime;
            const glm::vec3 vel = glm::vec3(in.velocity) + accel * stepTime;

            const float curStretchTime =
                (stretchTime > 0.0f) ? ((stretchTime > p.age) ? p.age : stretchTime) : 0.0f;

            p.position = glm::vec4(pos, 1.0f);
            p.velocity = glm::vec4(vel, 0.0f);

            if (stretchToStart) {
                p.stretchPosition = in.startPosition;
                p.rotation = in.rotation;
            } else if (curStretchTime > 0.0f) {
                const glm::vec4 acc4(accel, 0.0f);
                p.stretchPosition = p.position -
                                    (p.velocity - acc4 * (curStretchTime * 0.5f)) *
                                        (stretchTime * velFactor);
                p.rotation = in.rotation;
            } else {
                p.stretchPosition = p.position;
                p.rotation = in.rotation + in.rotationVariation * rotVel * stepTime;
            }

            p.color.x = samples[EmitterAttrs::Red];
            p.color.y = samples[EmitterAttrs::Green];
            p.color.z = samples[EmitterAttrs::Blue];
            p.color.w = samples[EmitterAttrs::Alpha];

            p.size = samples[EmitterAttrs::Size] * in.sizeVariation;

            liveCount++;
        }
    }

    numLiveParticles = liveCount;
}

void ParticleSystemInstanceBase::UpdateState(float time) {
    if (stateChangeMask != 0) {
        if ((stateChangeMask & ParticleSystemState::Playing) && !IsPlaying()) {
            curStepTime = time - ParticleSystem::StepTime;
            lastEmissionTime = 0.0f;
            timeSinceEmissionStart = 0.0f;
            firstEmissionFrame = true;
            numLiveParticles = 0;
            particles.Reset();
            stateMask = ParticleSystemState::Playing;
        }

        if ((stateChangeMask & ParticleSystemState::Stopped) && IsPlaying()) {
            stateMask |= ParticleSystemState::Stopping;
        }
    }

    if (IsStopping()) {
        if (numLiveParticles == 0) {
            stateMask = ParticleSystemState::Stopped;
        }
    }

    stateChangeMask = 0;
}
} // namespace Particles
