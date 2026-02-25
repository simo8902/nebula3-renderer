// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GLPARTICLERENDERER_H
#define NDEVC_GLPARTICLERENDERER_H

#include "particle.h"
#include "src/rendering/abstract/IShaderManager.h"
#include "src/rendering/abstract/IShader.h"
#include "glad/glad.h"
#include "glm.hpp"
#include <memory>
#include <cstdint>

namespace Particles {

    struct ParticleVertexData {
        glm::vec4 position;
        glm::vec4 stretchPosition;
        glm::vec4 color;
        glm::vec4 uvMinMax;
        glm::vec4 rotSizeId;
    };

    class GLParticleRenderer {
    public:
        GLParticleRenderer();
        ~GLParticleRenderer();

        void Setup(NDEVC::Graphics::IShaderManager* sharedShaderManager = nullptr);
        void Discard();

        void BeginAttach();
        void EndAttach();

        uint32_t GetCurParticleVertexIndex() const { return curParticleVertexIndex; }
        void AddCurParticleVertexIndex(uint32_t add) { curParticleVertexIndex += add; }

        void* GetCurVertexPtr() { return curVertexPtr; }
        void SetCurVertexPtr(void* ptr) { curVertexPtr = ptr; }

        GLuint GetParticleVBO() const { return particleVBO; }
        GLuint GetCornerVBO() const { return cornerVBO; }
        GLuint GetCornerIBO() const { return cornerIBO; }
        GLuint GetVAO() const { return vao; }

        void Render(const glm::mat4& viewProj, const glm::mat4& view, const glm::mat4& invView,
                    const glm::vec3& eyePos, const glm::vec2& fogDistances, const glm::vec4& fogColor,
                    const glm::mat4& emitterTransform, uint32_t numParticles, uint32_t baseInstance,
                    GLuint texture, GLuint gPositionVSTex, const glm::vec2& invViewport,
                    int numAnimPhases, float animFramesPerSecond, float time, bool billboard,
                    int colorMode, float intensity0, float emissiveIntensity);

        bool IsValid() const { return valid; }
        bool IsInAttach() const { return inAttach; }

    private:
        void CreateCornerMesh();
        void CreateParticleMesh();
        void SetupVertexAttributes();
        GLuint vao = 0;
        GLuint cornerVBO = 0;
        GLuint particleVBO = 0;
        GLuint cornerIBO = 0;
        GLuint sampler = 0;

        uint8_t* mappedParticleBuffer = nullptr;
        void* curVertexPtr = nullptr;
        uint32_t curParticleVertexIndex = 0;

        NDEVC::Graphics::IShaderManager* shaderManager = nullptr;
        bool ownsShaderManager = false;
        std::shared_ptr<NDEVC::Graphics::IShader> particleShader;

        bool valid = false;
        bool inAttach = false;
    };

} // namespace Particles

#endif //NDEVC_GLPARTICLERENDERER_H
