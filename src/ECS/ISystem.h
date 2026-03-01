#pragma once
namespace NDEVC::ECS {
    struct ISystem { virtual ~ISystem() = default; virtual void Update(double dt) = 0; };
}
