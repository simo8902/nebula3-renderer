#version 460 core

#ifdef BINDLESS
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

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
#else
uniform sampler2D DiffMap0;
uniform int alphaTest;
uniform float alphaCutoff;
uniform float alphaBlendFactor;
uniform float mayaAnimableAlpha;
uniform float AlphaClipRef;
#endif

in vec2 sUV;

void main() {
    const float mipBias = -0.5;
#ifdef BINDLESS
    MaterialGPU mat = materials[vMaterialID];
    vec4 diffColor = texture(sampler2D(mat.diffuseHandle), sUV, mipBias);
    const bool doAlphaTest = (mat.flags & 1u) != 0u;
    const float finalAlpha = diffColor.a * mat.alphaBlendFactor;
    const float cutoff = clamp(mat.alphaCutoff, 0.0, 1.0);
    if (doAlphaTest && finalAlpha < cutoff) discard;
#else
    vec4 diffColor = texture(DiffMap0, sUV, mipBias);
    const float finalAlpha = diffColor.a * alphaBlendFactor * mayaAnimableAlpha;
    const float clipThreshold = clamp(max(alphaCutoff, AlphaClipRef * (1.0 / 256.0)), 0.0, 1.0);
    if (alphaTest > 0 && finalAlpha < clipThreshold) discard;
#endif
}
