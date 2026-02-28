#include "Initialization.h"

Initialization::Initialization()
    : core(NDEVC::Engine::Core::Instance()) {
}

Initialization::~Initialization() = default;

void Initialization::RunMainLoop() const {
    core.Systems().RunMainLoop();
}
