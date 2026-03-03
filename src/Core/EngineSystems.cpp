#include "Core/EngineSystems.h"
#include "Core/Logger.h"

#include "Rendering/Interfaces/IRenderer.h"
#include "Rendering/Rendering.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
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
    uint64_t frameIndex = 0;
    bool stopRequested = false;
    std::vector<InputSystemFn> inputSystems;
    std::vector<UpdateSystemFn> fixedUpdateSystems;
    std::vector<UpdateSystemFn> frameUpdateSystems;

    explicit Impl(const Config& cfg)
        : config(cfg), renderer(Rendering::CreateDefaultRenderer()) {
        NC::LOGGING::Log("[ENGINE] Impl ctor begin fixedStep=", config.fixedUpdateStepSeconds,
                         " maxFrameDelta=", config.maxFrameDeltaSeconds,
                         " maxFixedUpdates=", config.maxFixedUpdatesPerFrame);
        NC::LOGGING::Log("[ENGINE] Renderer create ", (renderer ? "ok" : "null"));
        renderer->Initialize();


		const GLubyte* version  = glGetString(GL_VERSION);
		const GLubyte* renderer = glGetString(GL_RENDERER);
		const GLubyte* vendor   = glGetString(GL_VENDOR);

		std::cout << "GL_VERSION  : " << (version  ? (const char*)version  : "null") << '\n';
		std::cout << "GL_RENDERER : " << (renderer ? (const char*)renderer : "null") << '\n';
		std::cout << "GL_VENDOR   : " << (vendor   ? (const char*)vendor   : "null") << '\n';

        NC::LOGGING::Log("[ENGINE] Renderer initialize done");
        lastFrameTime = Clock::now();
        runtimeState = RuntimeState::Running;
        NC::LOGGING::Log("[ENGINE] Runtime state -> Running");

        if (glfwExtensionSupported("GL_ARB_bindless_texture"))
            printf("[GPU] GL_ARB_bindless_texture: SUPPORTED\n");
        else
            printf("[GPU] GL_ARB_bindless_texture: NOT SUPPORTED\n");
    }

    ~Impl() {
        NC::LOGGING::Log("[ENGINE] Impl dtor begin");
        runtimeState = RuntimeState::Stopping;
        if (renderer) {
            renderer->Shutdown();
            NC::LOGGING::Log("[ENGINE] Renderer shutdown in Impl dtor");
        }
        runtimeState = RuntimeState::Stopped;
        NC::LOGGING::Log("[ENGINE] Runtime state -> Stopped");
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
        ++frameIndex;
        // NC::LOGGING::Log("[ENGINE] BeginFrame idx=", frameIndex,
        //                  " dt=", frameDeltaTime,
        //                  " accumulator=", fixedUpdateAccumulator);
    }

    void PumpInput() {
        if (renderer) {
            renderer->PollEvents();
        }
        // NC::LOGGING::Log("[ENGINE] PumpInput systems=", inputSystems.size());
        for (const auto& system : inputSystems) {
            if (system) {
                system();
            }
        }
        if (!renderer || stopRequested || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
            NC::LOGGING::Warning("[ENGINE] PumpInput requested stop renderer=", (renderer ? 1 : 0),
                                 " stopRequested=", (stopRequested ? 1 : 0),
                                 " shouldClose=", (renderer && renderer->ShouldClose()) ? 1 : 0);
        }
    }

    void FixedUpdate(double dt) {
        // NC::LOGGING::Log("[ENGINE] FixedUpdate begin dt=", dt, " systems=", fixedUpdateSystems.size());
        for (const auto& system : fixedUpdateSystems) {
            if (system) {
                system(dt);
            }
        }
        if (!renderer || stopRequested || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
            NC::LOGGING::Warning("[ENGINE] FixedUpdate requested stop");
        }
        // NC::LOGGING::Log("[ENGINE] FixedUpdate end dt=", dt);
    }

    void FrameUpdate(double dt) {
        // NC::LOGGING::Log("[ENGINE] FrameUpdate begin dt=", dt, " systems=", frameUpdateSystems.size());
        for (const auto& system : frameUpdateSystems) {
            if (system) {
                system(dt);
            }
        }
        if (!renderer || stopRequested || renderer->ShouldClose()) {
            runtimeState = RuntimeState::Stopping;
            NC::LOGGING::Warning("[ENGINE] FrameUpdate requested stop");
        }
        // NC::LOGGING::Log("[ENGINE] FrameUpdate end dt=", dt);
    }

    void Update() {
        if (runtimeState != RuntimeState::Running) {
            NC::LOGGING::Warning("[ENGINE] Update skipped state=", static_cast<int>(runtimeState));
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

        // NC::LOGGING::Log("[ENGINE] Update fixedSteps=", fixedStepCount,
        //                  " accumulator=", fixedUpdateAccumulator,
        //                  " frameDt=", frameDeltaTime);
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
            NC::LOGGING::Warning("[ENGINE] EndFrame state -> Stopping");
        }
    }
};

EngineSystems::EngineSystems(const Config& config)
    : pImpl(std::make_unique<Impl>(config)) {
    NC::LOGGING::Log("[ENGINE] EngineSystems ctor");
}

EngineSystems::~EngineSystems() {
    NC::LOGGING::Log("[ENGINE] EngineSystems dtor");
}

NDEVC::Graphics::IRenderer* EngineSystems::GetRenderer() const {
    return pImpl->renderer.get();
}

SceneManager& EngineSystems::GetScene() const {
    return pImpl->renderer->GetScene();
}

EngineSystems::RuntimeState EngineSystems::GetRuntimeState() const {
    return pImpl->runtimeState;
}

void EngineSystems::RequestStop() const {
    pImpl->stopRequested = true;
    NC::LOGGING::Warning("[ENGINE] RequestStop");
    if (pImpl->runtimeState == RuntimeState::Running) {
        pImpl->runtimeState = RuntimeState::Stopping;
        NC::LOGGING::Warning("[ENGINE] Runtime state -> Stopping (RequestStop)");
    }
}

void EngineSystems::AddInputSystem(InputSystemFn system) {
    pImpl->inputSystems.push_back(std::move(system));
    NC::LOGGING::Log("[ENGINE] AddInputSystem total=", pImpl->inputSystems.size());
}

void EngineSystems::AddFixedUpdateSystem(UpdateSystemFn system) {
    pImpl->fixedUpdateSystems.push_back(std::move(system));
    NC::LOGGING::Log("[ENGINE] AddFixedUpdateSystem total=", pImpl->fixedUpdateSystems.size());
}

void EngineSystems::AddFrameUpdateSystem(UpdateSystemFn system) {
    pImpl->frameUpdateSystems.push_back(std::move(system));
    NC::LOGGING::Log("[ENGINE] AddFrameUpdateSystem total=", pImpl->frameUpdateSystems.size());
}

void EngineSystems::ClearSystems() {
    pImpl->inputSystems.clear();
    pImpl->fixedUpdateSystems.clear();
    pImpl->frameUpdateSystems.clear();
    NC::LOGGING::Log("[ENGINE] ClearSystems");
}

void EngineSystems::RunOneFrame() const {
    if (pImpl->runtimeState == RuntimeState::Stopped) {
        NC::LOGGING::Warning("[ENGINE] RunOneFrame skipped state=Stopped");
        return;
    }
    if (pImpl->runtimeState == RuntimeState::Booting) {
        pImpl->lastFrameTime = Impl::Clock::now();
        pImpl->runtimeState = RuntimeState::Running;
        NC::LOGGING::Log("[ENGINE] Runtime state Booting -> Running");
    }
    if (pImpl->runtimeState != RuntimeState::Running) {
        NC::LOGGING::Warning("[ENGINE] RunOneFrame skipped state=", static_cast<int>(pImpl->runtimeState));
        return;
    }

    pImpl->PumpInput();
    if (pImpl->runtimeState != RuntimeState::Running) {
        if (pImpl->runtimeState == RuntimeState::Stopping) {
            pImpl->runtimeState = RuntimeState::Stopped;
            NC::LOGGING::Log("[ENGINE] Runtime state Stopping -> Stopped (after PumpInput)");
        }
        return;
    }

    pImpl->BeginFrame();
    pImpl->Update();
    pImpl->Render();
    pImpl->EndFrame();

    if (pImpl->runtimeState == RuntimeState::Stopping) {
        pImpl->runtimeState = RuntimeState::Stopped;
        NC::LOGGING::Log("[ENGINE] Runtime state Stopping -> Stopped (end frame)");
    }
}

void EngineSystems::RunMainLoop() const {
    NC::LOGGING::Log("[ENGINE] RunMainLoop begin");
    auto shutdownRendererNow = [this]() {
        if (!pImpl->renderer) {
            return;
        }
        pImpl->runtimeState = RuntimeState::Stopping;
        NC::LOGGING::Log("[ENGINE] shutdownRendererNow begin");
        pImpl->renderer->Shutdown();
        pImpl->renderer.reset();
        pImpl->runtimeState = RuntimeState::Stopped;
        NC::LOGGING::Log("[ENGINE] shutdownRendererNow end");
    };

    try {
        while (pImpl->runtimeState != RuntimeState::Stopped) {
            RunOneFrame();
        }
        shutdownRendererNow();
        NC::LOGGING::Log("[ENGINE] RunMainLoop end");
    } catch (const std::exception& e) {
        shutdownRendererNow();
        NC::LOGGING::Error("[ENGINE] Fatal loop error: ", e.what());
        throw;
    }
}

}
