#pragma once
#include <cstdint>
namespace NDEVC::ECS {
    using EntityID = uint64_t;
    static constexpr EntityID InvalidEntity = 0;
    class EntityRegistry; // forward declaration — implementation is future work
}
