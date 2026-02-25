// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARTICLESYSTEM_LOADER_H
#define NDEVC_PARTICLESYSTEM_LOADER_H


#include "emitterattrs.h"
#include "emittermesh.h"
#include <array>
#include <memory>

namespace Particles {

struct ParsedNode {
    std::array<float, 9> emission_frequency;
    std::array<float, 9> lifetime;
    std::array<float, 9> spread_min;
    std::array<float, 9> spread_max;
    std::array<float, 9> start_velocity;
    std::array<float, 9> rotation_velocity;
    std::array<float, 9> particle_size;
    std::array<float, 9> particle_mass;
    std::array<float, 9> time_manipulator;
    std::array<float, 9> velocity_factor;
    std::array<float, 9> air_resistance;
    std::array<float, 9> color_red;
    std::array<float, 9> color_green;
    std::array<float, 9> color_blue;
    std::array<float, 9> color_alpha;

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

    bool particle_looping = false;
    bool render_oldest_first = false;
    bool particle_billboard = false;
    bool stretch_to_start = false;
    bool randomize_rotation = false;
    bool view_angle_fade = false;
    int32_t stretch_detail = 0;
};

class ParticleSystemLoader {
public:
    static EmitterAttrs CreateEmitterAttrs(const ParsedNode& node) {
        EmitterAttrs attrs;

        attrs.LoadFromParsedData(node.emission_frequency, EmitterAttrs::EmissionFrequency);
        attrs.LoadFromParsedData(node.lifetime, EmitterAttrs::LifeTime);
        attrs.LoadFromParsedData(node.spread_min, EmitterAttrs::SpreadMin);
        attrs.LoadFromParsedData(node.spread_max, EmitterAttrs::SpreadMax);
        attrs.LoadFromParsedData(node.start_velocity, EmitterAttrs::StartVelocity);
        attrs.LoadFromParsedData(node.rotation_velocity, EmitterAttrs::RotationVelocity);
        attrs.LoadFromParsedData(node.particle_size, EmitterAttrs::Size);
        attrs.LoadFromParsedData(node.particle_mass, EmitterAttrs::Mass);
        attrs.LoadFromParsedData(node.time_manipulator, EmitterAttrs::TimeManipulator);
        attrs.LoadFromParsedData(node.velocity_factor, EmitterAttrs::VelocityFactor);
        attrs.LoadFromParsedData(node.air_resistance, EmitterAttrs::AirResistance);
        attrs.LoadFromParsedData(node.color_red, EmitterAttrs::Red);
        attrs.LoadFromParsedData(node.color_green, EmitterAttrs::Green);
        attrs.LoadFromParsedData(node.color_blue, EmitterAttrs::Blue);
        attrs.LoadFromParsedData(node.color_alpha, EmitterAttrs::Alpha);

        attrs.SetFloat(EmitterAttrs::EmissionDuration, node.particle_emission_duration);
        attrs.SetFloat(EmitterAttrs::ActivityDistance, node.particle_activity_distance);
        attrs.SetFloat(EmitterAttrs::StartRotationMin, node.start_rotation_min);
        attrs.SetFloat(EmitterAttrs::StartRotationMax, node.start_rotation_max);
        attrs.SetFloat(EmitterAttrs::Gravity, node.particle_gravity);
        attrs.SetFloat(EmitterAttrs::ParticleStretch, node.particle_stretch);
        attrs.SetFloat(EmitterAttrs::TextureTile, node.texture_tile);
        attrs.SetFloat(EmitterAttrs::VelocityRandomize, node.velocity_randomize);
        attrs.SetFloat(EmitterAttrs::RotationRandomize, node.rotation_randomize);
        attrs.SetFloat(EmitterAttrs::SizeRandomize, node.size_randomize);
        attrs.SetFloat(EmitterAttrs::PrecalcTime, node.precalc_time);
        attrs.SetFloat(EmitterAttrs::StartDelay, node.start_delay);

        attrs.SetBool(EmitterAttrs::Looping, node.particle_looping);
        attrs.SetBool(EmitterAttrs::RenderOldestFirst, node.render_oldest_first);
        attrs.SetBool(EmitterAttrs::Billboard, node.particle_billboard);
        attrs.SetBool(EmitterAttrs::StretchToStart, node.stretch_to_start);
        attrs.SetBool(EmitterAttrs::RandomizeRotation, node.randomize_rotation);
        attrs.SetBool(EmitterAttrs::ViewAngleFade, node.view_angle_fade);

        attrs.SetInt(EmitterAttrs::StretchDetail, node.stretch_detail);

        return attrs;
    }
};

} // namespace Particles

#endif //NDEVC_PARTICLESYSTEM_LOADER_H
