// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EMITTERMESH_H
#define NDEVC_EMITTERMESH_H

#include "glm.hpp"
#include <vector>
#include <cstdint>
#include <cassert>

namespace Particles {

    class EmitterMesh {
    public:
        struct EmitterPoint {
            glm::vec4 position;
            glm::vec4 normal;
            glm::vec4 tangent;
        };

        EmitterMesh() = default;
        ~EmitterMesh();

        void Setup(const std::vector<uint8_t>& vertexData,
                   size_t vertexStride,
                   size_t posOffset,
                   size_t normOffset,
                   size_t tanOffset,
                   const std::vector<uint16_t>& indices,
                   uint32_t baseIndex,
                   uint32_t numIndices);

        void Discard();
        bool IsValid() const { return !points.empty(); }

        size_t GetNumPoints() const { return points.size(); }
        const EmitterPoint& GetEmitterPoint(size_t key) const;

    private:
        static constexpr size_t VertexWidth = 3;
        std::vector<EmitterPoint> points;
    };
}

#endif //NDEVC_EMITTERMESH_H
