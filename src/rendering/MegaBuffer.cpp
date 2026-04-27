// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/MegaBuffer.h"
#include "Rendering/Mesh.h"  // for ObjVertex layout / offsetof
#include "glad/glad.h"

MegaBuffer& MegaBuffer::instance() {
    static MegaBuffer inst;
    return inst;
}

void MegaBuffer::build(const std::vector<ObjVertex>& allVerts,
                        const std::vector<uint32_t>& allIndices) {
         glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(ObjVertex));
         glVertexArrayElementBuffer(vao, ebo);

         auto SetupAttr = [&](GLuint idx, GLint size, GLenum type, GLsizei offset, bool isInt = false) {
                 glEnableVertexArrayAttrib(vao, idx);
                 if (isInt) glVertexArrayAttribIFormat(vao, idx, size, type, offset);
                 else glVertexArrayAttribFormat(vao, idx, size, type, GL_FALSE, offset);
                 glVertexArrayAttribBinding(vao, idx, 0);
             };

         SetupAttr(0, 3, GL_FLOAT, offsetof(ObjVertex, px));
         SetupAttr(1, 3, GL_FLOAT, offsetof(ObjVertex, nx));
         SetupAttr(2, 2, GL_FLOAT, offsetof(ObjVertex, u0));
         SetupAttr(3, 3, GL_FLOAT, offsetof(ObjVertex, tx));
         SetupAttr(4, 3, GL_FLOAT, offsetof(ObjVertex, bx));
         SetupAttr(5, 4, GL_FLOAT, offsetof(ObjVertex, w0));
         SetupAttr(6, 4, GL_UNSIGNED_BYTE, offsetof(ObjVertex, j0), true);
         SetupAttr(7, 2, GL_FLOAT, offsetof(ObjVertex, u1));
         SetupAttr(8, 4, GL_FLOAT, offsetof(ObjVertex, cr));
}

void MegaBuffer::bind() const {
    glBindVertexArray(vao);
}
