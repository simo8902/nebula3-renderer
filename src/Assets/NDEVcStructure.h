// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_NDEVCSTRUCTURE_H
#define NDEVC_NDEVCSTRUCTURE_H

#include "glm.hpp"
#include "Platform/NDEVcHeaders.h"
#include "Assets/Particles/EnvelopeCurve.h"

// Animation enums
enum AnimNodeType {
    IntAnimator = 0,
    FloatAnimator = 1,
    Float4Animator = 2,
    TransformAnimator = 3,
    TransformCurveAnimator = 4,
    UvAnimator = 5,
    InvalidAnimNodeType = 6
};

enum AnimLoopType {
    Clamp = 0,
    Loop = 1
};

// Animation key template
template<typename T>
struct AnimKey {
    float time = 0.0f;
    T value;

    void SetTime(float t) { time = t; }
    void SetValue(const T& v) { value = v; }
};

// Animation section structure
struct AnimSection {
    AnimNodeType animationNodeType = TransformAnimator;
    AnimLoopType loopType = Clamp;
    std::vector<std::string> animatedNodesPath;
    std::string shaderVarSemantic;
    std::vector<AnimKey<glm::vec4>> posArray;
    std::vector<AnimKey<glm::vec4>> eulerArray;
    std::vector<AnimKey<glm::vec4>> scaleArray;
    std::vector<AnimKey<float>> floatKeyArray;
    std::vector<AnimKey<glm::vec4>> float4KeyArray;
    std::vector<AnimKey<int32_t>> intKeyArray;
    std::vector<int32_t> layer;
    std::string animationName;
    int32_t animationGroup = 0;
};

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
    Node* node_parent = nullptr;
    std::vector<Node*> node_children;

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

    Particles::EnvelopeCurve emission_frequency;
    Particles::EnvelopeCurve lifetime;
    Particles::EnvelopeCurve spread_min;
    Particles::EnvelopeCurve spread_max;
    Particles::EnvelopeCurve start_velocity;
    Particles::EnvelopeCurve rotation_velocity;
    Particles::EnvelopeCurve particle_size;
    Particles::EnvelopeCurve particle_mass;
    Particles::EnvelopeCurve time_manipulator;
    Particles::EnvelopeCurve velocity_factor;
    Particles::EnvelopeCurve air_resistance;
    Particles::EnvelopeCurve color_red;
    Particles::EnvelopeCurve color_green;
    Particles::EnvelopeCurve color_blue;
    Particles::EnvelopeCurve color_alpha;

    // ParticleSystemNode floats
    float particle_emission_duration = 0.0f;
    float particle_activity_distance = 0.0f;
    float start_rotation_min = 0.0f;
    float start_rotation_max = 0.0f;
    float particle_gravity = 0.0f;
    float particle_stretch = 0.0f;
    float texture_tile = 1.0f;
    float velocity_randomize = 0.0f;
    float rotation_randomize = 0.0f;
    float size_randomize = 0.0f;
    float precalc_time = 0.0f;
    float start_delay = 0.0f;

    // ParticleSystemNode bools/ints
    bool particle_looping = false;
    bool render_oldest_first = false;
    bool particle_billboard = false;
    bool stretch_to_start = false;
    bool randomize_rotation = false;
    bool view_angle_fade = false;
    bool curve_looping = false;
    int32_t stretch_detail = 0;

    // Animation data
    std::vector<AnimSection> animSections;
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