#include "Core/Initialization.h"
#include "Core/Logger.h"

Initialization::Initialization()
    : core(NDEVC::Engine::Core::Instance()) {
    NC::LOGGING::Log("[INIT] Initialization ctor");
}

Initialization::~Initialization() {
    NC::LOGGING::Log("[INIT] Initialization dtor");
}

void Initialization::RunMainLoop() const {
    NC::LOGGING::Log("[INIT] RunMainLoop begin");
    core.Systems().RunMainLoop();
    NC::LOGGING::Log("[INIT] RunMainLoop end");
}
