// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "ParticleRenderer.h"

namespace Particles {

ParticleRenderer::ParticleRenderer() = default;

ParticleRenderer& ParticleRenderer::Instance() {
    static ParticleRenderer instance;
    return instance;
}

} // namespace Particles
