#version 460 core

#ifdef BINDLESS
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#endif

#ifndef BINDLESS
uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;

uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;

uniform int ReceivesDecals;
uniform int alphaTest;
uniform float alphaCutoff;
uniform int twoSided;
uniform int isFlatNormal;
#endif

#ifdef BINDLESS
struct MaterialGPU {
    uint64_t diffuseHandle;
    uint64_t specularHandle;
    uint64_t normalHandle;
    uint64_t emissiveHandle;
    float emissiveIntensity;
    float specularIntensity;
    float specularPower;
    float alphaCutoff;
    uint flags;
    float bumpScale;
    float intensity0;
    float alphaBlendFactor;
    uint64_t diffMap1Handle;
    uint64_t specMap1Handle;
    uint64_t bumpMap1Handle;
    uint64_t maskMapHandle;
    uint64_t alphaMapHandle;
    uint64_t cubeMapHandle;
    float velocityX;
    float velocityY;
    float scale;
    float pad0;
};

layout(std430, binding = 2) readonly buffer MaterialBuffer {
    MaterialGPU materials[];
};

flat in uint vMaterialID;
#endif

in vec3 sWorldPos;
in vec3 sViewPos;
in vec2 sUV;
in vec2 sUV1;
in vec3 sTangent;
in vec3 sNormal;
in vec3 sBinormal;

// 6-output standard G-buffer layout (matches environment.frag)
layout(location=0) out vec4 gPositionVS;
layout(location=1) out vec4 gNormalDepthPacked;
layout(location=2) out vec4 gAlbedoSpec;
layout(location=3) out vec4 gPositionWS;
layout(location=4) out vec4 gEmissive;
layout(location=5) out vec4 gNormalDepthEncoded_out;

void main() {
    const float MipBias = -0.5;

#ifdef BINDLESS
    MaterialGPU mat = materials[vMaterialID];
    vec4 diffColor = texture(sampler2D(mat.diffuseHandle), sUV, MipBias);
    bool doAlphaTest = (mat.flags & 1u) != 0u;
    float cutoff = mat.alphaCutoff;
#else
    vec4 diffColor = texture(DiffMap0, sUV, MipBias);
    bool doAlphaTest = alphaTest > 0;
    float cutoff = alphaCutoff;
#endif
    if (doAlphaTest && diffColor.a < cutoff) discard;

    vec3 T = normalize(sTangent);
    vec3 B = normalize(sBinormal);
    vec3 N = normalize(sNormal);

#ifdef BINDLESS
    if ((mat.flags & 2u) != 0u && !gl_FrontFacing) { N = -N; T = -T; B = -B; }
#else
    if (twoSided > 0 && !gl_FrontFacing) { N = -N; T = -T; B = -B; }
#endif

#ifdef BINDLESS
    vec4 bump = texture(sampler2D(mat.normalHandle), sUV, MipBias);
#else
    vec4 bump = texture(BumpMap0, sUV, MipBias);
#endif
    // DXT5nm unpack (.w and .y channels like original)
    vec2 n2   = bump.wy * 2.0 - 1.0;
    float nz  = sqrt(max(1.0 - dot(n2, n2), 0.0));

#ifdef BINDLESS
    bool flatN = (mat.flags & 4u) != 0u;
#else
    bool flatN = isFlatNormal > 0;
#endif
    // World-space normal via TBN (TBN is now world-space from vertex shader)
    vec3 worldSpaceNormal = flatN ? N : normalize(n2.x * T + n2.y * B + nz * N);

#ifdef BINDLESS
    vec4 specColor    = texture(sampler2D(mat.specularHandle), sUV, MipBias);
    float specIntensity  = clamp(specColor.r * mat.specularIntensity, 0.0, 1.0);
    float specPowerPacked = clamp(mat.specularPower / 255.0, 0.0, 1.0);
    vec3 emsvColor = texture(sampler2D(mat.emissiveHandle), sUV, MipBias).rgb * mat.emissiveIntensity;
    float decalFlag = ((mat.flags & 8u) != 0u) ? 1.0 : 0.0;
#else
    vec4 specColor    = texture(SpecMap0, sUV, MipBias);
    float specIntensity  = clamp(specColor.r * MatSpecularIntensity, 0.0, 1.0);
    float specPowerPacked = clamp(MatSpecularPower / 255.0, 0.0, 1.0);
    vec3 emsvColor = texture(EmsvMap0, sUV, MipBias).rgb * MatEmissiveIntensity;
    float decalFlag = (ReceivesDecals > 0) ? 1.0 : 0.0;
#endif

    // Standard G-buffer outputs (matches environment.frag)
    gPositionVS           = vec4(sViewPos, 1.0);
    gNormalDepthPacked    = vec4(worldSpaceNormal * 0.5 + 0.5, specPowerPacked);
    gAlbedoSpec           = vec4(diffColor.rgb, specIntensity);
    gPositionWS           = vec4(sWorldPos, decalFlag);
    gEmissive             = vec4(emsvColor, 0.0);
    gNormalDepthEncoded_out = vec4(0.0);
}
