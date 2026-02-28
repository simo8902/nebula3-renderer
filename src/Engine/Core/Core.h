#ifndef NDEVC_CORE_H
#define NDEVC_CORE_H

#include "EngineSystems.h"
#include <memory>

namespace NDEVC::Engine {

class Core {
public:
    static Core& Instance();

    EngineSystems& Systems() const { return *engineSystems; }

private:
    Core();
    ~Core();

    std::unique_ptr<EngineSystems> engineSystems;
};

}

#endif
