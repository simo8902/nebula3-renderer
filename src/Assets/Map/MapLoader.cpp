// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Assets/Map/MapLoader.h"
#include "Core/Logger.h"
#include "glm.hpp"
#include <algorithm>
#include <iostream>


std::unique_ptr<MapData> MapLoader::load_map(const std::string& filepath) {
    NC::LOGGING::Log("[MAP] load_map begin path=", filepath);
    NDEVcBinaryReader r(filepath, false);
    if (!r.isOpen()) {
        NC::LOGGING::Error("[MAP] failed to open path=", filepath);
        return nullptr;
    }

    blocks.clear();
    auto data = std::make_unique<MapData>();
    if (!load_internal(r, data.get())) {
        NC::LOGGING::Error("[MAP] load_internal failed path=", filepath);
        return nullptr;
    }
    NC::LOGGING::Log("[MAP] load_map end path=", filepath,
                     " strings=", data->string_table.size(),
                     " templates=", data->templates.size(),
                     " instances=", data->instances.size(),
                     " groups=", data->groups.size(),
                     " events=", data->event_list.size(),
                     " mappings=", data->event_mapping.size());
    return data;
}

void MapLoader::dump_report(const MapData* m) const {
    for (const auto& block : blocks) {
        std::cout << std::setw(12) << std::left << block.name
            << " @ 0x" << std::hex << std::setw(8) << std::setfill('0')
            << block.start << std::dec << std::setfill(' ')
            << " (" << block.size << " bytes)\n";
    }
    std::cout << "\n";

    const MapInfo& info = m->info;
    const glm::vec4& gs = info.grid_size;
    const glm::vec4& ce = info.center;
    const glm::vec4& ex = info.extents;
    std::cout << std::fixed << std::setprecision(2)
        << "gridSize=(" << gs.x << "," << gs.y << "," << gs.z << "," << gs.w << ") "
        << "center=(" << ce.x << "," << ce.y << "," << ce.z << "," << ce.w << ") "
        << "extents=(" << ex.x << "," << ex.y << "," << ex.z << "," << ex.w << ") "
        << "mapSize=(" << info.map_size_x << "," << info.map_size_y << "," << info.map_size_z << ")\n\n";

    std::cout << "StringTable: " << m->string_table.size() << " entries\n";
    for (size_t i = 0; i < m->string_table.size(); i++) {
        std::cout << "  [" << std::setw(2) << std::setfill('0') << i << std::setfill(' ')
            << "] " << m->string_table[i] << "\n";
    }
    std::cout << "\n";

    std::cout << "Events: list=" << m->event_list.size()
        << " mapping=" << m->event_mapping.size() << "\n";
    for (size_t i = 0; i < m->event_mapping.size(); i++) {
        std::cout << "  map[" << i << "]: '" << m->event_mapping[i] << "'\n";
    }
    std::cout << "\n";

    std::cout << "Templates: " << m->templates.size() << "\n";
    for (size_t i = 0; i < m->templates.size(); i++) {
        const Template& t = m->templates[i];
        std::string name = (t.gfx_res_id < m->string_table.size())
            ? m->string_table[t.gfx_res_id] : "?";

        std::cout << std::setw(3) << std::setfill('0') << i << std::setfill(' ')
            << ": name=" << name
            << " gfxResId=" << t.gfx_res_id
            << " root=" << t.gfx_root_node
            << " phx=" << t.phx_res_id
            << " coll=" << t.coll_res_id
            << " sfx=" << t.sfx_event_id
            << std::fixed << std::setprecision(2)
            << " center=(" << t.center.x << "," << t.center.y << "," << t.center.z << ")"
            << " extents=(" << t.extents.x << "," << t.extents.y << "," << t.extents.z << ")"
            << " collGrp=" << t.coll_mesh_group_index
            << " type=" << t.type << "\n";
    }
    std::cout << "\n";

    std::cout << "Instances: " << m->instances.size()
        << " | Templates: " << m->templates.size()
        << " | Groups: " << m->groups.size() << "\n";
    for (size_t i = 0; i < m->instances.size(); i++) {
        const Instance& inst = m->instances[i];
        float sx = inst.use_scaling ? inst.scale.x : 1.0f;
        float sy = inst.use_scaling ? inst.scale.y : 1.0f;
        float sz = inst.use_scaling ? inst.scale.z : 1.0f;

        std::string tmpl_name = "?";
        std::string grp_name = "?";
        std::string evt_map = "";

        if (inst.templ_index >= 0 && inst.templ_index < (int)m->templates.size()) {
            const Template& t = m->templates[inst.templ_index];
            if (t.gfx_res_id < m->string_table.size()) {
                tmpl_name = m->string_table[t.gfx_res_id];
            }
        }

        if (inst.group_index >= 0 && inst.group_index < (int)m->groups.size()) {
            const Group& g = m->groups[inst.group_index];
            if (g.name_id < m->string_table.size()) {
                grp_name = m->string_table[g.name_id];
            }
        }

        if (inst.index_to_mapping < m->event_mapping.size()) {
            evt_map = m->event_mapping[inst.index_to_mapping];
        }

        std::cout << std::setw(3) << std::setfill('0') << i << std::setfill(' ')
            << ": grp=" << inst.group_index << "(" << grp_name << ")"
            << " tmpl=" << inst.templ_index << "(" << tmpl_name << ")"
            << std::fixed << std::setprecision(2)
            << " T=(" << inst.pos.x << "," << inst.pos.y << "," << inst.pos.z << ")"
            << std::setprecision(3)
            << " R=(" << inst.rot.x << "," << inst.rot.y << "," << inst.rot.z << "," << inst.rot.w << ")"
            << std::setprecision(2)
            << " S=(" << sx << "," << sy << "," << sz << ")"
            << " dgNameIdx=" << inst.dg_name_index
            << " evtMapIdx=" << inst.index_to_mapping;

        if (!evt_map.empty()) {
            std::cout << ":" << evt_map;
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    std::cout << "Groups: " << m->groups.size() << "\n";
    for (size_t i = 0; i < m->groups.size(); i++) {
        const Group& g = m->groups[i];
        std::string name = (g.name_id < m->string_table.size())
            ? m->string_table[g.name_id] : "?";

        std::cout << std::setw(3) << std::setfill('0') << i << std::setfill(' ')
            << ": name=" << name
            << " type=" << g.type
            << " parent=" << g.parent_group_index
            << " insts=[";

        for (size_t j = 0; j < g.instance_indices.size(); j++) {
            if (j > 0) std::cout << ", ";
            std::cout << g.instance_indices[j];
        }
        std::cout << "] groups=[";

        for (size_t j = 0; j < g.group_indices.size(); j++) {
            if (j > 0) std::cout << ", ";
            std::cout << g.group_indices[j];
        }
        std::cout << "]\n";
    }
}

bool MapLoader::load_internal(NDEVcBinaryReader& r, MapData* data) {
    NC::LOGGING::Log("[MAP] load_internal begin");
    if (!parse_header(r)) { NC::LOGGING::Error("[MAP] header fail"); return false; }
    if (!load_map_info(r, data)) { NC::LOGGING::Error("[MAP] map info fail"); return false; }
    if (!load_string_table(r, data)) { NC::LOGGING::Error("[MAP] string table fail"); return false; }
    if (!load_sets(r)) { NC::LOGGING::Error("[MAP] sets fail"); return false; }
    if (!load_event_list(r, data)) { NC::LOGGING::Error("[MAP] event list fail"); return false; }
    if (!load_event_mapping(r, data)) { NC::LOGGING::Error("[MAP] event mapping fail"); return false; }
    if (!load_templates(r, data)) { NC::LOGGING::Error("[MAP] templates fail"); return false; }
    if (!load_instances(r, data)) { NC::LOGGING::Error("[MAP] instances fail"); return false; }
    if (!load_groups(r, data)) { NC::LOGGING::Error("[MAP] groups fail"); return false; }
    if (!load_nav_blockers(r)) { NC::LOGGING::Error("[MAP] nav blockers fail"); return false; }

    NC::LOGGING::Log("[MAP] load_internal end");
    return true;
}

bool MapLoader::check_tag(NDEVcBinaryReader& r, const char* expected) {
    std::string tag;
    if (!r.readFourCC(tag)) return false;
    std::reverse(tag.begin(), tag.end());
    return tag == expected;
}

bool MapLoader::parse_header(NDEVcBinaryReader& r) {
    if (!check_tag(r, "DSOM")) return false;
    uint32_t version;
    if (!r.readU32(version)) return false;
    NC::LOGGING::Log("[MAP] parse_header version=", version);
    return version == VERSION;
}

bool MapLoader::load_map_info(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "MAPI")) return false;
    for (int i = 0; i < 4; i++) if (!r.readF32(data->info.grid_size[i])) return false;
    for (int i = 0; i < 4; i++) if (!r.readF32(data->info.center[i])) return false;
    for (int i = 0; i < 4; i++) if (!r.readF32(data->info.extents[i])) return false;
    if (!r.readI16(data->info.map_size_x)) return false;
    if (!r.readI16(data->info.map_size_y)) return false;
    if (!r.readI16(data->info.map_size_z)) return false;
    return true;
}

bool MapLoader::load_string_table(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "STRT")) return false;
    uint32_t num_strings, table_size;
    if (!r.readU32(num_strings)) return false;
    if (!r.readU32(table_size)) return false;

    if (num_strings > 0) {
        std::vector<char> buf(table_size);
        if (!r.readRawData(buf.data(), table_size)) return false;

        size_t pos = 0;
        for (uint32_t i = 0; i < num_strings && pos < table_size; i++) {
            std::string s;
            while (pos < table_size && buf[pos] != '\0') {
                s += buf[pos++];
            }
            pos++;
            data->string_table.push_back(s);
        }
    }
    NC::LOGGING::Log("[MAP] load_string_table num_strings=", data->string_table.size(), " raw_table_size=", table_size);
    return true;
}

bool MapLoader::load_sets(NDEVcBinaryReader& r) {
    if (!check_tag(r, "SETT")) return false;
    uint32_t num_sets;
    if (!r.readU32(num_sets)) return false;
    for (uint32_t i = 0; i < num_sets; i++) {
        uint16_t dummy;
        if (!r.readU16(dummy)) return false;
        if (!r.readU16(dummy)) return false;
    }
    return true;
}

bool MapLoader::load_event_list(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "EVET")) return false;
    uint32_t n;
    if (!r.readU32(n)) return false;
    data->event_list.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v;
        if (!r.readU32(v)) return false;
        data->event_list.push_back(v);
    }
    NC::LOGGING::Log("[MAP] load_event_list count=", data->event_list.size());
    return true;
}

bool MapLoader::load_event_mapping(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "EMAL")) return false;
    uint32_t n;
    if (!r.readU32(n)) return false;
    data->event_mapping.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        std::string s;
        if (!r.readString(s)) return false;
        data->event_mapping.push_back(s);
    }
    NC::LOGGING::Log("[MAP] load_event_mapping count=", data->event_mapping.size());
    return true;
}

bool MapLoader::load_templates(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "TMPL")) return false;
    uint32_t count;
    if (!r.readU32(count)) return false;
    data->templates.reserve(count);

    for (uint32_t i = 0; i < count; i++) {
        Template t;
        if (!r.readU16(t.gfx_res_id)) return false;
        if (!r.readU16(t.gfx_root_node)) return false;
        if (!r.readU16(t.phx_res_id)) return false;
        if (!r.readU16(t.coll_res_id)) return false;
        if (!r.readU16(t.sfx_event_id)) return false;
        for (int j = 0; j < 4; j++) if (!r.readF32(t.center[j])) return false;
        for (int j = 0; j < 4; j++) if (!r.readF32(t.extents[j])) return false;
        if (!r.readU16(t.coll_mesh_group_index)) return false;
        if (!r.readU16(t.type)) return false;
        data->templates.push_back(t);
    }
    NC::LOGGING::Log("[MAP] load_templates count=", data->templates.size());
    return true;
}

bool MapLoader::load_instances(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "INST")) return false;
    uint32_t n;
    if (!r.readU32(n)) return false;
    data->instances.reserve(n);

    for (uint32_t i = 0; i < n; i++) {
        Instance inst;
        for (int j = 0; j < 4; j++) if (!r.readF32(inst.pos[j])) return false;
        for (int j = 0; j < 4; j++) if (!r.readF32(inst.rot[j])) return false;
        if (!r.readBool(inst.use_scaling)) return false;
        if (inst.use_scaling) {
            for (int j = 0; j < 4; j++) if (!r.readF32(inst.scale[j])) return false;
        } else {
            inst.scale = glm::vec4(1, 1, 1, 0);
        }
        if (!r.readBool(inst.use_collide)) return false;
        if (!r.readBool(inst.is_visible_for_nav_mesh_gen)) return false;
        if (!r.readI16(inst.templ_index)) return false;
        if (!r.readI16(inst.group_index)) return false;
        if (!r.readU16(inst.dg_name_index)) return false;
        if (!r.readU16(inst.index_to_mapping)) return false;

        data->instances.push_back(inst);
    }
    NC::LOGGING::Log("[MAP] load_instances count=", data->instances.size());
    return true;
}

void MapLoader::parse_event_bits(const std::string& mapping, Instance& inst) {
    //TODO:
}

bool MapLoader::load_groups(NDEVcBinaryReader& r, MapData* data) {
    if (!check_tag(r, "GROP")) return false;
    uint32_t n;
    if (!r.readU32(n)) return false;
    data->groups.reserve(n);

    for (uint32_t i = 0; i < n; i++) {
        Group g;
        if (!r.readU16(g.name_id)) return false;
        if (!r.readU16(g.type)) return false;
        if (!r.readI16(g.parent_group_index)) return false;

        uint16_t ni, ng;
        if (!r.readU16(ni)) return false;
        if (!r.readU16(ng)) return false;

        g.instance_indices.reserve(ni);
        for (uint16_t j = 0; j < ni; j++) {
            uint16_t idx;
            if (!r.readU16(idx)) return false;
            g.instance_indices.push_back(idx);
        }

        g.group_indices.reserve(ng);
        for (uint16_t j = 0; j < ng; j++) {
            uint16_t idx;
            if (!r.readU16(idx)) return false;
            g.group_indices.push_back(idx);
        }

        data->groups.push_back(g);
    }
    NC::LOGGING::Log("[MAP] load_groups count=", data->groups.size());
    return true;
}

bool MapLoader::load_nav_blockers(NDEVcBinaryReader& r) {
    if (!check_tag(r, "NAVB")) return false;
    uint32_t n;
    if (!r.readU32(n)) return false;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t pts;
        if (!r.readU32(pts)) return false;
        for (uint32_t j = 0; j < pts; j++) {
            float x, y;
            if (!r.readF32(x)) return false;
            if (!r.readF32(y)) return false;
        }
    }
    NC::LOGGING::Log("[MAP] load_nav_blockers count=", n);
    return true;
}
