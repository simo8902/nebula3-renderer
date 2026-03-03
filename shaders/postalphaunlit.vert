#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord0;
layout(location = 3) in vec4 aJointWeights;
layout(location = 4) in ivec4 aJointIndices;
layout(location = 5) in vec2 texcoord1;
layout(location = 6) in vec3 tangent;
layout(location = 7) in vec3 binormal;
layout(location = 8) in vec4 color;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 JointMatrices[128];
uniform int UseSkinning;
uniform int UseInstancing;
uniform mat4 textureTransform0;

layout(binding = 1, std430) readonly buffer ModelMatrixBuffer {
    mat4 models[];
};

out vec2 sUV;
out vec4 sColor;

void main() {
    vec4 lp = vec4(position, 1.0);

    if (UseSkinning > 0) {
        vec4 w = aJointWeights;
        float s = w.x + w.y + w.z + w.w;
        if (s > 0.0) w /= s;
        mat4 m = w.x * JointMatrices[aJointIndices.x]
               + w.y * JointMatrices[aJointIndices.y]
               + w.z * JointMatrices[aJointIndices.z]
               + w.w * JointMatrices[aJointIndices.w];
        lp = m * lp;
    }

    mat4 modelMat = (UseInstancing > 0) ? models[gl_BaseInstance + gl_InstanceID] : model;
    vec4 wpos = modelMat * lp;
    sUV = (textureTransform0 * vec4(texcoord0, 0.0, 1.0)).xy;
    sColor = color;
    gl_Position = projection * view * wpos;
}
