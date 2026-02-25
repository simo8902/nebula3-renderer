// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLESYSTEMSTATE_H
#define NDEVC_PARTICLESYSTEMSTATE_H

#include <cstdint>

namespace Particles {

class ParticleSystemState {
public:
    enum Bits : uint16_t {
        Initial  = (1 << 0),
        Playing  = (1 << 1),
        Stopping = (1 << 2),
        Stopped  = (1 << 3),
    };
    using Mask = uint16_t;
};

} // namespace Particles

#endif // NDEVC_PARTICLESYSTEMSTATE_H
