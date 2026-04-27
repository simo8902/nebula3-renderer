#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_ARB_shader_storage_buffer_object : require

#if !defined(STANDARD_PASS_HIGHLIGHT) && !defined(STANDARD_PASS_DIFFUSE_FOG_ALPHA) && !defined(STANDARD_PASS_LIT_COMPOSITE) && !defined(STANDARD_PASS_EMISSIVE_TINT_FOG) && !defined(STANDARD_PASS_BUMP_GBUFFER) && !defined(STANDARD_PASS_PROJECTIVE_DEPTH)
#define STANDARD_PASS_BUMP_GBUFFER 1
#endif

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vTangentVS;
layout(location = 2) in vec3 vNormalVS;
layout(location = 3) in vec3 vBinormalVS;
layout(location = 4) in vec3 vViewPos;
layout(location = 5) flat in uint vMaterialIndex;
layout(location = 6) in vec4 vClipPos;

layout(location = 0) out vec4 outColor0;
layout(location = 1) out vec4 outColor1;

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
    float mayaAnimableAlpha;
    float encodefactor;
    float pad1;
    float pad2;
    vec4 customColor2;
    vec4 tintingColour;
    vec4 highlightColor;
    vec4 luminance;
};

layout(std140, binding = 1) uniform PSRegisters {
    vec4 pc[4];
};

layout(std430, binding = 2) readonly buffer Materials {
    MaterialGPU materials[];
};

layout(binding = 4) uniform sampler2D lightSampler;

sampler2D bindlessSampler2D(uint64_t handle)
{
    return sampler2D(unpackUint2x32(handle));
}

void main()
{
    MaterialGPU mat = materials[vMaterialIndex];
    vec4 diffuse = texture(bindlessSampler2D(mat.diffuseHandle), vTexCoord);
    const vec4 fogDistances = vec4(0.0, 1000000.0, 0.0, 0.0);
    const vec4 fogColor = vec4(0.0);
    const vec4 heightFogColor = vec4(0.0, 0.0, 0.0, 1.0);

#if defined(STANDARD_PASS_BUMP_GBUFFER)
#if defined(STANDARD_ALPHA_TEST)
    float alphaRef = pc[1].x * (1.0 / 256.0);
    if (diffuse.w - alphaRef < 0.0) {
        discard;
    }
#endif
    vec4 bump = texture(bindlessSampler2D(mat.normalHandle), vTexCoord);
    vec2 nxy = bump.wy * 2.0 - 1.0;
    vec3 N = nxy.x * vTangentVS + nxy.y * vBinormalVS;
    float nz = sqrt(clamp(1.0 - dot(nxy, nxy), 0.0, 1.0));
    N += nz * vNormalVS;
    N = normalize(N);
    vec2 encoded = N.xy / (N.z + 1.0) * 0.281262308 + 0.5;
    float dist = length(vViewPos);
    float depthVal = dist * pc[0].x * (1.0 / 256.0);
    float depthHigh = floor(depthVal) * (1.0 / 256.0);
    float depthLow = fract(depthVal);
    outColor0 = vec4(encoded, depthHigh, depthLow);
    outColor1 = diffuse;
#elif defined(STANDARD_PASS_HIGHLIGHT)
    outColor0 = vec4(mat.highlightColor.rgb, diffuse.a);
    outColor1 = vec4(0.0);
#elif defined(STANDARD_PASS_PROJECTIVE_DEPTH)
#if defined(STANDARD_ALPHA_TEST)
    if (any(lessThan(diffuse.wwww + vec4(-0.00999999978), vec4(0.0)))) {
        discard;
    }
#endif
    float invW = 1.0 / vClipPos.w;
    outColor0 = vec4(invW * vClipPos.z);
    outColor1 = vec4(0.0);
#elif defined(STANDARD_PASS_DIFFUSE_FOG_ALPHA)
    float invFogRange = 1.0 / max(fogDistances.y - fogDistances.x, 1e-6);
    float f = (fogDistances.y - vClipPos.z) * invFogRange;
    f = min(max(f, fogColor.w), 1.0);
    vec3 mixedDiffuse = mix(diffuse.xyz, mat.customColor2.xyz, mat.customColor2.w);
    vec3 fogged = mix(fogColor.xyz, mixedDiffuse, f);
    outColor0 = vec4(fogged * 0.5, diffuse.w * mat.alphaBlendFactor * mat.mayaAnimableAlpha);
    outColor1 = vec4(0.0);
#elif defined(STANDARD_PASS_LIT_COMPOSITE)
#if defined(STANDARD_ALPHA_TEST)
    float alphaRef = pc[1].x * (1.0 / 256.0);
    if (diffuse.w - alphaRef < 0.0) {
        discard;
    }
#endif
    vec2 pixelSize = 1.0 / max(vec2(textureSize(lightSampler, 0)), vec2(1.0));
    vec2 screenUV = gl_FragCoord.xy * pixelSize;
    vec4 lightAccum = texture(lightSampler, screenUV);
    vec4 emiss = texture(bindlessSampler2D(mat.emissiveHandle), vTexCoord);
    vec4 specSamp = texture(bindlessSampler2D(mat.specularHandle), vTexCoord);
    lightAccum *= vec4(diffuse.xyz, 1.0);
    lightAccum.xyz += lightAccum.xyz;
    float specBase = pow(abs(lightAccum.w), mat.specularPower);
    lightAccum.w = specBase * mat.encodefactor;
    lightAccum.xyz = emiss.xyz * mat.emissiveIntensity + lightAccum.xyz;
    lightAccum.xyz = specSamp.xyz * mat.specularIntensity * lightAccum.w + lightAccum.xyz;
    outColor0 = vec4(mix(lightAccum.xyz, mat.customColor2.xyz, mat.customColor2.w) * 0.5, diffuse.w);
#if defined(STANDARD_ALPHA_MODULATE)
    outColor0.a *= mat.alphaBlendFactor * mat.mayaAnimableAlpha;
#endif
    outColor1 = vec4(0.0);
#elif defined(STANDARD_PASS_EMISSIVE_TINT_FOG)
    float invFogRange = 1.0 / max(fogDistances.y - fogDistances.x, 1e-6);
    float f = (fogDistances.y - vClipPos.z) * invFogRange;
    f = min(max(f, fogColor.w), 1.0);
    vec4 emiss = texture(bindlessSampler2D(mat.emissiveHandle), vTexCoord);
    vec3 emissive = emiss.xyz * mat.emissiveIntensity * f;
    vec3 tinted = emissive * mat.tintingColour.xyz;
    vec3 mixed = mix(tinted, mat.customColor2.xyz, mat.customColor2.w);
    outColor0 = vec4(mixed * mat.alphaBlendFactor * 0.5,
                     dot(mixed, mat.luminance.xyz) * mat.alphaBlendFactor);
    outColor1 = vec4(0.0);
#endif
}
