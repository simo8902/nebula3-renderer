#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord0;
layout(location = 3) in vec4 aJointWeights;
layout(location = 4) in ivec4 aJointIndices;
layout(location = 5) in vec2 texcoord1;
layout(location = 6) in vec3 tangent;
layout(location = 7) in vec3 binormal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 JointMatrices[128];
uniform int UseSkinning;

out vec3 vWorldPos;
out vec3 vViewPos;
out vec2 vUV;
out vec2 vUV1;
out vec3 vTangent;
out vec3 vNormal;
out vec3 vBinormal;

void main() {
    vec4 lp = vec4(position, 1.0);
    vec3 ln = normal;
    vec3 lt = tangent;
    vec3 lb = binormal;

    if (UseSkinning > 0) {
        vec4 w = aJointWeights;
        float s = w.x + w.y + w.z + w.w;
        if (s > 0.0) w /= s;
        mat4 m = w.x * JointMatrices[aJointIndices.x]
               + w.y * JointMatrices[aJointIndices.y]
               + w.z * JointMatrices[aJointIndices.z]
               + w.w * JointMatrices[aJointIndices.w];
        lp = m * lp;
        ln = mat3(m) * ln;
        lt = mat3(m) * lt;
        lb = mat3(m) * lb;
    }

    vec4 wpos = model * lp;
    vWorldPos = wpos.xyz;
    vViewPos = (view * wpos).xyz;

    mat3 nM = transpose(inverse(mat3(model)));
    vec3 N = normalize(nM * ln);
    vec3 T = normalize(nM * lt);
    T = normalize(T - N * dot(T, N));
    vec3 Borig = normalize(nM * lb);
    float handed = sign(dot(cross(N, T), Borig));
    vec3 B = normalize(cross(N, T)) * handed;

    vTangent  = T;
    vNormal   = N;
    vBinormal = B;

    vUV  = texcoord0;
    vUV1 = texcoord1;

    gl_Position = projection * vec4(vViewPos, 1.0);
}
