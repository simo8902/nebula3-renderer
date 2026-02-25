// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLESERVER_H
#define NDEVC_PARTICLESERVER_H

#include "ParticleRenderer.h"
#include "rendering/abstract/IShaderManager.h"
#include <iostream>

namespace Particles {

    class ParticleServer {
    public:
        static ParticleServer& Instance() {
            static ParticleServer instance;
            return instance;
        }

        ParticleServer(const ParticleServer&) = delete;
        ParticleServer& operator=(const ParticleServer&) = delete;

        void Open(NDEVC::Graphics::IShaderManager* sharedShaderManager = nullptr) {
            if (isOpen) return;

            ParticleRenderer::Instance().Setup(sharedShaderManager);

            if (!ParticleRenderer::Instance().IsValid()) {
                std::cerr << "[ParticleServer] Failed to setup particle renderer\n";
                return;
            }

            isOpen = true;
            std::cout << "[ParticleServer] Opened successfully\n";
        }

        void Close() {
            if (!isOpen) return;

            ParticleRenderer::Instance().Discard();

            isOpen = false;
            std::cout << "[ParticleServer] Closed\n";
        }

        bool IsOpen() const { return isOpen; }

        ParticleRenderer* GetParticleRenderer() { return isOpen ? &ParticleRenderer::Instance() : nullptr; }
        const ParticleRenderer* GetParticleRenderer() const { return isOpen ? &ParticleRenderer::Instance() : nullptr; }

        void BeginAttach() {
            if (!isOpen) return;
            ParticleRenderer::Instance().BeginAttach();
        }
        void EndAttach() {
            if (!isOpen) return;
            ParticleRenderer::Instance().EndAttach();
        }

    private:
        ParticleServer() = default;

        ~ParticleServer() {
            Close();
        }

        bool isOpen = false;
    };

} // namespace Particles


#endif //NDEVC_PARTICLESERVER_H
