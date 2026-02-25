// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "MegaBuffer.h"
#include "Mesh.h"  // for ObjVertex layout / offsetof

MegaBuffer& MegaBuffer::instance() {
    static MegaBuffer inst;
    return inst;
}

void MegaBuffer::shutdownGL() {
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ebo) {
        glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
}

void MegaBuffer::build(const std::vector<ObjVertex>& allVerts,
                        const std::vector<uint32_t>& allIndices) {
    shutdownGL();

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, allVerts.size() * sizeof(ObjVertex),
                 allVerts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size() * sizeof(uint32_t),
                 allIndices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, px));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, u0));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, w0));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, j0));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, u1));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, tx));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, bx));

    glBindVertexArray(0);
}

void MegaBuffer::bind() const {
    glBindVertexArray(vao);
}
