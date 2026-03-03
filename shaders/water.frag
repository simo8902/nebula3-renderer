#version 460 core

#ifdef BINDLESS
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

struct WaterMaterialGPU {
    uint64_t diffuseHandle;
    uint64_t bumpHandle;
    uint64_t emissiveHandle;
    uint64_t cubeHandle;
    float intensity0;
    float emissiveIntensity;
    float specularIntensity;
    float bumpScale;
    float uvScale;
    float velocityX;
    float velocityY;
    uint flags;
};

layout(std430, binding = 2) readonly buffer WaterMaterialBuffer {
    WaterMaterialGPU waterMaterials[];
};

flat in uint vMaterialID;
#else
uniform sampler2D DiffMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform samplerCube CubeMap0;
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform float Intensity0;
uniform float BumpScale;
#endif

uniform vec3 eyePos;
uniform int DisableViewDependentReflection;

in vec2 sUV;
in vec3 sWorldPos;
in vec3 sNormal;
in vec3 sTangent;
in vec3 sBinormal;

out vec4 FragColor;

const vec3 SEA_BLUE_SHALLOW = vec3(0.0, 0.5, 0.8);
const vec3 SEA_BLUE_DEEP = vec3(0.0, 0.2, 0.5);
const float WATER_CLARITY = 0.7;

void main() {
#ifdef BINDLESS
    WaterMaterialGPU wmat = waterMaterials[vMaterialID];
    float matBumpScale = wmat.bumpScale;
    float matEmissiveIntensity = wmat.emissiveIntensity;
    float matSpecularIntensity = wmat.specularIntensity;
    float matIntensity0 = wmat.intensity0;
    vec4 diff = texture(sampler2D(wmat.diffuseHandle), sUV);
    vec4 bumpSample = texture(sampler2D(wmat.bumpHandle), sUV);
    vec3 emsv = texture(sampler2D(wmat.emissiveHandle), sUV).rgb * matEmissiveIntensity;
#else
    float matBumpScale = BumpScale;
    float matEmissiveIntensity = MatEmissiveIntensity;
    float matSpecularIntensity = MatSpecularIntensity;
    float matIntensity0 = Intensity0;
    vec4 diff = texture(DiffMap0, sUV);
    vec4 bumpSample = texture(BumpMap0, sUV);
    vec3 emsv = texture(EmsvMap0, sUV).rgb * matEmissiveIntensity;
#endif

    // Tangent-space normal mapping
    vec3 T = normalize(sTangent);
    vec3 B = normalize(sBinormal);
    vec3 N = normalize(sNormal);
    vec3 tNormal;
    tNormal.xy = bumpSample.ag * 2.0 - 1.0;
    tNormal.xy *= matBumpScale;
    tNormal.z = sqrt(max(0.0, 1.0 - dot(tNormal.xy, tNormal.xy)));
    vec3 worldNormal = normalize(mat3(T, B, N) * tNormal);

    // View and reflection vectors
    vec3 reflectionEyePos = eyePos;
    reflectionEyePos.y = min(reflectionEyePos.y, 1.0);
    vec3 worldViewVec = normalize(sWorldPos - reflectionEyePos);
    vec3 envColor = vec3(0.0);
    if (DisableViewDependentReflection == 0) {
        vec3 reflectVec = reflect(worldViewVec, worldNormal);
#ifdef BINDLESS
        envColor = texture(samplerCube(wmat.cubeHandle), reflectVec).rgb;
#else
        envColor = texture(CubeMap0, reflectVec).rgb;
#endif
    }

    // Enhanced Fresnel effect for water
    float ndv = max(dot(-worldViewVec, worldNormal), 0.0);
    float fresnel = pow(1.0 - ndv, 3.0);

    // Calculate water depth factor (using view angle as approximation)
    float depthFactor = 1.0 - ndv;

    // Blend between shallow and deep sea blue based on depth
    vec3 seaBlue = mix(SEA_BLUE_SHALLOW, SEA_BLUE_DEEP, depthFactor);

    // Mix diffuse texture with sea blue color
    vec3 waterBase = mix(seaBlue, diff.rgb, WATER_CLARITY * diff.a);

    // Add emissive component
    vec3 baseColor = waterBase + emsv;

    // Apply reflection with fresnel
    float reflStrength = (DisableViewDependentReflection == 0)
        ? clamp(matSpecularIntensity * fresnel, 0.0, 1.0)
        : 0.0;
    vec3 color = mix(baseColor, envColor, reflStrength);

    // Enhanced alpha for better transparency
    float alpha = clamp(diff.a * matIntensity0 * (0.6 + depthFactor * 0.4), 0.4, 0.85);

    FragColor = vec4(color, alpha);
}
