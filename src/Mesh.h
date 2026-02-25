// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MESH_H
#define NDEVC_MESH_H

#include "NDEVcHeaders.h"
#include "NDEVcStructure.h"

static float ub_to_n11(uint8_t b) { return (float(b) * (1.0f / 255.0f)) * 2.0f - 1.0f; }
static float fps2float(int16_t s) { return float(s) / 8192.0f; }

struct DrawCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  baseVertex;
    uint32_t baseInstance;
};

class Mesh {
    enum VertexComponent {
        Coord = 1 << 0,
        Normal = 1 << 1,
        NormalUB4N = 1 << 2,
        Uv0 = 1 << 3,
        Uv0S2 = 1 << 4,
        Uv1 = 1 << 5,
        Uv1S2 = 1 << 6,
        Uv2 = 1 << 7,
        Uv2S2 = 1 << 8,
        Uv3 = 1 << 9,
        Uv3S2 = 1 << 10,
        Color = 1 << 11,
        ColorUB4N = 1 << 12,
        Tangent = 1 << 13,
        TangentUB4N = 1 << 14,
        Binormal = 1 << 15,
        BinormalUB4N = 1 << 16,
        Weights = 1 << 17,
        WeightsUB4N = 1 << 18,
        JIndices = 1 << 19,
        JIndicesUB4 = 1 << 20
    };

    static int GetComponentSize(const uint32_t &comp) {
        switch (comp) {
            case Coord: return 12;
            case Normal: return 12;
            case NormalUB4N: return 4;
            case Uv0: return 8;
            case Uv0S2: return 4;
            case Uv1: return 8;
            case Uv1S2: return 4;
            case Uv2: return 8;
            case Uv2S2: return 4;
            case Uv3: return 8;
            case Uv3S2: return 4;
            case Color: return 16;
            case ColorUB4N: return 4;
            case Tangent: return 12;
            case TangentUB4N: return 4;
            case Binormal: return 12;
            case BinormalUB4N: return 4;
            case Weights: return 16;
            case WeightsUB4N: return 4;
            case JIndices: return 16;
            case JIndicesUB4: return 4;
            default: return 0;
        }
    }
public:
    static void SetupVAO(Mesh& out);
    static bool LoadNVX2(const std::string& path, Mesh& out);
    uint32_t vao = 0, vbo = 0, ebo = 0;

    std::vector<ObjVertex> verts;
    std::vector<uint32_t> idx;
    std::vector<Nvx2Group> groups;

    uint32_t indirect_buffer = 0;
    std::vector<DrawCommand> draw_commands;
    uint32_t megaVertexOffset = 0;
    uint32_t megaIndexOffset  = 0;

    glm::vec3 boundingBoxMin{0.0f};
    glm::vec3 boundingBoxMax{0.0f};

    static void SetupMultiDraw(Mesh& out);
    void drawMulti() const;
    void drawGroup(int groupIndex) const;
    void computeBoundingBox();
};


#endif //NDEVC_MESH_H