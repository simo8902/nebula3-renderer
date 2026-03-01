#pragma once
namespace NDEVC::Physics {
    struct IPhysicsWorld { virtual ~IPhysicsWorld() = default; virtual void StepSimulation(double dt) = 0; };
}
