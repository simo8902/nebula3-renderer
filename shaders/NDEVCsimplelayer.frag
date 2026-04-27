#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_ARB_shader_storage_buffer_object : require

#ifndef PASS
#define PASS 2
#endif

#ifndef ALPHA_CLIP
#define ALPHA_CLIP 0
#endif

// Match NDEVC std430 material block mapped across the engine
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
    uint64_t diffMap1Handle;    // 64  (layer2)
    uint64_t specMap1Handle;    // 72
    uint64_t bumpMap1Handle;    // 80
    uint64_t maskMapHandle;     // 88  (mask)
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

layout(std430, binding = 2) readonly buffer Materials {
    MaterialGPU materials[];
};

sampler2D bindlessSampler2D(uint64_t handle)
{
    return sampler2D(unpackUint2x32(handle));
}

#if PASS == 0
uniform vec4 highlightColor;

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) flat in uint vMaterialIndex;

layout(location = 0) out vec4 outColor;

void main()
{
    MaterialGPU mat = materials[vMaterialIndex];
    vec4 diffuse = texture(bindlessSampler2D(mat.diffuseHandle), vTexCoord);
    outColor = vec4(highlightColor.rgb, diffuse.a);
}

#elif PASS >= 1 && PASS <= 3

uniform vec4 fogDistances;
uniform vec4 fogColor;
uniform vec4 heightFogColor;
uniform vec4 customColor2;

#if PASS == 1
uniform float mayaAnimableAlpha;
#elif PASS >= 2
uniform float mayaAnimableAlpha;
uniform float encodefactor;
uniform vec4 pixelSize;
uniform uint64_t lightSampler;
#endif

layout(location = 0) in vec4 vViewPos;
layout(location = 1) in vec3 vUvAndHF;
layout(location = 2) in vec4 vClipPosCopy;
layout(location = 3) in vec4 vLayerUV;
layout(location = 4) flat in uint vMaterialIndex;

layout(location = 0) out vec4 outColor;

void main()
{
    MaterialGPU mat = materials[vMaterialIndex];

    float invFogRange = 1.0 / (fogDistances.y - fogDistances.x);
    float distFog = (fogDistances.y - vClipPosCopy.z) * invFogRange;
    distFog = clamp(distFog, fogColor.w, 1.0);

    vec4 diffuse = texture(bindlessSampler2D(mat.diffuseHandle), vUvAndHF.xy);
    vec4 layer2  = texture(bindlessSampler2D(mat.diffMap1Handle), vLayerUV.zw);
    vec4 mask    = texture(bindlessSampler2D(mat.maskMapHandle), vLayerUV.xy);

    vec4 blended = mix(diffuse, layer2, mask.x);
    vec3 tinted = mix(blended.rgb, customColor2.rgb, customColor2.w);

#if PASS == 1
    float alpha = blended.a * mat.alphaBlendFactor * mayaAnimableAlpha;
    vec3 foggedColor = mix(fogColor.rgb, tinted, distFog);
    vec3 finalColor  = mix(heightFogColor.rgb, foggedColor, vUvAndHF.z);
    outColor = vec4(finalColor * 0.5, alpha);

#else
    vec2 screenUV = (gl_FragCoord.xy + 0.5) * pixelSize.xy;
    vec4 lightColor = texture(bindlessSampler2D(lightSampler), screenUV);

    vec4 diffuseWithAlpha = vec4(blended.rgb, 1.0) * vec4(1.0, 1.0, 1.0, 0.0) + vec4(0.0, 0.0, 0.0, 1.0);
    vec4 lit = lightColor * diffuseWithAlpha;
    vec3 color = lit.rgb * 2.0;

    float specPower = pow(abs(lit.w), mat.specularPower);
    float encodedSpec = specPower * encodefactor;

    vec4 emissive = texture(bindlessSampler2D(mat.emissiveHandle), vUvAndHF.xy);
    color += emissive.rgb * mat.emissiveIntensity;

    vec4 spec0 = texture(bindlessSampler2D(mat.specularHandle), vUvAndHF.xy);
    vec4 spec1 = texture(bindlessSampler2D(mat.specMap1Handle), vLayerUV.zw);
    vec3 specBlended = mix(spec0.rgb, spec1.rgb, mask.x);
    color += specBlended * mat.specularIntensity * encodedSpec;

    vec3 tintedColor = mix(color, customColor2.rgb, customColor2.w);

    vec3 foggedColor = mix(fogColor.rgb, tintedColor, distFog);
    vec3 finalColor  = mix(heightFogColor.rgb, foggedColor, vUvAndHF.z);

#if PASS == 2
    float alpha = blended.a * mat.alphaBlendFactor * mayaAnimableAlpha;
#else
    float alpha = 1.0;
#endif

    outColor = vec4(finalColor * 0.5, alpha);
#endif
}

#elif PASS == 4 || PASS == 5

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vTangentVS;
layout(location = 2) in vec3 vNormalVS;
layout(location = 3) in vec3 vBinormalVS;
layout(location = 4) in vec4 vViewPos;
layout(location = 5) in vec4 vLayerUV;
layout(location = 6) flat in uint vMaterialIndex;

layout(location = 0) out vec4 outPackedGBuffer;
layout(location = 1) out vec4 outAlbedo;

void main()
{
    MaterialGPU mat = materials[vMaterialIndex];

    vec4 diffuse = texture(bindlessSampler2D(mat.diffuseHandle), vTexCoord);

#if PASS == 4
    float clipThreshold = pc[1].x * (1.0 / 256.0);
    if (diffuse.w - clipThreshold < 0.0) {
        discard;
    }
#endif

    vec4 bump0 = texture(bindlessSampler2D(mat.normalHandle), vTexCoord);
    vec2 n0xy = bump0.wy * 2.0 - 1.0;
    float n0z = sqrt(clamp(1.0 - dot(n0xy, n0xy), 0.0, 1.0));
    vec3 normal0 = vec3(n0xy, n0z);

    vec4 bump1 = texture(bindlessSampler2D(mat.bumpMap1Handle), vLayerUV.zw);
    vec2 n1xy = bump1.wy * 2.0 - 1.0;
    float n1z = sqrt(clamp(1.0 - dot(n1xy, n1xy), 0.0, 1.0));
    vec3 normal1 = vec3(n1xy, n1z);

    vec4 maskVal = texture(bindlessSampler2D(mat.maskMapHandle), vLayerUV.xy);
    vec3 blendedNormal = mix(normal0, normal1, maskVal.x);

    vec3 viewNormal = blendedNormal.x * vTangentVS +
                      blendedNormal.y * vBinormalVS +
                      blendedNormal.z * vNormalVS;

    vec2 encoded = viewNormal.xy / (viewNormal.z + 1.0);
    vec2 packedNormal = encoded * 0.281262308 + 0.5;

    float dist = length(vViewPos.xyz);
    float depthVal = dist * pc[0].x * (1.0 / 256.0);
    float depthHigh = floor(depthVal) * (1.0 / 256.0);
    float depthLow = fract(depthVal);

    vec4 layer2  = texture(bindlessSampler2D(mat.diffMap1Handle), vLayerUV.zw);
    vec4 blendedDiffuse = mix(diffuse, layer2, maskVal.x);

    outPackedGBuffer = vec4(packedNormal, depthHigh, depthLow);
    outAlbedo = blendedDiffuse;
}

#elif PASS == 6

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vClipPosCopy;
layout(location = 2) flat in uint vMaterialIndex;

layout(location = 0) out float outDepth;

void main()
{
    MaterialGPU mat = materials[vMaterialIndex];
    vec4 diffuse = texture(bindlessSampler2D(mat.diffuseHandle), vTexCoord);
    if (diffuse.a - 0.01 < 0.0) {
        discard;
    }

    outDepth = vClipPosCopy.z / vClipPosCopy.w;
}

#elif PASS == 7

layout(location = 0) in vec4 vClipPosCopy;
layout(location = 0) out float outDepth;

void main()
{
    outDepth = vClipPosCopy.z / vClipPosCopy.w;
}

#endif