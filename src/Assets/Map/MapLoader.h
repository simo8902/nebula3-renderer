// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MAPLOADER_H
#define NDEVC_MAPLOADER_H

#include "Platform/NDEVcHeaders.h"
#include "Assets/NDEVcBinaryReader.h"
#include "Assets/Map/MapHeader.h"
#include <memory>

class MapLoader {
public:
    static constexpr int VERSION = 5;

    struct BlockInfo {
        std::string name;
        std::streampos start;
        size_t size;
    };

    std::vector<BlockInfo> blocks;

    std::unique_ptr<MapData> load_map(const std::string& filepath);
    void dump_report(const MapData* m) const;

private:
    bool load_internal(NDEVcBinaryReader& r, MapData* data);
    bool check_tag(NDEVcBinaryReader& r, const char* expected);
    bool parse_header(NDEVcBinaryReader& r);
    bool load_map_info(NDEVcBinaryReader& r, MapData* data);
    bool load_string_table(NDEVcBinaryReader& r, MapData* data);
    bool load_sets(NDEVcBinaryReader& r);
    bool load_event_list(NDEVcBinaryReader& r, MapData* data);
    bool load_event_mapping(NDEVcBinaryReader& r, MapData* data);
    bool load_templates(NDEVcBinaryReader& r, MapData* data);
    bool load_instances(NDEVcBinaryReader& r, MapData* data);
    bool load_groups(NDEVcBinaryReader& r, MapData* data);
    bool load_nav_blockers(NDEVcBinaryReader& r);

    void parse_event_bits(const std::string& mapping, Instance& inst);
};


#endif //NDEVC_MAPLOADER_H
