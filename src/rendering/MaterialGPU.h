// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MATERIALGPU_H
#define NDEVC_MATERIALGPU_H

#include <cstdint>

// GPU-side material struct — must match GLSL std430 layout exactly.
// Indexed per-draw via gl_BaseInstance into binding 2.
struct MaterialGPU {
    uint64_t diffuseHandle;     //  0
    uint64_t specularHandle;    //  8
    uint64_t normalHandle;      // 16
    uint64_t emissiveHandle;    // 24
    float emissiveIntensity;    // 32
    float specularIntensity;    // 36
    float specularPower;        // 40
    float alphaCutoff;          // 44
    uint32_t flags;             // 48
    float bumpScale;            // 52
    float intensity0;           // 56
    float alphaBlendFactor;     // 60
    uint64_t diffMap1Handle;    // 64
    uint64_t specMap1Handle;    // 72
    uint64_t bumpMap1Handle;    // 80
    uint64_t maskMapHandle;     // 88
    uint64_t alphaMapHandle;    // 96
    uint64_t cubeMapHandle;     //104
    float velocityX;            //112
    float velocityY;            //116
    float scale;                //120
    float pad0;                 //124
};
static_assert(sizeof(MaterialGPU) == 128, "MaterialGPU must be 128 bytes for std430");

enum MaterialFlags : uint32_t {
    MATFLAG_ALPHA_TEST      = 1u << 0,
    MATFLAG_TWO_SIDED       = 1u << 1,
    MATFLAG_FLAT_NORMAL     = 1u << 2,
    MATFLAG_RECEIVES_DECALS = 1u << 3,
    MATFLAG_ADDITIVE        = 1u << 4,
    MATFLAG_HAS_SPEC_MAP    = 1u << 5,
};

// GPU-side decal material — must match GLSL std430 layout exactly.
// Indexed per-draw via gl_BaseInstance into binding 4.
struct DecalMaterialGPU {
    uint64_t diffuseHandle;     //  0
    uint64_t emissiveHandle;    //  8
    float    decalScale;        // 16
    uint32_t decalDiffuseMode;  // 20
    float    pad0;              // 24
    float    pad1;              // 28
};
static_assert(sizeof(DecalMaterialGPU) == 32, "DecalMaterialGPU must be 32 bytes for std430");

// GPU-side water material — must match GLSL std430 layout exactly.
// Indexed per-draw via gl_BaseInstance into binding 2 (forward pass).
struct WaterMaterialGPU {
    uint64_t diffuseHandle;        //  0
    uint64_t bumpHandle;           //  8
    uint64_t emissiveHandle;       // 16
    uint64_t cubeHandle;           // 24
    float    intensity0;           // 32
    float    emissiveIntensity;    // 36
    float    specularIntensity;    // 40
    float    bumpScale;            // 44
    float    uvScale;              // 48
    float    velocityX;            // 52
    float    velocityY;            // 56
    uint32_t flags;                // 60
};
static_assert(sizeof(WaterMaterialGPU) == 64, "WaterMaterialGPU must be 64 bytes for std430");

enum WaterMaterialFlags : uint32_t {
    WATER_FLAG_HAS_VELOCITY = 1u << 0,
};

// GPU-side refraction material — must match GLSL std430 layout exactly.
// Indexed per-draw via gl_BaseInstance into binding 2 (forward pass).
struct RefractionMaterialGPU {
    uint64_t distortHandle;        //  0
    float    velocityX;            //  8
    float    velocityY;            // 12
    float    distortionScale;      // 16
    float    pad0;                 // 20
    float    pad1;                 // 24
    float    pad2;                 // 28
};
static_assert(sizeof(RefractionMaterialGPU) == 32, "RefractionMaterialGPU must be 32 bytes for std430");

// GPU-side environment alpha material — must match GLSL std430 layout exactly.
// Indexed per-draw via gl_BaseInstance into binding 2 (forward pass).
struct EnvAlphaMaterialGPU {
    uint64_t diffuseHandle;        //  0
    uint64_t specHandle;           //  8
    uint64_t bumpHandle;           // 16
    uint64_t emsvHandle;           // 24
    uint64_t envCubeHandle;        // 32
    float    reflectivity;         // 40
    float    specularIntensity;    // 44
    float    alphaBlendFactor;     // 48
    uint32_t flags;                // 52
    float    pad0;                 // 56
    float    pad1;                 // 60
};
static_assert(sizeof(EnvAlphaMaterialGPU) == 64, "EnvAlphaMaterialGPU must be 64 bytes for std430");

enum EnvAlphaFlags : uint32_t {
    ENVALPHA_FLAG_TWO_SIDED   = 1u << 0,
    ENVALPHA_FLAG_FLAT_NORMAL = 1u << 1,
};

#endif
