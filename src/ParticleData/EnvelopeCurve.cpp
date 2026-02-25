// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "EnvelopeCurve.h"
#include "glm.hpp"

namespace Particles {

    EnvelopeCurve::EnvelopeCurve() :
        keyPos0(0.25f),
        keyPos1(0.75f),
        frequency(0.0f),
        amplitude(0.0f),
        modFunction(Sine) {
        values[0] = 0.0f;
        values[1] = 0.0f;
        values[2] = 0.0f;
        values[3] = 0.0f;
    }

    void EnvelopeCurve::Setup(float val0, float val1, float val2, float val3,
                              float keyp0, float keyp1, float freq, float amp, ModFunc mod) {
        values[0] = val0;
        values[1] = val1;
        values[2] = val2;
        values[3] = val3;
        keyPos0 = keyp0;
        keyPos1 = keyp1;
        frequency = freq;
        amplitude = amp;
        modFunction = mod;
    }

    float EnvelopeCurve::Sample(float t) const {
        t = glm::clamp(t, 0.0f, 1.0f);

        float value;
        if (t < keyPos0) {
            value = glm::mix(values[0], values[1], t / keyPos0);
        } else if (t < keyPos1) {
            value = glm::mix(values[1], values[2], (t - keyPos0) / (keyPos1 - keyPos0));
        } else {
            value = glm::mix(values[2], values[3], (t - keyPos1) / (1.0f - keyPos1));
        }

        if (amplitude > 0.0f) {
            if (modFunction == Sine) {
                value += std::sin(t * 2.0f * N_PI * frequency) * amplitude;
            } else {
                value += std::cos(t * 2.0f * N_PI * frequency) * amplitude;
            }
        }
        return value;
    }

    void EnvelopeCurve::PreSample(float* sampleBuffer, size_t numSamples, size_t sampleStride) const {
        float t = 0.0f;
        float d = 1.0f / numSamples;
        for (size_t i = 0; i < numSamples; ++i) {
            sampleBuffer[i * sampleStride] = Sample(t);
            t += d;
        }
    }

    float EnvelopeCurve::GetMaxValue() const {
        return std::max({values[0], values[1], values[2], values[3]});
    }

} // namespace Particles