#include "Core.h"

namespace NDEVC::Engine {

Core::Core() {
    EngineSystems::Config cfg;
    engineSystems = std::make_unique<EngineSystems>(cfg);
}

Core::~Core() = default;

Core& Core::Instance() {
    static Core instance;
    return instance;
}

}
