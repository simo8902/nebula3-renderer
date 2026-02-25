// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MAPHEADER_H
#define NDEVC_MAPHEADER_H

#include <unordered_map>
#include <string>
#include <vector>
#include "glm.hpp"

struct MapInfo {
    glm::vec4 grid_size;
    glm::vec4 center;
    glm::vec4 extents;
    int16_t map_size_x;
    int16_t map_size_y;
    int16_t map_size_z;
};

struct Template {
    uint16_t gfx_res_id;
    uint16_t gfx_root_node;
    uint16_t phx_res_id;
    uint16_t coll_res_id;
    uint16_t sfx_event_id;
    glm::vec4 center;
    glm::vec4 extents;
    uint16_t coll_mesh_group_index;
    uint16_t type;
};

struct Instance {
    glm::vec4 pos;
    glm::vec4 rot;
    bool use_scaling;
    glm::vec4 scale;
    bool use_collide;
    bool is_visible_for_nav_mesh_gen;
    int16_t templ_index;
    int16_t group_index;
    uint16_t dg_name_index;
    uint16_t index_to_mapping;

    // Event visibility data (parsed from event_mapping)
    bool is_event_default = true;  // Visible by default (main map)
    std::unordered_map<std::string, bool> eventBits;  // Event visibility flags
};

struct Group {
    uint16_t name_id;
    uint16_t type;
    int16_t parent_group_index;
    std::vector<uint16_t> instance_indices;
    std::vector<uint16_t> group_indices;
};

struct MapData {
    MapInfo info;
    std::vector<std::string> string_table;
    std::vector<uint32_t> event_list;
    std::vector<std::string> event_mapping;
    std::vector<Template> templates;
    std::vector<Instance> instances;
    std::vector<Group> groups;
};

#endif //NDEVC_MAPHEADER_H