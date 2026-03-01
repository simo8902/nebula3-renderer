// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLESYSTEMNODE_H
#define NDEVC_PARTICLESYSTEMNODE_H

#include "envelopecurve.h"
#include "glparticlesysteminstance.h"
#include "ParticleSystem.h"
#include "Assets/NDEVcStructure.h"
#include <memory>
#include "glad/glad.h"

#include "Rendering/Mesh.h"

namespace Particles {

    enum class ParticleBlendMode {
        Alpha = 0,
        Additive
    };

    class ParticleSystemNode {
    public:
        ParticleSystemNode();
        ~ParticleSystemNode();

        bool Setup(const Node* node, Mesh* mesh);
        void Discard();

        std::shared_ptr<GLParticleSystemInstance> GetInstance() { return instance; }
        void SetParticleTexture(GLuint tex) {
            particleTexture = tex;
            if (instance) instance->SetParticleTexture(tex);
        }
        GLuint GetParticleTexture() const { return particleTexture; }
        bool IsValid() const { return instance && instance->IsValid(); }
        ParticleBlendMode GetBlendMode() const { return blendMode; }
        float GetIntensity0() const { return intensity0; }
        float GetIntensity1() const { return intensity1; }
        float GetEmissiveIntensity() const { return emissiveIntensity; }
        int GetNumAnimPhases() const { return numAnimPhases; }
        float GetAnimFramesPerSecond() const { return animFramesPerSecond; }

    private:
        EnvelopeCurve ParseEnvelopeCurveArray(const Node* node, Mesh* mesh) const;
        std::shared_ptr<GLParticleSystemInstance> instance;
        std::shared_ptr<ParticleSystem> particleSystem;
        GLuint particleTexture = 0;
        ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
        float intensity0 = 1.0f;
        float intensity1 = 1.0f;
        float emissiveIntensity = 1.0f;
        int numAnimPhases = 1;
        float animFramesPerSecond = 0.0f;
    };

} // namespace Particles

#endif //NDEVC_PARTICLESYSTEMNODE_H
