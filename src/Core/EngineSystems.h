#ifndef NDEVC_ENGINE_SYSTEMS_H
#define NDEVC_ENGINE_SYSTEMS_H

#include <functional>
#include <memory>

class SceneManager;

namespace NDEVC {
namespace Graphics {
class IRenderer;
}

namespace Engine {

class EngineSystems {
public:
    using InputSystemFn = std::function<void()>;
    using UpdateSystemFn = std::function<void(double)>;

    enum class RuntimeState {
        Booting,
        Running,
        Stopping,
        Stopped
    };

    struct Config {
        double fixedUpdateStepSeconds = 1.0 / 60.0;
        int maxFixedUpdatesPerFrame = 5;
        double maxFrameDeltaSeconds = 1.0 / 30.0;  // 33ms max, prevents 10x frame time variance
    };

    explicit EngineSystems(const Config& config);
    ~EngineSystems();

    NDEVC::Graphics::IRenderer* GetRenderer() const;
    SceneManager& GetScene() const;
    RuntimeState GetRuntimeState() const;
    void RequestStop() const;
    void AddInputSystem(InputSystemFn system);
    void AddFixedUpdateSystem(UpdateSystemFn system);
    void AddFrameUpdateSystem(UpdateSystemFn system);
    void ClearSystems();
    void RunOneFrame() const;
    void RunMainLoop() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

}
}

#endif
