#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_ARB_shader_storage_buffer_object : require

#ifndef ALPHA_CLIP
#define ALPHA_CLIP 0
#endif

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vTangentVS;
layout(location = 2) in vec3 vNormalVS;
layout(location = 3) in vec3 vBinormalVS;
layout(location = 4) in vec3 vViewPos;
layout(location = 5) flat in uint vMaterialIndex;

layout(location = 0) out vec4 outPackedGBuffer;
layout(location = 1) out vec4 outAlbedo;

// MaterialGPU struct matching C++ std430 layout
struct MaterialGPU {
    uint64_t diffuseHandle;     // 0
    uint64_t specularHandle;    // 8
    uint64_t normalHandle;      // 16
    uint64_t emissiveHandle;    // 24
    float emissiveIntensity;    // 32
    float specularIntensity;    // 36
    float specularPower;        // 40
    float alphaCutoff;          // 44
    uint flags;                 // 48
    float bumpScale;            // 52
    float intensity0;           // 56
    float alphaBlendFactor;     // 60
    uint64_t diffMap1Handle;    // 64
    uint64_t specMap1Handle;    // 72
    uint64_t bumpMap1Handle;    // 80
    uint64_t maskMapHandle;     // 88
    uint64_t alphaMapHandle;    // 96
    uint64_t cubeMapHandle;     // 104
    float velocityX;            // 112
    float velocityY;            // 116
    float scale;                // 120
    float pad0;                 // 124
};

layout(std140, binding = 1) uniform PSRegisters {
    vec4 pc[4];
};

// SSBO for bindless materials
layout(std430, binding = 2) readonly buffer Materials {
    MaterialGPU materials[];
};

sampler2D bindlessSampler2D(uint64_t handle)
{
    return sampler2D(unpackUint2x32(handle));
}

void main()
{
    MaterialGPU mat = materials[vMaterialIndex];

    vec4 diffuse = texture(bindlessSampler2D(mat.diffuseHandle), vTexCoord);

#if ALPHA_CLIP
    float alphaRef = pc[1].x * (1.0 / 256.0);
    if (diffuse.w - alphaRef < 0.0) {
        discard;
    }
#endif

    // DXT5nm: normal.xy stored in .wy channels
    vec4 bump = texture(bindlessSampler2D(mat.normalHandle), vTexCoord);
    vec2 nxy = bump.wy * 2.0 - 1.0;

    // Perturb normal using view-space TBN
    vec3 N = nxy.x * vTangentVS + nxy.y * vBinormalVS;
    float nz = sqrt(clamp(1.0 - dot(nxy, nxy), 0.0, 1.0));
    N += nz * vNormalVS;

    // Dual-paraboloid normal encoding
    vec2 encoded = N.xy / (N.z + 1.0) * 0.281262308 + 0.5;

    // 16-bit linear depth (split into high/low bytes)
    float dist = length(vViewPos);
    float depthVal = dist * pc[0].x * (1.0 / 256.0);
    float depthHigh = floor(depthVal) * (1.0 / 256.0);
    float depthLow = fract(depthVal);

    outPackedGBuffer = vec4(encoded, depthHigh, depthLow);
    outAlbedo = diffuse;
}
