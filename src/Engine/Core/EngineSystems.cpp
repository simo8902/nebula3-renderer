#include "EngineSystems.h"

#include "../../DeferredRenderer.h"
#include "../../rendering/abstract/IRenderer.h"
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <utility>
#include <vector>

namespace NDEVC::Engine {

struct EngineSystems::Impl {
    using Clock = std::chrono::steady_clock;

    Config config;
    std::unique_ptr<NDEVC::Graphics::IRenderer> renderer;
    RuntimeState runtimeState = RuntimeState::Booting;
    Clock::time_point lastFrameTime = Clock::time_point{};
    double frameDeltaTime = 0.0;
    double fixedUpdateAccumulator = 0.0;
    bool stopRequested = false;
    std::vector<InputSystemFn> inputSystems;
    std::vector<UpdateSystemFn> fixedUpdateSystems;
    std::vector<UpdateSystemFn> frameUpdateSystems;

    explicit Impl(const Config& cfg)
        : config(cfg), renderer(std::make_unique<DeferredRenderer>()) {
        renderer->Initialize();
        lastFrameTime = Clock::now();
        runtimeState = RuntimeState::Running;
    }

    ~Impl() {
        runtimeState = RuntimeState::Stopping;
        if (renderer) {
            renderer->Shutdown();
        }
        runtimeState = RuntimeState::Stopped;
    }

    void BeginFrame() {
        const Clock::time_point now = Clock::now();
        if (lastFrameTime.time_since_epoch().count() == 0) {
            frameDeltaTime = config.fixedUpdateStepSeconds;
        } else {
            frameDeltaTime = std::chrono::duration<double>(now - lastFrameTime).count();
            if (frameDeltaTime < 0.0 || !std::isfinite(frameDeltaTime)) frameDeltaTime = config.fixedUpdateStepSeconds;
            if (frameDeltaTime > config.maxFrameDeltaSeconds) frameDeltaTime = config.maxFrameDeltaSeconds;
        }
        lastFrameTime = now;
        fixedUpdateAccumulator += frameDeltaTime;
    }

    void PumpInput() {
        if (renderer) {
            renderer->PollEvents();
        }
        for (const auto& system : inputSystems) {
            if (system) {
                system();
            }
        }
        if (!renderer || stopRequested || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
        }
    }

    void FixedUpdate(double dt) {
        for (const auto& system : fixedUpdateSystems) {
            if (system) {
                system(dt);
            }
        }
        if (!renderer || stopRequested || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
        }
    }

    void FrameUpdate(double dt) {
        for (const auto& system : frameUpdateSystems) {
            if (system) {
                system(dt);
            }
        }
        if (!renderer || stopRequested || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
        }
    }

    void Update() {
        if (runtimeState != RuntimeState::Running) {
            return;
        }

        int fixedStepCount = 0;
        while (fixedUpdateAccumulator >= config.fixedUpdateStepSeconds &&
               fixedStepCount < config.maxFixedUpdatesPerFrame &&
               runtimeState == RuntimeState::Running) {
            FixedUpdate(config.fixedUpdateStepSeconds);
            fixedUpdateAccumulator -= config.fixedUpdateStepSeconds;
            ++fixedStepCount;
        }

        if (fixedStepCount == config.maxFixedUpdatesPerFrame &&
            fixedUpdateAccumulator > config.fixedUpdateStepSeconds) {
            fixedUpdateAccumulator = config.fixedUpdateStepSeconds;
        }

        FrameUpdate(frameDeltaTime);
    }

    void Render() {
        if (runtimeState == RuntimeState::Running && renderer) {
            renderer->RenderSingleFrame();
        }
    }

    void EndFrame() {
        if (!renderer || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
        }
    }
};

EngineSystems::EngineSystems(const Config& config)
    : pImpl(std::make_unique<Impl>(config)) {
}

EngineSystems::~EngineSystems() = default;

NDEVC::Graphics::IRenderer* EngineSystems::GetRenderer() const {
    return pImpl->renderer.get();
}

EngineSystems::RuntimeState EngineSystems::GetRuntimeState() const {
    return pImpl->runtimeState;
}

void EngineSystems::RequestStop() const {
    pImpl->stopRequested = true;
    if (pImpl->runtimeState == RuntimeState::Running) {
        pImpl->runtimeState = RuntimeState::Stopping;
    }
}

void EngineSystems::AddInputSystem(InputSystemFn system) {
    pImpl->inputSystems.push_back(std::move(system));
}

void EngineSystems::AddFixedUpdateSystem(UpdateSystemFn system) {
    pImpl->fixedUpdateSystems.push_back(std::move(system));
}

void EngineSystems::AddFrameUpdateSystem(UpdateSystemFn system) {
    pImpl->frameUpdateSystems.push_back(std::move(system));
}

void EngineSystems::ClearSystems() {
    pImpl->inputSystems.clear();
    pImpl->fixedUpdateSystems.clear();
    pImpl->frameUpdateSystems.clear();
}

void EngineSystems::RunOneFrame() const {
    if (pImpl->runtimeState == RuntimeState::Stopped) {
        return;
    }
    if (pImpl->runtimeState == RuntimeState::Booting) {
        pImpl->lastFrameTime = Impl::Clock::now();
        pImpl->runtimeState = RuntimeState::Running;
    }
    if (pImpl->runtimeState != RuntimeState::Running) {
        return;
    }

    pImpl->PumpInput();
    if (pImpl->runtimeState != RuntimeState::Running) {
        if (pImpl->runtimeState == RuntimeState::Stopping) {
            pImpl->runtimeState = RuntimeState::Stopped;
        }
        return;
    }

    pImpl->BeginFrame();
    pImpl->Update();
    pImpl->Render();
    pImpl->EndFrame();

    if (pImpl->runtimeState == RuntimeState::Stopping) {
        pImpl->runtimeState = RuntimeState::Stopped;
    }
}

void EngineSystems::RunMainLoop() const {
    try {
        while (pImpl->runtimeState != RuntimeState::Stopped) {
            RunOneFrame();
        }
    } catch (const std::exception& e) {
        pImpl->runtimeState = RuntimeState::Stopping;
        std::cerr << "Fatal loop error: " << e.what() << "\n";
        pImpl->runtimeState = RuntimeState::Stopped;
        throw;
    }
}

}
