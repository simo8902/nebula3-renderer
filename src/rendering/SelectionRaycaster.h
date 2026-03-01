// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_SELECTION_RAYCASTER_H
#define NDEVC_SELECTION_RAYCASTER_H

#include "glm.hpp"

struct DrawCmd;

class SelectionRaycaster {
public:
    static bool ComputeDrawBoundingSphereWS(const DrawCmd& draw,
                                            glm::vec3& centerWS,
                                            float& radiusWS);

    static bool RayIntersectsSphere(const glm::vec3& rayOrigin,
                                    const glm::vec3& rayDir,
                                    const glm::vec3& centerWS,
                                    float radiusWS,
                                    float& outT);
};

#endif