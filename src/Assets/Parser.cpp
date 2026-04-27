// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Assets/Parser.h"
#include "Rendering/ValidationLayer.h"
#include "glm.hpp"
#include <algorithm>
#include <cctype>

bool Parser::parse_file(const std::string &filepath) {
    this->filepath = filepath;
    rep.currentFile = filepath;
    NDEVcBinaryReader reader(filepath, false);
    if (!reader.isOpen()) { rep.report(Reporter::Err, "Cannot open file"); return false; }
    if (!read_header(reader)) return false;

    nodeStackObj.clear();
    bool done = false;

    while (!done && !reader.eof()) {
        std::string tag;
        if (!reader.readFourCC(tag)) break;

        if (tag == ">MDL" || tag == "LDM>") {
            std::string classFourCC;
            if (!reader.readFourCC(classFourCC)) return false;
            if (!reader.readString(n3modelname)) return false;
            n3modeltype = classFourCC;
            if (coreParams) std::cout << ">MDL type='" << classFourCC << "' name='" << n3modelname << "'\n";
        }
        else if (tag == "<MDL" || tag == "LDM<") {
            if (coreParams) std::cout << "<MDL\n";
            done = true;
        }
        else if (tag == ">MND" || tag == "DNM>") {
            std::string classFourCC;
            if (!reader.readFourCC(classFourCC)) return false;
            std::string node_name;
            if (!reader.readString(node_name)) return false;

            std::string ind((nodeStackObj.size() + 1) * 2, ' ');
            if (coreParams)  std::cout << ind << ">MND type='" << classFourCC << "' name='" << node_name << "'\n";

            Node new_node;
            new_node.node_type    = classFourCC;
            new_node.node_name    = node_name;
            new_node.parentIndex  = nodeStackObj.empty() ? 0xFFFFFFFFu : nodeStackObj.back();
            const uint32_t newIdx = static_cast<uint32_t>(n3node_storage.size());
            n3node_storage.push_back(std::move(new_node));
            nodeStackObj.push_back(newIdx);
        }
        else if (tag == "<MND" || tag == "DNM<") {
            if (!nodeStackObj.empty()) {
                std::string ind(nodeStackObj.size() * 2, ' ');
                if (coreParams) std::cout << ind << "<MND '" << n3node_storage[nodeStackObj.back()].node_name << "'\n";
                nodeStackObj.pop_back();
            }
        }
        else {
            if (!nodeStackObj.empty()) {
                std::string ind((nodeStackObj.size() + 1) * 2, ' ');
                parse_node_tag(reader, tag, n3node_storage[nodeStackObj.back()], ind);
            }
        }
    }

    return true;
}

bool Parser::read_header(NDEVcBinaryReader& reader) {
    std::string magic;
    if (!reader.readFourCC(magic)) { rep.report(Reporter::Err, "Failed reading magic"); return false; }
    if (magic != "NEB3" && magic != "3BEN") {
        rep.report(Reporter::Err, std::string("Invalid file, unknown fourCC '") + magic + "'");
        return false;
    }

    uint32_t rawVer = 0;
    if (!reader.readU32(rawVer)) { rep.report(Reporter::Err, "Failed reading version"); return false; }
    if (coreParams) std::cout << "Raw version: " << rawVer << "\n";

    uint32_t le = rawVer;
    uint32_t be = ((rawVer & 0xff) << 24) | ((rawVer & 0xff00) << 8) | ((rawVer & 0xff0000) >> 8) | ((rawVer >> 24) & 0xff);

    if (le == 1 || le == 2) {
        n3version = (int)le;
        reader.setSwapBytes(false);
        //if (coreParams) std::cout << "Detected little-endian, version=" << n3version << "\n";
    }
    else if (be == 1 || be == 2) {
        n3version = (int)be;
        reader.setSwapBytes(true);
        //if (coreParams) std::cout << "Detected big-endian, version=" << n3version << "\n";
    }
    else {
        rep.report(Reporter::Err, "Unsupported version field: " + std::to_string(le) + " / " + std::to_string(be));
        return false;
    }

    return true;
}

bool Parser::is_valid_fourcc(const std::string &tag) {
    if (tag.length() != 4) return false;
    for (char c : tag) {
        if (c < 'A' || c > 'Z') return false;
    }
    return true;
}

bool Parser::parse_node_tag(NDEVcBinaryReader& reader, const std::string& tag, Node& node, const std::string& ind) const
{
    // CharacterSkinNode tags
    if (tag == "FKSN" || tag == "NSKF") {
        int32_t num_fragments;
        if (!reader.readI32(num_fragments)) { std::cout << ind << "[FKSN] readI32 failed\n"; return false; }
        if (coreParams) std::cout << ind << "[FKSN] num_fragments=" << num_fragments << "\n";
        node.skin_fragments.clear();
        node.skin_fragments.reserve(num_fragments);
        return true;
    }
    else if (tag == "GRFS" || tag == "SFRG") {
        int32_t prim_group_index;
        if (!reader.readI32(prim_group_index)) { std::cout << ind << "[GRFS] readI32 prim_group_index failed\n"; return false; }
        int32_t num_joints;
        if (!reader.readI32(num_joints)) { std::cout << ind << "[GRFS] readI32 num_joints failed\n"; return false; }
        if (coreParams)  std::cout << ind << "[GRFS] prim_group_index=" << prim_group_index << " num_joints=" << num_joints << "\n";
        std::vector<int32_t> joint_palette;
        joint_palette.reserve(num_joints);
        for (int32_t i = 0; i < num_joints; i++) {
            int32_t joint_idx;
            if (!reader.readI32(joint_idx)) {
                std::cout << ind << "[GRFS] readI32 joint_idx failed at i=" << i << "\n";
                return false;
            }
            joint_palette.push_back(joint_idx);
        }
        if (coreParams) std::cout << ind << "[GRFS] joint_palette size=" << joint_palette.size() << "\n";
        node.skin_fragments[prim_group_index] = joint_palette;
        return true;
    }
    else if (tag == "NJNT" || tag == "TNJN") {
        int32_t numJoints = 0;
        if (!reader.readI32(numJoints)) { std::cout << ind << "[NJNT] readI32 failed\n"; return false; }
        node.num_joints = numJoints;
        node.joints.clear();
        if (numJoints > 0) node.joints.reserve(static_cast<size_t>(numJoints));
        if (coreParams) std::cout << ind << "[NJNT] num_joints=" << numJoints << "\n";
        return true;
    }
    else if (tag == "JONT" || tag == "TNOJ") {
        Joint j;
        if (!reader.readI32(j.joint_idx) || !reader.readI32(j.parent_joint_idx)) {
            std::cout << ind << "[JONT] readI32 failed\n";
            return false;
        }

        float px, py, pz, pw;
        float rx, ry, rz, rw;
        float sx, sy, sz, sw;
        if (!reader.readF32(px) || !reader.readF32(py) || !reader.readF32(pz) || !reader.readF32(pw) ||
            !reader.readF32(rx) || !reader.readF32(ry) || !reader.readF32(rz) || !reader.readF32(rw) ||
            !reader.readF32(sx) || !reader.readF32(sy) || !reader.readF32(sz) || !reader.readF32(sw)) {
            std::cout << ind << "[JONT] readF32 failed\n";
            return false;
        }
        j.pose_translation = { px, py, pz, pw };
        j.pose_rotation = { rx, ry, rz, rw };
        j.pose_scale = { sx, sy, sz, sw };
        if (!reader.readString(j.joint_name)) {
            std::cout << ind << "[JONT] readString name failed\n";
            return false;
        }
        node.joints.push_back(std::move(j));
        if (coreParams) std::cout << ind << "[JONT] joint='" << node.joints.back().joint_name
                                  << "' idx=" << node.joints.back().joint_idx
                                  << " parent=" << node.joints.back().parent_joint_idx << "\n";
        return true;
    }

    // SHAPE NODE
    if (tag == "MESH" || tag == "HSEM") {
        std::string s;
        if (!reader.readString(s)) {
            std::cout << ind << "[MESH] readString failed\n"; return false;
        }
        if (coreParams) std::cout << ind << "[MESH] mesh_ressource_id='" << s << "'\n";
        node.mesh_ressource_id = s;
        return true;
    }
    else if (tag == "IRGP") {
        int32_t v;
        if (!reader.readI32(v)) { std::cout << ind << "[IRGP] readI32 failed\n"; return false; }
        if (coreParams) std::cout << ind << "[IRGP] primitive_group_idx=" << v << "\n";
        node.primitive_group_idx = v;
        return true;
    }

    // StateNode tags
    if (tag == "RDHS" || tag == "SHDR") {
        std::string shader;
        if (!reader.readString(shader)) { std::cout << ind << "[RDHS] readString failed\n"; return false; }
       if (coreParams)  std::cout << ind << "[RDHS] shader='" << shader << "'\n";
        node.shader = shader;
        return true;
    }
    else if (tag == "TXTS" || tag == "STXT") {
        std::string param, value;
        if (!reader.readString(param) || !reader.readString(value)) { std::cout << ind << "[TXTS] readString failed\n"; return false; }
        if (coreParams) std::cout << ind << "[TXTS] param='" << param << "' value='" << value << "'\n";
        // DEBUG: Log all texture parameters
        std::cout << "[PARSER] TXTS param='" << param << "' value='" << value << "'\n";
        node.shader_params_texture[param] = value;
        return true;
    }
    else if (tag == "TNIS" || tag == "SINT") {
        std::string param;
        int32_t value;
        if (!reader.readString(param) || !reader.readI32(value)) { std::cout << ind << "[TNIS] read failed\n"; return false; }
       if (coreParams)  std::cout << ind << "[TNIS] param='" << param << "' value=" << value << "\n";
        node.shader_params_int[param] = value;
        return true;
    }
    else if (tag == "TLFS" || tag == "SFLT") {
        std::string param;
        float value;
        if (!reader.readString(param) || !reader.readF32(value)) { std::cout << ind << "[TLFS] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[TLFS] param='" << param << "' value=" << value << "\n";
        node.shader_params_float[param] = value;
        return true;
    }
    else if (tag == "CEVS" || tag == "SVEC") {
        std::string param;
        float x, y, z, w;
        if (!reader.readString(param) || !reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) { std::cout << ind << "[SVEC] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[SVEC] param='" << param << "' vec4=(" << x << "," << y << "," << z << "," << w << ")\n";
        node.shader_params_vec4[param] = glm::vec4(x, y, z, w);
        return true;
    }
    else if (tag == "SUTS" || tag == "STUS") {
        int32_t index;
        float x, y, z, w;
        if (!reader.readI32(index) || !reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) { std::cout << ind << "[SUTS] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[SUTS] index=" << index << " vec4=(" << x << "," << y << "," << z << "," << w << ")\n";
        node.shader_params_mlp_uv_stretch[index] = glm::vec4(x, y, z, w);
        return true;
    }
    else if (tag == "IPSS" || tag == "SSPI") {
        int32_t index;
        float x, y, z, w;
        if (!reader.readI32(index) || !reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) { std::cout << ind << "[IPSS] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[IPSS] index=" << index << " vec4=(" << x << "," << y << "," << z << "," << w << ")\n";
        node.shader_params_mlp_spec_intensity[index] = glm::vec4(x, y, z, w);
        return true;
    }
    else if (tag == "SLCB" || tag == "BCLS")
    {
        // StateLocalBox - local bounding box for state nodes (like decals)
        float cx, cy, cz, ex, ey, ez;
        if (!reader.readF32(cx) || !reader.readF32(cy) || !reader.readF32(cz)) return false;
        if (!reader.readF32(ex) || !reader.readF32(ey) || !reader.readF32(ez)) return false;

        auto r1 = ValidationLayer::validate(cx, "SLCB.center.x", 1e6f, ind);
        auto r2 = ValidationLayer::validate(cy, "SLCB.center.y", 1e6f, ind);
        auto r3 = ValidationLayer::validate(cz, "SLCB.center.z", 1e6f, ind);
        auto r4 = ValidationLayer::validateExtent(ex, "SLCB.extents.x", ind);
        auto r5 = ValidationLayer::validateExtent(ey, "SLCB.extents.y", ind);
        auto r6 = ValidationLayer::validateExtent(ez, "SLCB.extents.z", ind);
        ValidationLayer::printSummary("SLCB", {r1, r2, r3, r4, r5, r6}, ind);

        if (fabsf(ez) < 1e-6f) ez = 0.0f;

        node.local_box_min = glm::vec4(cx - ex, cy - ey, cz - ez, 1.0f);
        node.local_box_max = glm::vec4(cx + ex, cy + ey, cz + ez, 1.0f);

        if (coreParams) std::cout << ind << "[SLCB] center=("
                  << cx << "," << cy << "," << cz << ",1) extents=("
                  << ex << "," << ey << "," << ez << ",0)\n";
        return true;
    }
    // ====================================================================

    // TransformNode tags
    if (tag == "ISOP" || tag == "POSI") {
        float x, y, z;
        if (!reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z)) { std::cout << ind << "[POSI] read failed\n"; return false; }

        VALIDATE(x, "POSI.x");
        VALIDATE(y, "POSI.y");
        VALIDATE(z, "POSI.z");

        if (coreParams) std::cout << ind << "[POSI] position=(" << x << "," << y << "," << z << ",1)\n";
        node.position = glm::vec4(x, y, z, 1.0f);  // w=1.0 for point
        return true;
    }
    else if (tag == "NTOR" || tag == "ROTN") {
        float x, y, z, w;
        if (!reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) { std::cout << ind << "[ROTN] read failed\n"; return false; }

        VALIDATE_QUAT(x, "ROTN.x");
        VALIDATE_QUAT(y, "ROTN.y");
        VALIDATE_QUAT(z, "ROTN.z");
        VALIDATE_QUAT(w, "ROTN.w");

        if (coreParams) std::cout << ind << "[ROTN] rotation=(" << x << "," << y << "," << z << "," << w << ")\n";
        node.rotation = glm::vec4(x, y, z, w);
        return true;
    }
    else if (tag == "LACS" || tag == "SCAL") {
        float x, y, z;
        if (!reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z)) { std::cout << ind << "[SCAL] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[SCAL] scale=(" << x << "," << y << "," << z << ",1)\n";
        node.scale = glm::vec4(x, y, z, 1.0f);
        return true;
    }
    else if (tag == "VIPR" || tag == "RPIV") {
        float x, y, z;
        if (!reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z)) { std::cout << ind << "[RPIV] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[RPIV] rotate_pivot=(" << x << "," << y << "," << z << ",1)\n";
        node.rotate_pivot = glm::vec4(x, y, z, 1.0f);
        return true;
    }
    else if (tag == "VIPS" || tag == "SPIV") {
        float x, y, z;
        if (!reader.readF32(x) || !reader.readF32(y) || !reader.readF32(z)) { std::cout << ind << "[SPIV] read failed\n"; return false; }
        if (coreParams) std::cout << ind << "[SPIV] scale_pivot=(" << x << "," << y << "," << z << ",1)\n";
        node.scale_pivot = glm::vec4(x, y, z, 1.0f);
        return true;
    }
    else if (tag == "PSVS" || tag == "SVSP") {
        bool val;
        if (!reader.readBool(val)) { std::cout << ind << "[PSVS] readBool failed\n"; return false; }
        // if (coreParams) std::cout << ind << "[PSVS] val=" << val << "\n";
        // node.in_view_space = val;
        return true;
    }
    else if (tag == "VKLS" || tag == "SLKV") {
        bool val;
        if (!reader.readBool(val)) { std::cout << ind << "[VKLS] readBool failed\n"; return false; }
        if (coreParams) std::cout << ind << "[VKLS] val=" << val << "\n";
        // node.locked_to_viewer = val;
        return true;
    }
    else if (tag == "DIMS" || tag == "SMID") {
        float val;
        if (!reader.readF32(val)) { std::cout << ind << "[DIMS] readF32 failed\n"; return false; }
        if (coreParams) std::cout << ind << "[DIMS] min_distance=" << val << "\n";
        node.min_distance = val;
        return true;
    }
    else if (tag == "DAMS" || tag == "SMAD") {
        float val;
        if (!reader.readF32(val)) { std::cout << ind << "[DAMS] readF32 failed\n"; return false; }
        if (coreParams) std::cout << ind << "[DAMS] max_distance=" << val << "\n";
        node.max_distance = val;
        return true;
    }

    // MODEL NODE
    else if (tag == "XOBL" || tag == "LBOX") {
        float cx, cy, cz, ex, ey, ez;
        if (!reader.readF32(cx) || !reader.readF32(cy) || !reader.readF32(cz)) return false;
        if (!reader.readF32(ex) || !reader.readF32(ey) || !reader.readF32(ez)) return false;

        auto r1 = ValidationLayer::validate(cx, "LBOX.center.x", 1e6f, ind);
        auto r2 = ValidationLayer::validate(cy, "LBOX.center.y", 1e6f, ind);
        auto r3 = ValidationLayer::validate(cz, "LBOX.center.z", 1e6f, ind);
        auto r4 = ValidationLayer::validateExtent(ex, "LBOX.extents.x", ind);
        auto r5 = ValidationLayer::validateExtent(ey, "LBOX.extents.y", ind);
        auto r6 = ValidationLayer::validateExtent(ez, "LBOX.extents.z", ind);
        ValidationLayer::printSummary("LBOX", {r1, r2, r3, r4, r5, r6}, ind);

        if (fabsf(ez) < 1e-6f) ez = 0.0f;

        node.local_box_min = glm::vec4(cx - ex, cy - ey, cz - ez, 1.0f);
        node.local_box_max = glm::vec4(cx + ex, cy + ey, cz + ez, 1.0f);

        if (coreParams) std::cout << ind << "[LBOX] center=("
                  << cx << "," << cy << "," << cz << ",1) extents=("
                  << ex << "," << ey << "," << ez << ",0)\n";
        return true;
    }
    else if (tag == "PTNM" || tag == "MNTP") {
        std::string type_name;
        if (!reader.readString(type_name)) { std::cout << ind << "[PTNM] readString failed\n"; return false; }
        if (coreParams) std::cout << ind << "[PTNM] type_name='" << type_name << "'\n";
        node.model_node_type = type_name;
        return true;
    }
    else if (tag == "ATSS" || tag == "SSTA") {
        std::string key, value;
        if (!reader.readString(key) || !reader.readString(value)) { std::cout << ind << "[ATSS] readString failed\n"; return false; }
        if (coreParams) std::cout << ind << "[ATSS] key='" << key << "' value='" << value << "'\n";
        node.string_attrs[key] = value;
        return true;
    }

    // REMAINING
    else if (tag == "HCRH" || tag == "HRCH") {
        bool val;
        if (!reader.readBool(val)) { std::cout << ind << "[HCRH] readBool failed\n"; return false; }
        if (coreParams) std::cout << ind << "[HCRH] val=" << val << "\n";
        node.hrch_flag = val;
        return true;
    }
    else if (tag == "HSAC" || tag == "CASH") {
        bool val;
        if (!reader.readBool(val)) { std::cout << ind << "[HSAC] readBool failed\n"; return false; }
        if (coreParams) std::cout << ind << "[HSAC] val=" << val << "\n";
        node.cash_flag = val;
        return true;
    }
    else {
        if (is_valid_fourcc(tag)) {
            std::cout << ind << "[UNKNOWN] tag='" << tag << "'\n";
            rep.report(Reporter::Err, "Unknown data tag '" + tag + "' in node '" + node.node_name + "' (type: " + node.node_type + ")");
        }
        return false;
    }
}
