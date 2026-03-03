// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GLPARTICLESYSTEMINSTANCE_H
#define NDEVC_GLPARTICLESYSTEMINSTANCE_H

#include "particlesysteminstancebase.h"
#include <memory>
#include "glad/glad.h"

namespace Particles {

    class GLParticleRenderer;

    class GLParticleSystemInstance : public ParticleSystemInstanceBase {
    public:
        GLParticleSystemInstance();
        ~GLParticleSystemInstance() = default;

        void SetRenderer(GLParticleRenderer* renderer) { this->renderer = renderer; }
        GLParticleRenderer* GetRenderer() { return renderer; }

        void UpdateVertexStreams() override;
        void Render(const glm::mat4& viewProj, const glm::mat4& view, const glm::mat4& invView,
                    const glm::vec3& eyePos, const glm::vec2& fogDistances, const glm::vec4& fogColor,
                    uint32_t gPositionWSTex, const glm::vec2& invViewport,
                    int numAnimPhases, float animFramesPerSecond, float time, int colorMode,
                    float intensity0, float emissiveIntensity) override;
        void SetParticleTexture(GLuint tex);
    private:
        GLParticleRenderer* renderer = nullptr;
        GLuint particleTexture = 0;
        uint32_t numParticlesThisFrame = 0;
        uint32_t baseVertexIndex = 0;

    };

} // namespace Particles

#endif //NDEVC_GLPARTICLESYSTEMINSTANCE_H
