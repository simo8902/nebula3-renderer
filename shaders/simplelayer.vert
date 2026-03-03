#version 460 core

#ifndef SKINNING_MODE
#define SKINNING_MODE 0
#endif

#ifndef PASS
#define PASS 0
#endif

const float UV_SCALE = 1.0;

layout(location = 0) in vec4 position;

#if PASS == 1 || PASS == 2
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;
layout(location = 5) in vec2 texcoord1;
#elif PASS == 0 || PASS == 3
layout(location = 2) in vec2 texcoord;
#endif

#if PASS == 2
layout(location = 6) in vec3 tangent;
layout(location = 7) in vec3 binormal;
#endif

#if SKINNING_MODE == 0
layout(location = 3) in vec4 blendWeights;
layout(location = 4) in vec4 blendIndices;
#elif SKINNING_MODE == 1
layout(location = 8) in vec4 instRow0;
layout(location = 9) in vec4 instRow1;
layout(location = 10) in vec4 instRow2;
#endif

#if SKINNING_MODE == 0
uniform mat4x3 JointPalette[72];
#endif

uniform mat4 mvp;
uniform mat4 projection;
uniform mat4 view;
uniform int UseInstancing;

layout(binding = 1, std430) readonly buffer ModelMatrixBuffer {
    mat4 models[];
};

#if PASS == 1
uniform mat4 modelView;
uniform mat4 model;
uniform vec4 heightFogColor;
uniform float layerTiling;
uniform vec4 fogDistances;
#elif PASS == 2
uniform mat4 modelView;
uniform mat4 imv;
uniform float layerTiling;
uniform mat4 invView;
layout(binding = 2, std430) readonly buffer SlGBufInvWorldBuffer { mat4 slInvWorlds[]; };
layout(binding = 3, std430) readonly buffer SlGBufTilingBuffer   { float slTilings[];  };
#elif PASS == 3
uniform mat4 lightSpaceMatrix;
#elif PASS == 4
uniform mat4 lightSpaceMatrix;
#endif

#if PASS == 0
layout(location = 0) out vec2 vTexcoord;
#elif PASS == 1
layout(location = 0) out vec4 vViewPos;
layout(location = 1) out vec3 vUvAndHF;
layout(location = 2) out vec4 vClipPosCopy;
layout(location = 3) out vec4 vLayerUV;
#elif PASS == 2
layout(location = 0) out vec2 vTexcoord;
layout(location = 1) out vec3 vTangentVS;
layout(location = 2) out vec3 vNormalVS;
layout(location = 3) out vec3 vBinormalVS;
layout(location = 4) out vec4 vViewPos;
layout(location = 5) out vec4 vLayerUV;
#elif PASS == 3
layout(location = 0) out vec2 vTexcoord;
layout(location = 1) out vec4 vClipPosCopy;
#elif PASS == 4
layout(location = 0) out vec4 vClipPosCopy;
#endif

#if SKINNING_MODE == 0
vec3 skinPosition(vec4 pos, vec4 weights, vec4 indices)
{
    float weightSum = dot(weights, vec4(1.0));
    vec4 w = weights / weightSum;
    ivec4 idx = ivec4(indices);

    vec3 result = vec3(0.0);
    result += w.x * (pos * JointPalette[idx.x]);
    result += w.y * (pos * JointPalette[idx.y]);
    result += w.z * (pos * JointPalette[idx.z]);
    result += w.w * (pos * JointPalette[idx.w]);
    return result;
}

vec3 skinVector(vec3 vec, vec4 rawWeights, ivec4 idx)
{
    vec3 result = vec3(0.0);
    result += rawWeights.x * (vec4(vec, 0.0) * JointPalette[idx.x]);
    result += rawWeights.y * (vec4(vec, 0.0) * JointPalette[idx.y]);
    result += rawWeights.z * (vec4(vec, 0.0) * JointPalette[idx.z]);
    result += rawWeights.w * (vec4(vec, 0.0) * JointPalette[idx.w]);
    return result;
}
#endif

void main()
{
#if SKINNING_MODE == 0
    vec3 skinnedPos = skinPosition(position, blendWeights, blendIndices);
    vec4 pos = vec4(skinnedPos, 1.0);
#elif SKINNING_MODE == 1
    vec3 instPos;
    instPos.x = dot(position, instRow0);
    instPos.y = dot(position, instRow1);
    instPos.z = dot(position, instRow2);
    vec4 pos = vec4(instPos, position.w);
#else
    vec4 pos = position;
#endif

#if PASS == 1
    if (UseInstancing > 0) {
        mat4 modelMat = models[gl_BaseInstance + gl_InstanceID];
        vec4 worldPos = modelMat * pos;
        vec4 viewPos = view * worldPos;
        vec4 clipPos = projection * viewPos;
        gl_Position = clipPos;

        float heightFogScale = 1.0 / (heightFogColor.w - fogDistances.z);
        vViewPos = viewPos;
        vUvAndHF.xy = texcoord * UV_SCALE;
        vUvAndHF.z = clamp((-worldPos.y + heightFogColor.w) * heightFogScale, 0.0, 1.0);
        vClipPosCopy = clipPos;

        vec2 layerUVScaled = texcoord1 * UV_SCALE;
        vLayerUV.xy = layerUVScaled;
        vLayerUV.zw = layerUVScaled * layerTiling;
        return;
    }
#endif

    vec4 clipPos = pos * mvp;
    gl_Position = clipPos;

#if PASS == 0
    vTexcoord = texcoord * UV_SCALE;

#elif PASS == 1
    float heightFogScale = 1.0 / (heightFogColor.w - fogDistances.z);

    vViewPos.x = dot(pos, modelView[0]);
    vViewPos.y = dot(pos, modelView[1]);
    vViewPos.z = dot(pos, modelView[2]);
    vViewPos.w = 1.0;

    vUvAndHF.xy = texcoord * UV_SCALE;

    float worldY = dot(pos, model[1]);
    vUvAndHF.z = clamp((-worldY + heightFogColor.w) * heightFogScale, 0.0, 1.0);

    vClipPosCopy = clipPos;

    vec2 layerUVScaled = texcoord1 * UV_SCALE;
    vLayerUV.xy = layerUVScaled;
    vLayerUV.zw = layerUVScaled * layerTiling;

#elif PASS == 2
    if (UseInstancing > 0) {
        int instIdx = gl_BaseInstance + gl_InstanceID;
        mat4 worldMat = models[instIdx];
        mat4 mv       = view * worldMat;
        vec4 instClip = projection * mv * pos;
        gl_Position   = instClip;

        vTexcoord = texcoord * UV_SCALE;
        vViewPos  = mv * pos;

        // imvMat = inv(M)*inv(V) = inv(V*M); normalTransform = mat3(transpose(imvMat))
        // T * mat3(transpose(imvMat)) ≡ T * mat3(glsl_imv) in the non-instanced path
        mat4 imvMat      = slInvWorlds[instIdx] * invView;
        mat3 normalTform = mat3(transpose(imvMat));
        vTangentVS  = tangent  * normalTform;
        vNormalVS   = normal   * normalTform;
        vBinormalVS = binormal * normalTform;

        float tiling    = slTilings[instIdx];
        vec2 layerUVS   = texcoord1 * UV_SCALE;
        vLayerUV.xy = layerUVS;
        vLayerUV.zw = layerUVS * tiling;
        return;
    }
    vTexcoord = texcoord * UV_SCALE;

    vViewPos.x = dot(pos, modelView[0]);
    vViewPos.y = dot(pos, modelView[1]);
    vViewPos.z = dot(pos, modelView[2]);
    vViewPos.w = 1.0;

    vec2 layerUVScaled = texcoord1 * UV_SCALE;
    vLayerUV.xy = layerUVScaled;
    vLayerUV.zw = layerUVScaled * layerTiling;

    vec3 T = tangent;
    vec3 N = normal;
    vec3 B = binormal;

#if SKINNING_MODE == 0
    vec4 weights = blendWeights / dot(blendWeights, vec4(1.0));
    ivec4 idx = ivec4(blendIndices);

    T = normalize(T);
    N = normalize(N);
    B = normalize(B);

    vec3 skinnedT = skinVector(T, weights, idx);
    vec3 skinnedN = skinVector(N, weights, idx);
    vec3 skinnedB = skinVector(B, weights, idx);

    vTangentVS = skinnedT * mat3(imv);
    vNormalVS = skinnedN * mat3(imv);
    vBinormalVS = skinnedB * mat3(imv);

#elif SKINNING_MODE == 1
    mat3 instMat = mat3(instRow0.xyz, instRow1.xyz, instRow2.xyz);

    vec3 instT = T * instMat;
    vec3 instN = N * instMat;
    vec3 instB = B * instMat;

    vTangentVS = instT * mat3(imv);
    vNormalVS = instN * mat3(imv);
    vBinormalVS = instB * mat3(imv);

#else
    vTangentVS = T * mat3(imv);
    vNormalVS = N * mat3(imv);
    vBinormalVS = B * mat3(imv);
#endif

#elif PASS == 3
    if (UseInstancing > 0) {
        mat4 worldMat = models[gl_BaseInstance + gl_InstanceID];
        vec4 instClip = lightSpaceMatrix * worldMat * pos;
        gl_Position   = instClip;
        vTexcoord     = texcoord * UV_SCALE;
        vClipPosCopy  = instClip;
        return;
    }
    vTexcoord    = texcoord * UV_SCALE;
    vClipPosCopy = clipPos;

#elif PASS == 4
    if (UseInstancing > 0) {
        mat4 worldMat = models[gl_BaseInstance + gl_InstanceID];
        vec4 instClip = lightSpaceMatrix * worldMat * pos;
        gl_Position   = instClip;
        vClipPosCopy  = instClip;
        return;
    }
    vClipPosCopy = clipPos;
#endif
}
