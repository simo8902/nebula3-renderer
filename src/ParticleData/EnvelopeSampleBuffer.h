// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ENVELOPESAMPLEBUFFER_H
#define NDEVC_ENVELOPESAMPLEBUFFER_H

#include "EmitterAttrs.h"
#include <vector>
#include <cstddef>

namespace Particles {

    class EmitterAttrs; // forward decl

    class EnvelopeSampleBuffer {
    public:
        EnvelopeSampleBuffer() = default;
        ~EnvelopeSampleBuffer();

        void Setup(const EmitterAttrs& emitterAttrs, size_t numSamp);
        void Discard();
        bool IsValid() const { return !buffer.empty(); }

        float* GetBuffer() { return buffer.data(); }
        const float* GetBuffer() const { return buffer.data(); }
        size_t GetNumSamples() const { return numSamples; }
        float* LookupSamples(size_t sampleIndex) { return buffer.data() + (sampleIndex * EmitterAttrs::NumEnvelopeAttrs); }
        const float* LookupSamples(size_t sampleIndex) const { return buffer.data() + (sampleIndex * EmitterAttrs::NumEnvelopeAttrs); }

    private:
        size_t numSamples = 0;
        std::vector<float> buffer;
    };

} // namespace Particles

#endif //NDEVC_ENVELOPESAMPLEBUFFER_H
