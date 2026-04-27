#version 460 core

#extension GL_ARB_shader_storage_buffer_object : require

#ifndef SKINNING_MODE
#define SKINNING_MODE 2   // 0=skinned, 1=instanced, 2=static
#endif

#ifndef PASS
#define PASS 2
#endif

// 1:1 HLSL semantics -> GLSL layout locations
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBinormal;
#if SKINNING_MODE == 0
layout(location = 5) in vec4 inBlendWeights;
layout(location = 6) in uvec4 inBlendIndices;
#endif
layout(location = 7) in vec2 inTexCoord1;

layout(std140, binding = 0) uniform VSRegisters {
    vec4 vc[228];
};

#if SKINNING_MODE == 0
#define MVP_ROW0 vc[216]
#define MVP_ROW1 vc[217]
#define MVP_ROW2 vc[218]
#define MVP_ROW3 vc[219]
#define MODELVIEW_ROW0 vc[220]
#define MODELVIEW_ROW1 vc[221]
#define MODELVIEW_ROW2 vc[222]
#define IMV_ROW0 vc[223]
#define IMV_ROW1 vc[224]
#define IMV_ROW2 vc[225]
#elif SKINNING_MODE == 1
#define MODELVIEW_ROW0 vc[0]
#define MODELVIEW_ROW1 vc[1]
#define MODELVIEW_ROW2 vc[2]
#define MODELVIEW_ROW3 vc[3]
#define MVP_ROW0 vc[4]
#define MVP_ROW1 vc[5]
#define MVP_ROW2 vc[6]
#define MVP_ROW3 vc[7]
#define IMV_ROW0 vc[8]
#define IMV_ROW1 vc[9]
#define IMV_ROW2 vc[10]
#else
#define MVP_ROW0 vc[0]
#define MVP_ROW1 vc[1]
#define MVP_ROW2 vc[2]
#define MVP_ROW3 vc[3]
#define MODELVIEW_ROW0 vc[4]
#define MODELVIEW_ROW1 vc[5]
#define MODELVIEW_ROW2 vc[6]
#define IMV_ROW0 vc[7]
#define IMV_ROW1 vc[8]
#define IMV_ROW2 vc[9]
#endif

#if SKINNING_MODE == 1 || SKINNING_MODE == 2
layout(std430, binding = 1) readonly buffer ModelMatrices {
    mat4 modelMatrices[];
};
#endif

layout(std430, binding = 3) readonly buffer MaterialIndices {
    uint materialIndices[];
};

out gl_PerVertex {
    vec4 gl_Position;
};

// Pass-specific uniforms (outside VSRegisters core set)
#if PASS == 1
uniform mat4 model;
uniform vec4 heightFogColor;
uniform vec4 fogDistances;
uniform float layerTiling;
#elif PASS == 2 || PASS == 4 || PASS == 5
uniform float layerTiling;
#endif

#if PASS == 0
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) flat out uint vMaterialIndex;
#elif PASS == 1
layout(location = 0) out vec4 vViewPos;
layout(location = 1) out vec3 vUvAndHF;
layout(location = 2) out vec4 vClipPosCopy;
layout(location = 3) out vec4 vLayerUV;
layout(location = 4) flat out uint vMaterialIndex;
#elif PASS == 2 || PASS == 4 || PASS == 5
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vTangentVS;
layout(location = 2) out vec3 vNormalVS;
layout(location = 3) out vec3 vBinormalVS;
layout(location = 4) out vec4 vViewPos;
layout(location = 5) out vec4 vLayerUV;
layout(location = 6) flat out uint vMaterialIndex;
#elif PASS == 3 || PASS == 6
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vClipPosCopy;
layout(location = 2) flat out uint vMaterialIndex;
#elif PASS == 7
layout(location = 0) out vec4 vClipPosCopy;
#endif

const float UV_SCALE = 1.0 / 8192.0;

vec3 transformRows3(vec3 v, vec4 row0, vec4 row1, vec4 row2)
{
    return vec3(dot(v, row0.xyz), dot(v, row1.xyz), dot(v, row2.xyz));
}
vec3 transformRows4(vec4 v, vec4 row0, vec4 row1, vec4 row2)
{
    return vec3(dot(v, row0), dot(v, row1), dot(v, row2));
}
vec4 transformRows4(vec4 v, vec4 row0, vec4 row1, vec4 row2, vec4 row3)
{
    return vec4(dot(v, row0), dot(v, row1), dot(v, row2), dot(v, row3));
}

#if SKINNING_MODE == 0
vec3 skinPosition(vec4 pos, vec4 weights, uvec4 idx)
{
    float weightSum = dot(weights, vec4(1.0));
    vec4 w = weights / weightSum;

    vec3 result = vec3(0.0);
    int base = int(idx.x) * 3;
    result += w.x * transformRows4(pos, vc[base], vc[base + 1], vc[base + 2]);
    base = int(idx.y) * 3;
    result += w.y * transformRows4(pos, vc[base], vc[base + 1], vc[base + 2]);
    base = int(idx.z) * 3;
    result += w.z * transformRows4(pos, vc[base], vc[base + 1], vc[base + 2]);
    base = int(idx.w) * 3;
    result += w.w * transformRows4(pos, vc[base], vc[base + 1], vc[base + 2]);
    return result;
}

vec3 skinVector(vec3 vec, vec4 rawWeights, uvec4 idx)
{
    vec3 result = vec3(0.0);
    int base = int(idx.x) * 3;
    result += rawWeights.x * transformRows3(vec, vc[base], vc[base + 1], vc[base + 2]);
    base = int(idx.y) * 3;
    result += rawWeights.y * transformRows3(vec, vc[base], vc[base + 1], vc[base + 2]);
    base = int(idx.z) * 3;
    result += rawWeights.z * transformRows3(vec, vc[base], vc[base + 1], vc[base + 2]);
    base = int(idx.w) * 3;
    result += rawWeights.w * transformRows3(vec, vc[base], vc[base + 1], vc[base + 2]);
    return result;
}
#endif

void main()
{
#if SKINNING_MODE == 0
    vec3 skinnedPos = skinPosition(vec4(inPosition, 1.0), inBlendWeights, inBlendIndices);
    vec4 pos = vec4(skinnedPos, 1.0);
    uint matIdx = materialIndices[gl_BaseInstance + gl_InstanceID];
#elif SKINNING_MODE == 1
    uint instanceIdx = gl_BaseInstance + gl_InstanceID;
    mat4 instMat = modelMatrices[instanceIdx];
    vec4 pos = instMat * vec4(inPosition, 1.0);
    uint matIdx = materialIndices[instanceIdx];
#else
    uint instanceIdx = gl_BaseInstance + gl_InstanceID;
    mat4 instMat = modelMatrices[instanceIdx];
    vec4 pos = instMat * vec4(inPosition, 1.0);
    uint matIdx = materialIndices[instanceIdx];
#endif

    vec4 clipPos = transformRows4(pos, MVP_ROW0, MVP_ROW1, MVP_ROW2, MVP_ROW3);
    gl_Position = clipPos;

#if PASS != 7
    vMaterialIndex = matIdx;
#endif

#if PASS == 0
    vTexCoord = inTexCoord * UV_SCALE;
#elif PASS == 1
    float heightFogScale = 1.0 / (heightFogColor.w - fogDistances.z);

    vec3 vp = transformRows4(pos, MODELVIEW_ROW0, MODELVIEW_ROW1, MODELVIEW_ROW2);
    vViewPos = vec4(vp, inNormal.w);

    vUvAndHF.xy = inTexCoord * UV_SCALE;

    vec4 modelRow1 = vec4(model[0][1], model[1][1], model[2][1], model[3][1]);
    float worldY = dot(pos, modelRow1);
    vUvAndHF.z = clamp((-worldY + heightFogColor.w) * heightFogScale, 0.0, 1.0);

    vClipPosCopy = clipPos;

    vec2 layerUVScaled = inTexCoord1 * UV_SCALE;
    vLayerUV.xy = layerUVScaled;
    vLayerUV.zw = layerUVScaled * layerTiling;

#elif PASS == 2 || PASS == 4 || PASS == 5
    vTexCoord = inTexCoord * UV_SCALE;
    
    vec3 vp = transformRows4(pos, MODELVIEW_ROW0, MODELVIEW_ROW1, MODELVIEW_ROW2);
    vViewPos = vec4(vp, inNormal.w);

    vec2 layerUVScaled = inTexCoord1 * UV_SCALE;
    vLayerUV.xy = layerUVScaled;
    vLayerUV.zw = layerUVScaled * layerTiling;

    vec3 T = inTangent * 2.0 - 1.0;
    vec3 N = inNormal.xyz * 2.0 - 1.0;
    vec3 B = inBinormal * 2.0 - 1.0;

#if SKINNING_MODE == 0
    vec4 weights = inBlendWeights / dot(inBlendWeights, vec4(1.0));
    uvec4 idx = inBlendIndices;

    T = skinVector(normalize(T), weights, idx);
    N = skinVector(normalize(N), weights, idx);
    B = skinVector(normalize(B), weights, idx);

    vTangentVS  = transformRows3(T, IMV_ROW0, IMV_ROW1, IMV_ROW2);
    vNormalVS   = transformRows3(N, IMV_ROW0, IMV_ROW1, IMV_ROW2);
    vBinormalVS = transformRows3(B, IMV_ROW0, IMV_ROW1, IMV_ROW2);

#elif SKINNING_MODE == 1
    mat3 instMat3 = mat3(instMat);
    T = instMat3 * T;
    N = instMat3 * N;
    B = instMat3 * B;

    vTangentVS  = transformRows3(T, IMV_ROW0, IMV_ROW1, IMV_ROW2);
    vNormalVS   = transformRows3(N, IMV_ROW0, IMV_ROW1, IMV_ROW2);
    vBinormalVS = transformRows3(B, IMV_ROW0, IMV_ROW1, IMV_ROW2);

#else
    vTangentVS  = transformRows3(T, IMV_ROW0, IMV_ROW1, IMV_ROW2);
    vNormalVS   = transformRows3(N, IMV_ROW0, IMV_ROW1, IMV_ROW2);
    vBinormalVS = transformRows3(B, IMV_ROW0, IMV_ROW1, IMV_ROW2);
#endif

#elif PASS == 3 || PASS == 6
    vTexCoord = inTexCoord * UV_SCALE;
    vClipPosCopy = clipPos;
#elif PASS == 7
    vClipPosCopy = clipPos;
#endif
}
