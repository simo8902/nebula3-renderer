#version 460 core

#extension GL_ARB_shader_storage_buffer_object : require

#if !defined(STANDARD_GEOMETRY_SKINNED) && !defined(STANDARD_GEOMETRY_INSTANCED) && !defined(STANDARD_GEOMETRY_STATIC)
#define STANDARD_GEOMETRY_STATIC 1
#endif

#if !defined(STANDARD_PASS_HIGHLIGHT) && !defined(STANDARD_PASS_DIFFUSE_FOG_ALPHA) && !defined(STANDARD_PASS_LIT_COMPOSITE) && !defined(STANDARD_PASS_EMISSIVE_TINT_FOG) && !defined(STANDARD_PASS_BUMP_GBUFFER) && !defined(STANDARD_PASS_PROJECTIVE_DEPTH)
#define STANDARD_PASS_BUMP_GBUFFER 1
#endif

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord0;
layout(location = 3) in vec3 a_tangent;
layout(location = 4) in vec3 a_binormal;
#if defined(STANDARD_GEOMETRY_SKINNED)
layout(location = 5) in vec4 a_blendweight;
layout(location = 6) in uvec4 a_blendindices;
#endif

layout(std140, binding = 0) uniform VSRegisters {
    vec4 vc[228];
};

#if defined(STANDARD_GEOMETRY_SKINNED)
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

#if defined(STANDARD_GEOMETRY_INSTANCED) || defined(STANDARD_GEOMETRY_STATIC)
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

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vTangentVS;
layout(location = 2) out vec3 vNormalVS;
layout(location = 3) out vec3 vBinormalVS;
layout(location = 4) out vec3 vViewPos;
layout(location = 5) flat out uint vMaterialIndex;
layout(location = 6) out vec4 vClipPos;

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

#if defined(STANDARD_GEOMETRY_SKINNED)
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
#if defined(STANDARD_GEOMETRY_SKINNED)
    vec3 skinnedPos = skinPosition(vec4(a_position, 1.0), a_blendweight, a_blendindices);
    vec4 pos = vec4(skinnedPos, 1.0);
    vMaterialIndex = materialIndices[gl_BaseInstance + gl_InstanceID];
#else
    uint instanceIdx = gl_BaseInstance + gl_InstanceID;
    mat4 instMat = modelMatrices[instanceIdx];
    vec4 pos = instMat * vec4(a_position, 1.0);
    vMaterialIndex = materialIndices[instanceIdx];
#endif

    vClipPos = transformRows4(pos, MVP_ROW0, MVP_ROW1, MVP_ROW2, MVP_ROW3);
    gl_Position = vClipPos;

    vTexCoord = a_texcoord0;
    vViewPos = transformRows4(pos, MODELVIEW_ROW0, MODELVIEW_ROW1, MODELVIEW_ROW2);

    vec3 T = a_tangent * 2.0 - 1.0;
    vec3 N = a_normal * 2.0 - 1.0;
    vec3 B = a_binormal * 2.0 - 1.0;

#if defined(STANDARD_GEOMETRY_SKINNED)
    vec4 weights = a_blendweight / dot(a_blendweight, vec4(1.0));
    uvec4 idx = a_blendindices;
    T = skinVector(T, weights, idx);
    N = skinVector(N, weights, idx);
    B = skinVector(B, weights, idx);
    vTangentVS = normalize(transformRows3(T, IMV_ROW0, IMV_ROW1, IMV_ROW2));
    vNormalVS = normalize(transformRows3(N, IMV_ROW0, IMV_ROW1, IMV_ROW2));
    vBinormalVS = normalize(transformRows3(B, IMV_ROW0, IMV_ROW1, IMV_ROW2));
#else
    mat3 instMat3 = mat3(instMat);
    T = instMat3 * T;
    N = instMat3 * N;
    B = instMat3 * B;
    vTangentVS = normalize(transformRows3(T, IMV_ROW0, IMV_ROW1, IMV_ROW2));
    vNormalVS = normalize(transformRows3(N, IMV_ROW0, IMV_ROW1, IMV_ROW2));
    vBinormalVS = normalize(transformRows3(B, IMV_ROW0, IMV_ROW1, IMV_ROW2));
#endif
}
