// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Parser.h"
#include "ValidationLayer.h"
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

            auto new_node = std::make_unique<Node>();
            new_node->node_type = classFourCC;
            new_node->node_name = node_name;
            new_node->node_parent = nodeStackObj.empty() ? nullptr : nodeStackObj.back();

            Node* raw = new_node.get();
            if (raw->node_parent) raw->node_parent->node_children.push_back(raw);
            n3node_list.push_back(raw);
            n3node_storage.push_back(std::move(new_node));
            nodeStackObj.push_back(raw);
        }
        else if (tag == "<MND" || tag == "DNM<") {
            if (!nodeStackObj.empty()) {
                std::string ind(nodeStackObj.size() * 2, ' ');
                if (coreParams) std::cout << ind << "<MND '" << nodeStackObj.back()->node_name << "'\n";
                nodeStackObj.pop_back();
            }
        }
        else {
            if (!nodeStackObj.empty()) {
                std::string ind((nodeStackObj.size() + 1) * 2, ' ');
                parse_node_tag(reader, tag, *nodeStackObj.back(), ind);
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

    // PARTICLES
    else if (tag == "QRFE" || tag == "EFRQ") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;

        const float kEmissionFreqScale = 4.0f;
        float glowBoost = 1.0f;
        if (!node.model_node_type.empty()) {
            std::string t = node.model_node_type;
            std::ranges::transform(t, t.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (t.find("add") != std::string::npos) {
                glowBoost = 2.0f;
            }
        }
        const float totalScale = kEmissionFreqScale * glowBoost;
        v0 *= totalScale;
        v1 *= totalScale;
        v2 *= totalScale;
        v3 *= totalScale;
        if (particleParams) std::cout << ind << "[emission_frequency] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
                 << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
                 << " modFunc=" << modFunc << "\n";

        node.emission_frequency.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "TFLP" || tag == "PLFT") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;

        if (particleParams) std::cout << ind << "[lifetime] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";

        node.lifetime.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "NMSP" || tag == "PSMN") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams) std::cout << ind << "[spread_min] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
            << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
            << " modFunc=" << modFunc << "\n";
        node.spread_min.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "XMSP" || tag == "PSMX") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)  std::cout << ind << "[spread_max] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
            << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
            << " modFunc=" << modFunc << "\n";
        node.spread_max.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "LVSP" || tag == "PSVL") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams) std::cout << ind << "[start_velocity] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.start_velocity.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "LVRP" || tag == "PRVL") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams) std::cout << ind << "[rotation_velocity] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.rotation_velocity.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "EZSP" || tag == "PSZE") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        v0 = 0.3f;
        v1 = 0.5f;
        v2 = 0.5f;
        v3 = 0.3f;
        if (particleParams)  std::cout << ind << "[particle_size] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.particle_size.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "SSMP" || tag == "PMSS") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)   std::cout << ind << "[particle_mass] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
            << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
            << " modFunc=" << modFunc << "\n";
        node.particle_mass.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "NMTP" || tag == "PTMN") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)  std::cout << ind << "[TimeManipulator] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
            << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
            << " modFunc=" << modFunc << "\n";
        node.time_manipulator.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "FLVP" || tag == "PVLF") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)  std::cout << ind << "[VelocityFactor] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.velocity_factor.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "RIAP" || tag == "PAIR") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams) std::cout << ind << "[AirResistance] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.air_resistance.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "DERP" || tag == "PRED") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)  std::cout << ind << "[RED] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.color_red.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "NRGP" || tag == "PGRN") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)   std::cout << ind << "[Green] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
            << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
            << " modFunc=" << modFunc << "\n";
        node.color_green.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "ULBP" || tag == "PBLU") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams) std::cout << ind << "[BLUE] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
          << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
          << " modFunc=" << modFunc << "\n";
        node.color_blue.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "PLAP" || tag == "PALP") {
        float v0, v1, v2, v3, kp0, kp1, freq, amp;
        int32_t modFunc;
        if (!reader.readF32(v0) || !reader.readF32(v1) || !reader.readF32(v2) || !reader.readF32(v3) ||
            !reader.readF32(kp0) || !reader.readF32(kp1) || !reader.readF32(freq) || !reader.readF32(amp) ||
            !reader.readI32(modFunc)) return false;
        if (particleParams)  std::cout << ind << "[ALPHA] v0=" << v0 << " v1=" << v1 << " v2=" << v2 << " v3=" << v3
           << " kp0=" << kp0 << " kp1=" << kp1 << " freq=" << freq << " amp=" << amp
           << " modFunc=" << modFunc << "\n";
        node.color_alpha.Setup(v0, v1, v2, v3, kp0, kp1, freq, amp, (Particles::EnvelopeCurve::ModFunc)modFunc);
        return true;
    }
    else if (tag == "UDEP" || tag == "PEDU") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] emission_duration=" << v << "\n";
        node.particle_emission_duration = v;
        return true;
    }
    else if (tag == "EPLP" || tag == "PLPE") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] looping=" << v << "\n";
        node.particle_looping = (v == 1);
        return true;
    }
    else if (tag == "DCAP" || tag == "PACD") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] activity_distance=" << v << "\n";
        node.particle_activity_distance = v;
        return true;
    }
    else if (tag == "FORP" || tag == "PROF") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] render_oldest_first=" << v << "\n";
        node.render_oldest_first = (v != 0);
        return true;
    }
    else if (tag == "OBBP" || tag == "PBBO") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] billboard=" << v << "\n";
        node.particle_billboard = (v != 0);
        return true;
    }
    else if (tag == "NMRP" || tag == "PRMN") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] start_rotation_min=" << v << "\n";
        node.start_rotation_min = v;
        return true;
    }
    else if (tag == "XMRP" || tag == "PRMX") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] start_rotation_max=" << v << "\n";
        node.start_rotation_max = v;
        return true;
    }
    else if (tag == "VRGP" || tag == "PGRV") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] gravity=" << v << "\n";
        node.particle_gravity = v;
        return true;
    }
    else if (tag == "CTSP" || tag == "PSTC") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] particle_stretch=" << v << "\n";
        node.particle_stretch = v;
        return true;
    }
    else if (tag == "XTTP" || tag == "PTTX") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] texture_tile=" << v << "\n";
        node.texture_tile = v;
        return true;
    }
    else if (tag == "STSP" || tag == "PSTS") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] stretch_to_start=" << v << "\n";
        node.stretch_to_start = (v != 0);
        return true;
    }
    else if (tag == "MRVP" || tag == "PVRM") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] velocity_randomize=" << v << "\n";
        node.velocity_randomize = v;
        return true;
    }
    else if (tag == "MRRP" || tag == "PRRM") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] rotation_randomize=" << v << "\n";
        node.rotation_randomize = v;
        return true;
    }
    else if (tag == "MRSP" || tag == "PSRM") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] size_randomize=" << v << "\n";
        node.size_randomize = v;
        return true;
    }
    else if (tag == "TCPP" || tag == "PPCT") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] precalc_time=" << v << "\n";
        node.precalc_time = v;
        return true;
    }
    else if (tag == "DRRP" || tag == "PRRD") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] randomize_rotation=" << v << "\n";
        node.randomize_rotation = (v != 0);
        return true;
    }
    else if (tag == "LDSP" || tag == "PSDL") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] stretch_detail=" << v << "\n";
        node.stretch_detail = v;
        return true;
    }
    else if (tag == "FAVP" || tag == "PVAF") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] view_angle_fade=" << v << "\n";
        node.view_angle_fade = (v != 0);
        return true;
    }
    else if (tag == "LEDP" || tag == "PDEL") {
        float v;
        if (!reader.readF32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] start_delay=" << v << "\n";
        node.start_delay = v;
        return true;
    }
    else if (tag == "CSLP" || tag == "PLSC") {
        int32_t v;
        if (!reader.readI32(v)) return false;
        if (particleParams) std::cout << ind << "[" << tag << "] looping=" << v << "\n";
        node.curve_looping = (v == 1);
        return true;
    }

    // TODO: ANIM TAGS ref: animatornode.cc
    // AnimatorNode tags
    else if (tag == "BASE" || tag == "ESAB")
    {
        // Animation section base - creates new AnimSection
        int32_t nodeType;
        if (!reader.readI32(nodeType)) { std::cout << ind << "[BASE] readI32 failed\n"; return false; }
        if (coreParams) std::cout << ind << "[BASE] animationNodeType=" << nodeType << "\n";

        AnimSection animSec;
        animSec.animationNodeType = static_cast<AnimNodeType>(nodeType);
        node.animSections.push_back(animSec);
        return true;
    }
    else if (tag == "SLPT" || tag == "TPLS")
    {
        // LoopType
        if (node.animSections.empty()) { std::cout << ind << "[SLPT] no AnimSection\n"; return false; }
        std::string loopTypeStr;
        if (!reader.readString(loopTypeStr)) { std::cout << ind << "[SLPT] readString failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        if (loopTypeStr == "clamp") {
            curAnimSection.loopType = AnimLoopType::Clamp;
        } else if (loopTypeStr == "loop") {
            curAnimSection.loopType = AnimLoopType::Loop;
        }
        if (coreParams) std::cout << ind << "[SLPT] loopType='" << loopTypeStr << "'\n";
        return true;
    }
    else if (tag == "ANNO" || tag == "ONNA")
    {
        // AnimationPath - animated nodes path
        if (node.animSections.empty()) { std::cout << ind << "[ANNO] no AnimSection\n"; return false; }
        std::string path;
        if (!reader.readString(path)) { std::cout << ind << "[ANNO] readString failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        curAnimSection.animatedNodesPath.push_back(path);
        if (coreParams) std::cout << ind << "[ANNO] animatedNodesPath='" << path << "'\n";
        return true;
    }
    else if (tag == "SPNM" || tag == "MNPS" || tag == "SVCN" || tag == "NCVS")
    {
        // ShaderVariable semantic
        if (node.animSections.empty()) { std::cout << ind << "[SPNM] no AnimSection\n"; return false; }
        std::string semName;
        if (!reader.readString(semName)) { std::cout << ind << "[SPNM] readString failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        curAnimSection.shaderVarSemantic = semName;
        if (coreParams) std::cout << ind << "[SPNM] shaderVarSemantic='" << semName << "'\n";
        return true;
    }
    else if (tag == "ADPK" || tag == "KPDA")
    {
        // PositionKey
        if (node.animSections.empty()) { std::cout << ind << "[ADPK] no AnimSection\n"; return false; }
        int32_t animKeySize;
        if (!reader.readI32(animKeySize)) { std::cout << ind << "[ADPK] readI32 failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        if (coreParams) std::cout << ind << "[ADPK] animKeySize=" << animKeySize << "\n";

        for (int32_t i = 0; i < animKeySize; i++) {
            AnimKey<glm::vec4> key;

            if (curAnimSection.animationNodeType == UvAnimator) {
                int32_t layer;
                float time, u, v, skip1, skip2;
                if (!reader.readI32(layer) || !reader.readF32(time) ||
                    !reader.readF32(u) || !reader.readF32(v) ||
                    !reader.readF32(skip1) || !reader.readF32(skip2)) {
                    std::cout << ind << "[ADPK] UvAnimator read failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(u, v, 0.0f, 0.0f));
                curAnimSection.layer.push_back(layer);
            } else {
                float time, x, y, z, w;
                if (!reader.readF32(time) || !reader.readF32(x) ||
                    !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) {
                    std::cout << ind << "[ADPK] TransformAnimator read failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(x, y, z, w));
            }
            curAnimSection.posArray.push_back(key);
        }
        return true;
    }
    else if (tag == "ADEK" || tag == "KEDA")
    {
        // EulerKey
        if (node.animSections.empty()) { std::cout << ind << "[ADEK] no AnimSection\n"; return false; }
        int32_t animKeySize;
        if (!reader.readI32(animKeySize)) { std::cout << ind << "[ADEK] readI32 failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        if (coreParams) std::cout << ind << "[ADEK] animKeySize=" << animKeySize << "\n";

        for (int32_t i = 0; i < animKeySize; i++) {
            AnimKey<glm::vec4> key;

            if (curAnimSection.animationNodeType == UvAnimator) {
                int32_t layer;
                float time, u, v, skip1, skip2;
                if (!reader.readI32(layer) || !reader.readF32(time) ||
                    !reader.readF32(u) || !reader.readF32(v) ||
                    !reader.readF32(skip1) || !reader.readF32(skip2)) {
                    std::cout << ind << "[ADEK] UvAnimator read failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(u, v, 0.0f, 0.0f));
                curAnimSection.layer.push_back(layer);
            } else {
                float time, x, y, z, w;
                if (!reader.readF32(time) || !reader.readF32(x) ||
                    !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) {
                    std::cout << ind << "[ADEK] TransformAnimator read failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(x, y, z, w));
            }
            curAnimSection.eulerArray.push_back(key);
        }
        return true;
    }
    else if (tag == "ADSK" || tag == "KSDA")
    {
        // ScaleKey
        if (node.animSections.empty()) { std::cout << ind << "[ADSK] no AnimSection\n"; return false; }
        int32_t animKeySize;
        if (!reader.readI32(animKeySize)) { std::cout << ind << "[ADSK] readI32 failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        if (coreParams) std::cout << ind << "[ADSK] animKeySize=" << animKeySize << "\n";

        for (int32_t i = 0; i < animKeySize; i++) {
            AnimKey<glm::vec4> key;

            if (curAnimSection.animationNodeType == UvAnimator) {
                int32_t layer;
                float time, u, v, skip1, skip2;
                if (!reader.readI32(layer) || !reader.readF32(time) ||
                    !reader.readF32(u) || !reader.readF32(v) ||
                    !reader.readF32(skip1) || !reader.readF32(skip2)) {
                    std::cout << ind << "[ADSK] UvAnimator read failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(u, v, 0.0f, 0.0f));
                curAnimSection.layer.push_back(layer);
            } else {
                float time, x, y, z, w;
                if (!reader.readF32(time) || !reader.readF32(x) ||
                    !reader.readF32(y) || !reader.readF32(z) || !reader.readF32(w)) {
                    std::cout << ind << "[ADSK] TransformAnimator read failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(x, y, z, w));
            }
            curAnimSection.scaleArray.push_back(key);
        }
        return true;
    }
    else if (tag == "SANI" || tag == "INAS")
    {
        // Animation name
        if (node.animSections.empty()) { std::cout << ind << "[SANI] no AnimSection\n"; return false; }
        std::string animName;
        if (!reader.readString(animName)) { std::cout << ind << "[SANI] readString failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        curAnimSection.animationName = animName;
        if (coreParams) std::cout << ind << "[SANI] animationName='" << animName << "'\n";
        return true;
    }
    else if (tag == "SAGR" || tag == "RGAS")
    {
        // AnimationGroup
        if (node.animSections.empty()) { std::cout << ind << "[SAGR] no AnimSection\n"; return false; }
        int32_t animGroup;
        if (!reader.readI32(animGroup)) { std::cout << ind << "[SAGR] readI32 failed\n"; return false; }

        AnimSection& curAnimSection = node.animSections.back();
        curAnimSection.animationGroup = animGroup;
        if (coreParams) std::cout << ind << "[SAGR] animationGroup=" << animGroup << "\n";
        return true;
    }
    else if (tag == "ADDK" || tag == "KDDA")
    {
        // Dynamic key - supports Float, Float4, Int
        if (node.animSections.empty()) { std::cout << ind << "[ADDK] no AnimSection\n"; return false; }
        std::string valueType;
        int32_t animKeySize;
        if (!reader.readString(valueType) || !reader.readI32(animKeySize)) {
            std::cout << ind << "[ADDK] read failed\n";
            return false;
        }

        AnimSection& curAnimSection = node.animSections.back();
        if (coreParams) std::cout << ind << "[ADDK] valueType='" << valueType << "' size=" << animKeySize << "\n";

        for (int32_t i = 0; i < animKeySize; i++) {
            float time;
            if (!reader.readF32(time)) {
                std::cout << ind << "[ADDK] readF32 time failed at " << i << "\n";
                return false;
            }

            if (valueType == "Float") {
                AnimKey<float> key;
                float value;
                if (!reader.readF32(value)) {
                    std::cout << ind << "[ADDK] readF32 value failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(value);
                curAnimSection.floatKeyArray.push_back(key);
            }
            else if (valueType == "Float4") {
                AnimKey<glm::vec4> key;
                float x, y, z, w;
                if (!reader.readF32(x) || !reader.readF32(y) ||
                    !reader.readF32(z) || !reader.readF32(w)) {
                    std::cout << ind << "[ADDK] readF32 Float4 failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(glm::vec4(x, y, z, w));
                curAnimSection.float4KeyArray.push_back(key);
            }
            else if (valueType == "Int") {
                AnimKey<int32_t> key;
                int32_t value;
                if (!reader.readI32(value)) {
                    std::cout << ind << "[ADDK] readI32 value failed at " << i << "\n";
                    return false;
                }
                key.SetTime(time);
                key.SetValue(value);
                curAnimSection.intKeyArray.push_back(key);
            }
            else {
                std::cout << ind << "[ADDK] Unknown key type: " << valueType << "\n";
                return false;
            }
        }

        // UVAnimator specific: set shader semantic to TextureTransform0
        if (curAnimSection.animationNodeType == UvAnimator && curAnimSection.shaderVarSemantic.empty()) {
            curAnimSection.shaderVarSemantic = "TextureTransform0";
        }
        return true;
    }
    else if (tag == "ANIM" || tag == "MINA")
    {
        // Character/transform animation resource path (e.g. ani:...nax3).
        const auto pos = reader.tell();
        std::string animRes;
        if (!reader.readString(animRes)) {
            // Some files may use ANIM as a marker-only tag.
            reader.seek(pos);
            return true;
        }
        if (coreParams) std::cout << ind << "[ANIM] animation_resource='" << animRes << "'\n";
        node.animation_resource = animRes;
        return true;
    }
    else if (tag == "TRAV" || tag == "VART")
    {
        // Variation animation resource path.
        const auto pos = reader.tell();
        std::string varRes;
        if (!reader.readString(varRes)) {
            reader.seek(pos);
            return true;
        }
        if (coreParams) std::cout << ind << "[TRAV] variations_resource='" << varRes << "'\n";
        node.variations_resource = varRes;
        return true;
    }

    // TODO:
    else if (tag == "BLBS" || tag == "SBLB") {
        bool val;
        if (!reader.readBool(val)) return false;
        if (coreParams) std::cout << ind << "[BLBS] val=" << val << "\n";
       // node.sblb_flag = val;
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
