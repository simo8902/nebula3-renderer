// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ENVELOPECURVE_H
#define NDEVC_ENVELOPECURVE_H

#include <cmath>
#include <algorithm>

constexpr float N_PI = 3.14159265359f;

namespace Particles {

    class EnvelopeCurve {
    public:
        enum ModFunc {
            Sine = 0,
            Cosine = 1
        };

        EnvelopeCurve();

        void Setup(float val0, float val1, float val2, float val3,
                   float keyp0, float keyp1, float freq, float amp, ModFunc mod);

        float Sample(float t) const;
        void PreSample(float* sampleBuffer, size_t numSamples, size_t sampleStride) const;
        float GetMaxValue() const;

    private:
        float values[4];
        float keyPos0;
        float keyPos1;
        float frequency;
        float amplitude;
        ModFunc modFunction;
    };

} // namespace Particles

#endif //NDEVC_ENVELOPECURVE_H