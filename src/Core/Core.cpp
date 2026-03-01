#include "Core/Core.h"
#include "Core/Logger.h"

namespace NDEVC::Engine {

Core::Core() {
    NC::LOGGING::Log("[CORE] Core ctor begin");
    EngineSystems::Config cfg;
    engineSystems = std::make_unique<EngineSystems>(cfg);
    NC::LOGGING::Log("[CORE] Core ctor end");
}

Core::~Core() {
    NC::LOGGING::Log("[CORE] Core dtor");
}

Core& Core::Instance() {
    NC::LOGGING::Log("[CORE] Core::Instance");
    static Core instance;
    return instance;
}

}
