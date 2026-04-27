// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_NDEVCSTRUCTURE_H
#define NDEVC_NDEVCSTRUCTURE_H

#include "glm.hpp"
#include "Platform/NDEVcHeaders.h"

struct Joint {
    int32_t joint_idx = 0;
    int32_t parent_joint_idx = 0;
    std::array<float, 4> pose_translation{ 0,0,0,0 }; // TODO: conversion!!
    std::array<float, 4> pose_rotation{ 0,0,0,0 }; // TODO: conversion!!
    std::array<float, 4> pose_scale{ 0,0,0,0 }; // TODO: conversion!!
    std::string joint_name;
};

struct SkinFragment {
    int32_t prim_group_index = -1;
    std::vector<int32_t> joint_palette;
};

struct Node {
    std::string node_name;
    std::string node_type;
    uint32_t parentIndex = 0xFFFFFFFFu;

    glm::vec4 position{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 scale{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 rotate_pivot{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 scale_pivot{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 local_box_min{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 local_box_max{0.0f, 0.0f, 0.0f, 1.0f};

    bool in_view_space = false;
    bool locked_to_viewer = false;
    float min_distance = 0.0f;
    float max_distance = 0.0f;

    std::string mesh_ressource_id;
    int32_t primitive_group_idx = -1;

    std::string shader;
    std::unordered_map<std::string, std::string> shader_params_texture;
    std::unordered_map<std::string, int32_t> shader_params_int;
    std::unordered_map<std::string, float> shader_params_float;
    std::unordered_map<std::string, glm::vec4> shader_params_vec4;
    std::unordered_map<int32_t, glm::vec4> shader_params_mlp_uv_stretch;
    std::unordered_map<int32_t, glm::vec4> shader_params_mlp_spec_intensity;

    std::string model_node_type;
    std::unordered_map<std::string, std::string> string_attrs;

    bool hrch_flag = false;
    bool cash_flag = false;

    std::string animation_resource;
    std::string variations_resource;

    int32_t num_joints = 0;
    std::vector<Joint> joints;
    int32_t num_skin_fragments = 0;
    std::unordered_map<int32_t, std::vector<int32_t>> skin_fragments;
};

struct Reporter {
    enum Type { Info, Warn, Err };
    std::string currentFile;

    void report(Type t, const std::string& msg) const {
        const char* k = t == Info ? "INFO" : t == Warn ? "WARNING" : "ERROR";
        std::cerr << "[" << k << "] ";
        if (!currentFile.empty()) {
            std::cerr << "File '" << currentFile << "': ";
        }
        std::cerr << msg << "\n";
    }
};

struct Options {
    bool ignore_version = true;
    std::string n3filepath;
};

struct ObjVertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz;
    float bx, by, bz;
    float u0, v0;
    float u1, v1;
    float u2, v2;
    float u3, v3;
    float cr, cg, cb, ca;
    float w0, w1, w2, w3;
    uint8_t j0, j1, j2, j3;
};

#pragma pack(push,1)
struct Nvx2Group {
    uint32_t firstVertex;
    uint32_t numVertices;
    uint32_t firstTriangle;
    uint32_t numTriangles;
    uint32_t firstEdge;
    uint32_t numEdges;

    uint32_t firstIndex()   const { return firstTriangle * 3; }
    uint32_t indexCount()   const { return numTriangles * 3; }
    uint32_t baseVertex()   const { return firstVertex; }
};
#pragma pack(pop)


#endif //NDEVC_NDEVCSTRUCTURE_H