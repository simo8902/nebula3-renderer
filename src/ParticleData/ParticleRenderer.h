// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLERENDERER_H
#define NDEVC_PARTICLERENDERER_H

#include "glparticlerenderer.h"

namespace Particles {

class ParticleRenderer : public GLParticleRenderer {
public:
    static ParticleRenderer& Instance();

private:
    ParticleRenderer();
};

} // namespace Particles

#endif // NDEVC_PARTICLERENDERER_H
