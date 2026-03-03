#version 460 core

#ifdef BINDLESS
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_ARB_bindless_texture : require

struct EnvAlphaMaterialGPU {
    uint64_t diffuseHandle;
    uint64_t specHandle;
    uint64_t bumpHandle;
    uint64_t emsvHandle;
    uint64_t envCubeHandle;
    float    reflectivity;
    float    specularIntensity;
    float    alphaBlendFactor;
    uint     flags;
    float    pad0;
    float    pad1;
};

layout(binding = 2, std430) readonly buffer EnvAlphaMaterialBuffer {
    EnvAlphaMaterialGPU envAlphaMaterials[];
};

flat in uint vMaterialID;
#endif

#ifndef BINDLESS
uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform samplerCube EnvironmentMap;

uniform float Reflectivity;
uniform float MatSpecularIntensity;
uniform int twoSided;
uniform int isFlatNormal;
uniform float alphaBlendFactor;
#endif

uniform vec3 eyePos;
uniform int DisableViewDependentReflection;

in vec3 vWorldPos;
in vec2 vUV;
in vec2 vUV1;
in vec3 vTangent;
in vec3 vNormal;
in vec3 vBinormal;

out vec4 FragColor;

void main() {
    const float MipBias = -0.5;

#ifdef BINDLESS
    EnvAlphaMaterialGPU mat = envAlphaMaterials[vMaterialID];
    vec4 diffColor  = texture(sampler2D(mat.diffuseHandle), vUV, MipBias);
    vec4 specColor  = texture(sampler2D(mat.specHandle),    vUV, MipBias);
    vec4 emsvSample = texture(sampler2D(mat.emsvHandle),    vUV, MipBias);
    float mat_reflectivity   = mat.reflectivity;
    float mat_specIntensity  = mat.specularIntensity;
    float mat_alphaBlend     = mat.alphaBlendFactor;
    int   mat_twoSided       = int(mat.flags & 1u);
    int   mat_isFlatNormal   = int((mat.flags >> 1u) & 1u);
#else
    vec4 diffColor  = texture(DiffMap0, vUV, MipBias);
    vec4 specColor  = texture(SpecMap0, vUV, MipBias);
    vec4 emsvSample = texture(EmsvMap0, vUV, MipBias);
    float mat_reflectivity   = Reflectivity;
    float mat_specIntensity  = MatSpecularIntensity;
    float mat_alphaBlend     = alphaBlendFactor;
    int   mat_twoSided       = twoSided;
    int   mat_isFlatNormal   = isFlatNormal;
#endif

    float emsvLuma = max(emsvSample.r, max(emsvSample.g, emsvSample.b));
    float alphaMask = max(diffColor.a, max(specColor.a, max(emsvSample.a, emsvLuma)));
    float alpha = clamp(alphaMask * mat_alphaBlend, 0.0, 1.0);
    if (alpha <= 0.001) {
        discard;
    }

    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBinormal);
    vec3 N = normalize(vNormal);

    if (mat_twoSided > 0 && !gl_FrontFacing) {
        N = -N;
        T = -T;
        B = -B;
    }

    vec3 tNormal;
#ifdef BINDLESS
    vec4 bumpSample = texture(sampler2D(mat.bumpHandle), vUV, MipBias);
#else
    vec4 bumpSample = texture(BumpMap0, vUV, MipBias);
#endif
    tNormal.xy = bumpSample.ag * 2.0 - 1.0;
    tNormal.z  = sqrt(max(0.0, 1.0 - dot(tNormal.xy, tNormal.xy)));

    vec3 worldSpaceNormal = (mat_isFlatNormal > 0) ? N : normalize(mat3(T, B, N) * tNormal);

    float specIntensity = specColor.r * mat_specIntensity;
    vec3 emsvColor = emsvSample.rgb;

    vec3 envColor = vec3(0.0);
    float refl = 0.0;
    if (DisableViewDependentReflection == 0) {
        vec3 viewVec = vWorldPos - eyePos;
        viewVec.y *= 0.25;
        vec3 worldViewVec = normalize(viewVec);
        vec3 envDir = reflect(worldViewVec, worldSpaceNormal);
        float gloss = clamp(specColor.r, 0.0, 1.0);
        float roughness = clamp(1.0 - gloss, 0.1, 1.0);
        float envMip = mix(0.25, 3.5, roughness * roughness);
#ifdef BINDLESS
        envColor = textureLod(samplerCube(mat.envCubeHandle), envDir, envMip).rgb;
#else
        envColor = textureLod(EnvironmentMap, envDir, envMip).rgb;
#endif
        refl = clamp(mat_reflectivity * specColor.r, 0.0, 1.0);
    }

    vec3 litColor = diffColor.rgb + emsvColor + (envColor * refl);
    FragColor = vec4(litColor, alpha);
}
