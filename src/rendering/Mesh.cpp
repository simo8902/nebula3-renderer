// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Mesh.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"

void Mesh::SetupVAO(Mesh& out) {
    if (out.vao == 0) glGenVertexArrays(1, out.vao.put());
    if (out.vbo == 0) glGenBuffers(1, out.vbo.put());
    if (out.ebo == 0) glGenBuffers(1, out.ebo.put());
    glBindVertexArray(out.vao); glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER, out.verts.size() * sizeof(ObjVertex), out.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, out.idx.size() * sizeof(uint32_t), out.idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, px));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, nx));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, u0));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, w0));
    glEnableVertexAttribArray(4); glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, j0));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, u1));

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, tx));

    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, bx));

    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, cr));

    glBindVertexArray(0);
}

bool Mesh::LoadNVX2(const std::string& path, Mesh& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "[NVX2] cannot open " << path << " "; return false; }

    char magic[4]; f.read(magic, 4); if (!f)return false;
    uint32_t numGroups, numVertices, vertexWidthFloats, numTrianglesOrIndices, numEdges, compMask;
    f.read((char*)&numGroups, 4);
    f.read((char*)&numVertices, 4);
    f.read((char*)&vertexWidthFloats, 4);
    f.read((char*)&numTrianglesOrIndices, 4);
    f.read((char*)&numEdges, 4);
    f.read((char*)&compMask, 4);

    out.groups.assign(numGroups, {});
    if (numGroups) { f.read((char*)out.groups.data(), numGroups * sizeof(Nvx2Group)); if (!f)return false; }

    const uint32_t stride = vertexWidthFloats * 4;
    std::unordered_map<uint32_t, int> offsets;
    int off = 0;
    auto add = [&](uint32_t c, int sz) { if (compMask & c) { offsets[c] = off; off += sz; } };
    add(Coord, 12);
    add(Normal, 12); add(NormalUB4N, 4);
    add(Uv0, 8); add(Uv0S2, 4);
    add(Uv1, 8); add(Uv1S2, 4);
    add(Uv2, 8); add(Uv2S2, 4);
    add(Uv3, 8); add(Uv3S2, 4);
    add(Color, 16); add(ColorUB4N, 4);
    add(Tangent, 12); add(TangentUB4N, 4);
    add(Binormal, 12); add(BinormalUB4N, 4);
    add(Weights, 16); add(WeightsUB4N, 4);
    add(JIndices, 16); add(JIndicesUB4, 4);

    std::vector<uint8_t> vbuf(size_t(numVertices) * stride);
    if (!vbuf.empty()) { f.read((char*)vbuf.data(), vbuf.size()); if (!f)return false; }

    out.verts.assign(numVertices, {});
    for (uint32_t i = 0; i < numVertices; i++) {
        const uint8_t* base = vbuf.data() + size_t(i) * stride;
        ObjVertex v{}; v.nz = 1; v.cr = v.cg = v.cb = v.ca = 1; v.w0 = 1;
        if (offsets.count(Coord)) {
            const float* p = (const float*)(base + offsets[Coord]);
            v.px = p[0]; v.py = p[1]; v.pz = p[2];
        }
        if (offsets.count(Normal)) {
            const float* n = (const float*)(base + offsets[Normal]);
            v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
        }
        else if (offsets.count(NormalUB4N)) {
            const uint8_t* n = base + offsets[NormalUB4N];
            v.nx = ub_to_n11(n[0]); v.ny = ub_to_n11(n[1]); v.nz = ub_to_n11(n[2]);
            float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz); if (len > 0) { float inv = 1.0f / len; v.nx *= inv; v.ny *= inv; v.nz *= inv; }
        }
        if (offsets.count(Tangent)) {
            const float* t = (const float*)(base + offsets[Tangent]);
            v.tx = t[0]; v.ty = t[1]; v.tz = t[2];
        }
        else if (offsets.count(TangentUB4N)) {
            const uint8_t* t = base + offsets[TangentUB4N];
            v.tx = ub_to_n11(t[0]); v.ty = ub_to_n11(t[1]); v.tz = ub_to_n11(t[2]);
        }
        if (offsets.count(Binormal)) {
            const float* b = (const float*)(base + offsets[Binormal]);
            v.bx = b[0]; v.by = b[1]; v.bz = b[2];
        }
        else if (offsets.count(BinormalUB4N)) {
            const uint8_t* b = base + offsets[BinormalUB4N];
            v.bx = ub_to_n11(b[0]); v.by = ub_to_n11(b[1]); v.bz = ub_to_n11(b[2]);
        }
        if (offsets.count(Uv0)) { const float* u = (const float*)(base + offsets[Uv0]); v.u0 = u[0]; v.v0 = u[1]; }
        else if (offsets.count(Uv0S2)) { const int16_t* u = (const int16_t*)(base + offsets[Uv0S2]); v.u0 = fps2float(u[0]); v.v0 = fps2float(u[1]); }
        if (offsets.count(Uv1)) { const float* u = (const float*)(base + offsets[Uv1]); v.u1 = u[0]; v.v1 = u[1]; }
        else if (offsets.count(Uv1S2)) { const int16_t* u = (const int16_t*)(base + offsets[Uv1S2]); v.u1 = fps2float(u[0]); v.v1 = fps2float(u[1]); }
        if (offsets.count(Color)) { const float* c = (const float*)(base + offsets[Color]); v.cr = c[0]; v.cg = c[1]; v.cb = c[2]; v.ca = c[3]; }
        else if (offsets.count(ColorUB4N)) { const uint8_t* c = base + offsets[ColorUB4N]; v.cr = c[0] / 255.0f; v.cg = c[1] / 255.0f; v.cb = c[2] / 255.0f; v.ca = c[3] / 255.0f; }
        if (offsets.count(Weights)) { const float* w = (const float*)(base + offsets[Weights]); v.w0 = w[0]; v.w1 = w[1]; v.w2 = w[2]; v.w3 = w[3]; }
        else if (offsets.count(WeightsUB4N)) {
            const uint8_t* w = base + offsets[WeightsUB4N];
            v.w0 = w[0] / 255.0f; v.w1 = w[1] / 255.0f; v.w2 = w[2] / 255.0f; v.w3 = w[3] / 255.0f;
            float s = v.w0 + v.w1 + v.w2 + v.w3; if (s > 0) { float inv = 1.0f / s; v.w0 *= inv; v.w1 *= inv; v.w2 *= inv; v.w3 *= inv; }
        }
        if (offsets.count(JIndices)) { const float* jf = (const float*)(base + offsets[JIndices]); v.j0 = (uint8_t)jf[0]; v.j1 = (uint8_t)jf[1]; v.j2 = (uint8_t)jf[2]; v.j3 = (uint8_t)jf[3]; }
        else if (offsets.count(JIndicesUB4)) { const uint8_t* jb = base + offsets[JIndicesUB4]; v.j0 = jb[0]; v.j1 = jb[1]; v.j2 = jb[2]; v.j3 = jb[3]; }
        out.verts[i] = v;
    }

    uint32_t totalTris = 0;
    for (auto& g : out.groups) totalTris += g.numTriangles;
    uint32_t expectedIndexCount = totalTris * 3;
    if (expectedIndexCount == 0) expectedIndexCount = numTrianglesOrIndices;

    out.idx.clear();
    if (expectedIndexCount) {
        std::streampos pos = f.tellg(); f.seekg(0, std::ios::end);
        size_t remain = size_t(f.tellg() - pos); f.seekg(pos);
        size_t need16 = size_t(expectedIndexCount) * 2, need32 = size_t(expectedIndexCount) * 4;
        if ((remain >= need32 && remain % 4 == 0 && numVertices >= 65536) || remain == need32) {
            std::vector<uint32_t> tmp(expectedIndexCount);
            f.read((char*)tmp.data(), need32); if (!f)return false;
            out.idx = std::move(tmp);
        }
        else {
            std::vector<uint16_t> tmp(expectedIndexCount);
            f.read((char*)tmp.data(), need16); if (!f)return false;
            out.idx.resize(expectedIndexCount);
            for (uint32_t i = 0; i < expectedIndexCount; i++) out.idx[i] = tmp[i];
        }
    }


    out.computeBoundingBox();
    SetupVAO(out);
    SetupMultiDraw(out);
    return true;
}

void Mesh::SetupMultiDraw(Mesh& out) {
    out.draw_commands.clear();
    out.draw_commands.reserve(out.groups.size());

    for (auto& g : out.groups) {
        DrawCommand cmd{};
        cmd.count = g.indexCount();
        cmd.instanceCount = 1;
        cmd.firstIndex = g.firstIndex();
        cmd.baseVertex = 0;
        cmd.baseInstance = 0;
        out.draw_commands.push_back(cmd);
    }

    if (out.indirect_buffer == 0) glGenBuffers(1, out.indirect_buffer.put());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, out.indirect_buffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 out.draw_commands.size() * sizeof(DrawCommand),
                 out.draw_commands.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void Mesh::drawMulti() const {
    glBindVertexArray(vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, 0,
                                draw_commands.size(), 0);
}

void Mesh::drawGroup(int groupIndex) const {
    glBindVertexArray(vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
    glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
        (void*)(size_t(groupIndex) * sizeof(DrawCommand)));
}

void Mesh::computeBoundingBox() {
    if (verts.empty()) {
        boundingBoxMin = glm::vec3(0.0f);
        boundingBoxMax = glm::vec3(0.0f);
        return;
    }

    boundingBoxMin = glm::vec3(verts[0].px, verts[0].py, verts[0].pz);
    boundingBoxMax = boundingBoxMin;

    for (const auto& v : verts) {
        boundingBoxMin.x = std::min(boundingBoxMin.x, v.px);
        boundingBoxMin.y = std::min(boundingBoxMin.y, v.py);
        boundingBoxMin.z = std::min(boundingBoxMin.z, v.pz);
        boundingBoxMax.x = std::max(boundingBoxMax.x, v.px);
        boundingBoxMax.y = std::max(boundingBoxMax.y, v.py);
        boundingBoxMax.z = std::max(boundingBoxMax.z, v.pz);
    }
}
