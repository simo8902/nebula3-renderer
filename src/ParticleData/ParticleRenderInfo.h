// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLERENDERINFO_H
#define NDEVC_PARTICLERENDERINFO_H

#include <cstddef>

namespace Particles {

class ParticleRenderInfo {
public:
    ParticleRenderInfo() = default;
    ParticleRenderInfo(size_t base, size_t num) : baseVertexIndex(base), numVertices(num) {}

    void Clear() { baseVertexIndex = 0; numVertices = 0; }
    bool IsEmpty() const { return numVertices == 0; }

    size_t GetBaseVertexIndex() const { return baseVertexIndex; }
    size_t GetNumVertices() const { return numVertices; }

private:
    size_t baseVertexIndex = 0;
    size_t numVertices = 0;
};

} // namespace Particles

#endif // NDEVC_PARTICLERENDERINFO_H
