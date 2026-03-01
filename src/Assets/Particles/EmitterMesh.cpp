// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "emittermesh.h"
#include <algorithm>

namespace Particles {

EmitterMesh::~EmitterMesh() {
    if (IsValid()) {
        Discard();
    }
}

void EmitterMesh::Setup(const std::vector<uint8_t>& vertexData,
                        size_t vertexStride,
                        size_t posOffset,
                        size_t normOffset,
                        size_t tanOffset,
                        const std::vector<uint16_t>& indices,
                        uint32_t baseIndex,
                        uint32_t numIndices) {
    assert(!IsValid());

    if (vertexStride == 0) return;
    if (posOffset + sizeof(float) * 3 > vertexStride) return;
    if (normOffset + sizeof(float) * 3 > vertexStride) return;
    if (tanOffset + sizeof(float) * 3 > vertexStride) return;
    if (baseIndex >= indices.size()) return;

    const size_t numVBufferVertices = vertexData.size() / vertexStride;
    if (numVBufferVertices == 0) return;

    const uint64_t requestedEnd = static_cast<uint64_t>(baseIndex) + static_cast<uint64_t>(numIndices);
    const uint32_t safeEnd = static_cast<uint32_t>(std::min<uint64_t>(requestedEnd, indices.size()));

    std::vector<bool> flagArray(numVBufferVertices, false);
    std::vector<uint16_t> emitterIndices;
    emitterIndices.reserve(numVBufferVertices);

    for (uint32_t i = baseIndex; i < safeEnd; ++i) {
        uint16_t vertexIndex = indices[i];
        if (vertexIndex >= numVBufferVertices) continue;

        if (!flagArray[vertexIndex]) {
            emitterIndices.push_back(vertexIndex);
            flagArray[vertexIndex] = true;
        }
    }

    const size_t numPoints = emitterIndices.size() / VertexWidth;
    if (numPoints == 0) return;
    points.resize(numPoints);

    for (size_t i = 0; i < numPoints; ++i) {
        const uint8_t* src = vertexData.data() + vertexStride * emitterIndices[i];
        EmitterPoint& dst = points[i];

        const float* posPtr = reinterpret_cast<const float*>(src + posOffset);
        dst.position = glm::vec4(posPtr[0], posPtr[1], posPtr[2], 1.0f);

        const float* normPtr = reinterpret_cast<const float*>(src + normOffset);
        glm::vec3 n(normPtr[0], normPtr[1], normPtr[2]);
        const float nlen = glm::length(n);
        if (nlen > 0.0f) n /= nlen;
        dst.normal = glm::vec4(n, 0.0f);

        const float* tanPtr = reinterpret_cast<const float*>(src + tanOffset);
        glm::vec3 t(tanPtr[0], tanPtr[1], tanPtr[2]);
        const float tlen = glm::length(t);
        if (tlen > 0.0f) t /= tlen;
        dst.tangent = glm::vec4(t, 0.0f);
    }
}

void EmitterMesh::Discard() {
    assert(IsValid());
    points.clear();
    points.shrink_to_fit();
}

const EmitterMesh::EmitterPoint& EmitterMesh::GetEmitterPoint(size_t key) const {
    assert(!points.empty());
    return points[key % points.size()];
}

} // namespace Particles
