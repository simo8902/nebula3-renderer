// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "ParticleSystemNode.h"
#include "particlesystem_loader.h"
#include "Mesh.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "Servers/TextureServer.h"

namespace Particles {

ParticleSystemNode::ParticleSystemNode() = default;

ParticleSystemNode::~ParticleSystemNode() {
    Discard();
}

bool ParticleSystemNode::Setup(const Node* node, Mesh* mesh) {
    if (!node) {
        std::cerr << "[PARTICLE INIT] skipped: null node\n";
        return false;
    }

    if (!node->model_node_type.empty()) {
        std::string t = node->model_node_type;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (t.find("add") != std::string::npos) {
            blendMode = ParticleBlendMode::Additive;
        } else {
            blendMode = ParticleBlendMode::Alpha;
        }
    }

    auto itFloat = node->shader_params_float.find("Intensity0");
    if (itFloat != node->shader_params_float.end()) intensity0 = itFloat->second;
    itFloat = node->shader_params_float.find("Intensity1");
    if (itFloat != node->shader_params_float.end()) {
        intensity1 = itFloat->second;
        animFramesPerSecond = itFloat->second;
    }
    itFloat = node->shader_params_float.find("MatEmissiveIntensity");
    if (itFloat != node->shader_params_float.end()) emissiveIntensity = itFloat->second;

    auto itInt = node->shader_params_int.find("AlphaRef");
    if (itInt != node->shader_params_int.end()) {
        numAnimPhases = std::max(1, itInt->second);
    } else {
        itFloat = node->shader_params_float.find("AlphaRef");
        if (itFloat != node->shader_params_float.end()) {
            numAnimPhases = std::max(1, (int)std::lround(itFloat->second));
        }
    }

    EmitterMesh emitterMesh;
    if (mesh && !mesh->verts.empty() && !mesh->idx.empty()) {
        std::vector<uint8_t> vertexData(mesh->verts.size() * sizeof(ObjVertex));
        std::memcpy(vertexData.data(), mesh->verts.data(), vertexData.size());

        std::vector<uint16_t> indices16;
        indices16.reserve(mesh->idx.size());
        for (uint32_t idx : mesh->idx) indices16.push_back((uint16_t)idx);

        emitterMesh.Setup(
            vertexData,
            sizeof(ObjVertex),
            offsetof(ObjVertex, px),
            offsetof(ObjVertex, nx),
            offsetof(ObjVertex, tx),
            indices16,
            node->primitive_group_idx >= 0 && node->primitive_group_idx < (int)mesh->groups.size()
                ? mesh->groups[node->primitive_group_idx].firstIndex()
                : 0,
            node->primitive_group_idx >= 0 && node->primitive_group_idx < (int)mesh->groups.size()
                ? mesh->groups[node->primitive_group_idx].indexCount()
                : mesh->idx.size()
        );
    } else {
        // Fallback for meshless emitters: synthesize a tiny local triangle emitter.
        const glm::vec3 bmin(node->local_box_min.x, node->local_box_min.y, node->local_box_min.z);
        const glm::vec3 bmax(node->local_box_max.x, node->local_box_max.y, node->local_box_max.z);
        const glm::vec3 center = (bmin + bmax) * 0.5f;
        const glm::vec3 extents = glm::abs(bmax - bmin) * 0.5f;
        float r = std::max(extents.x, std::max(extents.y, extents.z));
        if (r < 0.05f) r = 0.05f;

        std::array<ObjVertex, 3> fallbackVerts{};
        fallbackVerts[0].px = center.x - r; fallbackVerts[0].py = center.y - r; fallbackVerts[0].pz = center.z;
        fallbackVerts[1].px = center.x + r; fallbackVerts[1].py = center.y - r; fallbackVerts[1].pz = center.z;
        fallbackVerts[2].px = center.x;     fallbackVerts[2].py = center.y + r; fallbackVerts[2].pz = center.z;
        for (auto& v : fallbackVerts) {
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f;
        }

        std::vector<uint8_t> vertexData(fallbackVerts.size() * sizeof(ObjVertex));
        std::memcpy(vertexData.data(), fallbackVerts.data(), vertexData.size());
        std::vector<uint16_t> indices16 = {0, 1, 2};

        emitterMesh.Setup(
            vertexData,
            sizeof(ObjVertex),
            offsetof(ObjVertex, px),
            offsetof(ObjVertex, nx),
            offsetof(ObjVertex, tx),
            indices16,
            0,
            static_cast<uint32_t>(indices16.size())
        );
    }

    if (!emitterMesh.IsValid()) {
        std::cerr << "[PARTICLE INIT] skipped: node '" << node->node_name
                  << "' emitter mesh invalid (group=" << node->primitive_group_idx << ")\n";
        return false;
    }

    EmitterAttrs attrs;
    attrs.SetEnvelope(EmitterAttrs::EmissionFrequency, node->emission_frequency);
    attrs.SetEnvelope(EmitterAttrs::LifeTime, node->lifetime);
    attrs.SetEnvelope(EmitterAttrs::SpreadMin, node->spread_min);
    attrs.SetEnvelope(EmitterAttrs::SpreadMax, node->spread_max);
    attrs.SetEnvelope(EmitterAttrs::StartVelocity, node->start_velocity);
    attrs.SetEnvelope(EmitterAttrs::RotationVelocity, node->rotation_velocity);
    attrs.SetEnvelope(EmitterAttrs::Size, node->particle_size);
    attrs.SetEnvelope(EmitterAttrs::Mass, node->particle_mass);
    attrs.SetEnvelope(EmitterAttrs::TimeManipulator, node->time_manipulator);
    attrs.SetEnvelope(EmitterAttrs::VelocityFactor, node->velocity_factor);
    attrs.SetEnvelope(EmitterAttrs::AirResistance, node->air_resistance);
    attrs.SetEnvelope(EmitterAttrs::Red, node->color_red);
    attrs.SetEnvelope(EmitterAttrs::Green, node->color_green);
    attrs.SetEnvelope(EmitterAttrs::Blue, node->color_blue);
    attrs.SetEnvelope(EmitterAttrs::Alpha, node->color_alpha);


    float emissionDuration = node->particle_emission_duration;
    if (!std::isfinite(emissionDuration) || emissionDuration <= 0.0f) {
        emissionDuration = 1.0f;
    }
    attrs.SetFloat(EmitterAttrs::EmissionDuration,     emissionDuration);
    attrs.SetFloat(EmitterAttrs::ActivityDistance,     node->particle_activity_distance);
    attrs.SetFloat(EmitterAttrs::StartRotationMin,     node->start_rotation_min);
    attrs.SetFloat(EmitterAttrs::StartRotationMax,     node->start_rotation_max);
    attrs.SetFloat(EmitterAttrs::Gravity,              node->particle_gravity);
    attrs.SetFloat(EmitterAttrs::ParticleStretch,      node->particle_stretch);
    attrs.SetFloat(EmitterAttrs::TextureTile,          node->texture_tile);
    attrs.SetFloat(EmitterAttrs::VelocityRandomize,    node->velocity_randomize);
    attrs.SetFloat(EmitterAttrs::RotationRandomize,    node->rotation_randomize);
    attrs.SetFloat(EmitterAttrs::SizeRandomize,        node->size_randomize);
    attrs.SetFloat(EmitterAttrs::PrecalcTime,          node->precalc_time);
    attrs.SetFloat(EmitterAttrs::StartDelay,           node->start_delay);

    attrs.SetBool(EmitterAttrs::Looping,               node->particle_looping || node->particle_emission_duration <= 0.0f);
    attrs.SetBool(EmitterAttrs::RenderOldestFirst,     node->render_oldest_first);
    attrs.SetBool(EmitterAttrs::Billboard,             node->particle_billboard);
    attrs.SetBool(EmitterAttrs::StretchToStart,        node->stretch_to_start);
    attrs.SetBool(EmitterAttrs::RandomizeRotation,     node->randomize_rotation);
    attrs.SetBool(EmitterAttrs::ViewAngleFade,         node->view_angle_fade);

    attrs.SetInt(EmitterAttrs::StretchDetail,          node->stretch_detail);

    particleSystem = std::make_shared<ParticleSystem>();
    particleSystem->Setup(emitterMesh, attrs);

    instance = std::make_shared<GLParticleSystemInstance>();
    instance->Setup(particleSystem);
    if (instance && instance->IsValid()) {
        instance->Start();
    }

    const bool ok = instance && instance->IsValid();
    if (!ok) {
        std::cerr << "[PARTICLE INIT] skipped: node '" << node->node_name
                  << "' GL particle instance invalid after setup\n";
    }
    return ok;
}

void ParticleSystemNode::Discard() {
    if (instance) {
        instance->Discard();
        instance = nullptr;
    }
    particleSystem = nullptr;
}

} // namespace Particles
