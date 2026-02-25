#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord0;
layout(location = 3) in vec4 aJointWeights;
layout(location = 4) in ivec4 aJointIndices;
layout(location = 5) in vec2 texcoord1;
layout(location = 6) in vec3 tangent;
layout(location = 7) in vec3 binormal;

uniform mat4  projection;
uniform mat4  view;
uniform mat4  model;
uniform mat3  normalMatrix;       // CPU: transpose(inverse(mat3(view * model))) — replaces GPU inversion + gives view-space like original imv
uniform mat4x3 JointMatrices[72]; // mat4x3 matches original float4x3, no wasted w row
uniform int   UseSkinning;
uniform int   UseInstancing;
uniform mat4  textureTransform0;

layout(binding = 1, std430) readonly buffer ModelMatrixBuffer {
    mat4 models[];
};

out vec3 sWorldPos;
out vec3 sViewPos;
out vec2 sUV;
out vec2 sUV1;
out vec3 sTangent;
out vec3 sNormal;
out vec3 sBinormal;

void main() {
    mat4 modelMat = (UseInstancing > 0) ? models[gl_BaseInstance] : model;

    vec4 lp = vec4(position, 1.0);
    vec3 ln = normal;
    vec3 lt = tangent;
    vec3 lb = binormal;

    if (UseSkinning > 0) {
        vec4 w = aJointWeights;
        float s = w.x + w.y + w.z + w.w;
        if (s > 0.0) w /= s;

        // mat4x3 * vec4 = vec3, set w=1 after
        vec3 sp  = JointMatrices[aJointIndices.x] * lp * w.x;
        sp      += JointMatrices[aJointIndices.y] * lp * w.y;
        sp      += JointMatrices[aJointIndices.z] * lp * w.z;
        sp      += JointMatrices[aJointIndices.w] * lp * w.w;
        lp = vec4(sp, 1.0);

        // vectors: use upper-left mat3 of each joint
        mat3 m3 = mat3(JointMatrices[aJointIndices.x]) * w.x
        + mat3(JointMatrices[aJointIndices.y]) * w.y
        + mat3(JointMatrices[aJointIndices.z]) * w.z
        + mat3(JointMatrices[aJointIndices.w]) * w.w;
        ln = m3 * ln;
        lt = m3 * lt;
        lb = m3 * lb;
    }

    vec4 wpos = modelMat * lp;
    sWorldPos = wpos.xyz;
    sViewPos  = (view * wpos).xyz;

    // normalMatrix is view-space normal matrix (matches original imv)
    // no Gram-Schmidt — original doesn't do it, just transform + normalize
    sTangent  = normalize(normalMatrix * lt);
    sNormal   = normalize(normalMatrix * ln);
    sBinormal = normalize(normalMatrix * lb);

    sUV  = (textureTransform0 * vec4(texcoord0, 0.0, 1.0)).xy;
    sUV1 = (textureTransform0 * vec4(texcoord1, 0.0, 1.0)).xy;

    gl_Position = projection * view * wpos;
}