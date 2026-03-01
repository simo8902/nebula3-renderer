// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "EnvelopeSampleBuffer.h"
#include "EmitterAttrs.h"

namespace Particles {

    EnvelopeSampleBuffer::~EnvelopeSampleBuffer() {
        if (IsValid()) {
            Discard();
        }
    }

    void EnvelopeSampleBuffer::Setup(const EmitterAttrs& emitterAttrs, size_t numSamp) {
        assert(!IsValid());
        numSamples = numSamp;

        size_t bufferSize = numSamples * EmitterAttrs::NumEnvelopeAttrs;
        buffer.resize(bufferSize);

        for (size_t i = 0; i < EmitterAttrs::NumEnvelopeAttrs; ++i) {
            float* samplePtr = buffer.data() + i;
            emitterAttrs.GetEnvelope(static_cast<EmitterAttrs::EnvelopeAttr>(i))
                .PreSample(samplePtr, numSamples, EmitterAttrs::NumEnvelopeAttrs);
        }
    }

    void EnvelopeSampleBuffer::Discard() {
        assert(IsValid());
        buffer.clear();
        buffer.shrink_to_fit();
        numSamples = 0;
    }

} // namespace Particles