#ifndef NDEVC_INITIALIZATION_H
#define NDEVC_INITIALIZATION_H

#include "Core.h"

class Initialization {
public:
    Initialization();
    ~Initialization();

    void RunMainLoop() const;

private:
    NDEVC::Engine::Core& core;
};

#endif
