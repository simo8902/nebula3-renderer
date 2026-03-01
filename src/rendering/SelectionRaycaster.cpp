// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/SelectionRaycaster.h"

#include "glad/glad.h"
#include "Rendering/DrawCmd.h"

#include <algorithm>
#include <cmath>

namespace {

bool IsFiniteVec3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool ResolveLocalBoundsForPick(const DrawCmd& dc, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(dc.localBoxMin);
    outMax = glm::vec3(dc.localBoxMax);
    if (!IsFiniteVec3(outMin) || !IsFiniteVec3(outMax)) return false;

    const glm::vec3 rawMin = outMin;
    const glm::vec3 rawMax = outMax;
    outMin = glm::min(rawMin, rawMax);
    outMax = glm::max(rawMin, rawMax);

    const glm::vec3 extent = outMax - outMin;
    if (glm::dot(extent, extent) < 1e-8f) {
        if (!dc.mesh) return false;
        outMin = dc.mesh->boundingBoxMin;
        outMax = dc.mesh->boundingBoxMax;
        if (!IsFiniteVec3(outMin) || !IsFiniteVec3(outMax)) return false;
        const glm::vec3 meshMin = outMin;
        const glm::vec3 meshMax = outMax;
        outMin = glm::min(meshMin, meshMax);
        outMax = glm::max(meshMin, meshMax);
    }

    return true;
}

} // namespace

bool SelectionRaycaster::ComputeDrawBoundingSphereWS(const DrawCmd& draw,
                                                     glm::vec3& centerWS,
                                                     float& radiusWS) {
    glm::vec3 localMin(0.0f);
    glm::vec3 localMax(0.0f);
    if (!ResolveLocalBoundsForPick(draw, localMin, localMax)) return false;

    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const glm::vec3 localHalfExtents = (localMax - localMin) * 0.5f;
    centerWS = glm::vec3(draw.worldMatrix * glm::vec4(localCenter, 1.0f));
    if (!IsFiniteVec3(centerWS)) return false;

    const glm::vec3 axisX(draw.worldMatrix[0][0], draw.worldMatrix[0][1], draw.worldMatrix[0][2]);
    const glm::vec3 axisY(draw.worldMatrix[1][0], draw.worldMatrix[1][1], draw.worldMatrix[1][2]);
    const glm::vec3 axisZ(draw.worldMatrix[2][0], draw.worldMatrix[2][1], draw.worldMatrix[2][2]);
    const float maxScale = std::max(glm::length(axisX), std::max(glm::length(axisY), glm::length(axisZ)));
    radiusWS = glm::length(localHalfExtents) * std::max(1e-4f, maxScale);
    return std::isfinite(radiusWS) && radiusWS > 1e-5f;
}

bool SelectionRaycaster::RayIntersectsSphere(const glm::vec3& rayOrigin,
                                             const glm::vec3& rayDir,
                                             const glm::vec3& centerWS,
                                             float radiusWS,
                                             float& outT) {
    const glm::vec3 oc = rayOrigin - centerWS;
    const float b = glm::dot(oc, rayDir);
    const float c = glm::dot(oc, oc) - radiusWS * radiusWS;
    const float disc = b * b - c;
    if (disc < 0.0f) return false;

    const float sqrtDisc = std::sqrt(std::max(0.0f, disc));
    float t = -b - sqrtDisc;
    if (t < 0.0f) t = -b + sqrtDisc;
    if (t < 0.0f) return false;

    outT = t;
    return true;
}
