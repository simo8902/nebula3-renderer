// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MEGABUFFER_H
#define NDEVC_MEGABUFFER_H

#include "Rendering/OpenGL/GLHandles.h"
#include <cstdint>
#include <vector>

struct MegaRange {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
};

class MegaBuffer {
public:
    static MegaBuffer& instance();

    void build(const std::vector<struct ObjVertex>& allVerts,
               const std::vector<uint32_t>& allIndices);

    void bind() const;
    bool hasGLResources() const { return vao.valid(); }

    NDEVC::GL::GLVAOHandle vao;
    NDEVC::GL::GLBufHandle vbo;
    NDEVC::GL::GLBufHandle ebo;

private:
    MegaBuffer() = default;
};


#endif //NDEVC_MEGABUFFER_H
