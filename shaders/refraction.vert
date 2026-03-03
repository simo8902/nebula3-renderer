#version 460 core

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texcoord0;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform int UseInstancing;

layout(binding = 1, std430) readonly buffer ModelMatrixBuffer {
    mat4 models[];
};

#ifdef BINDLESS
layout(std430, binding = 3) readonly buffer RefractionMaterialIndexBuffer {
    uint refrMatIndices[];
};

flat out uint vMaterialID;
#else
uniform mat4 textureTransform0;
#endif

out vec2 vUV;

void main() {
    mat4 modelMat = (UseInstancing > 0) ? models[gl_BaseInstance + gl_InstanceID] : model;
    vec4 wpos = modelMat * vec4(position, 1.0);
    vec4 clip = projection * view * wpos;
    gl_Position = clip;

#ifdef BINDLESS
    vMaterialID = refrMatIndices[gl_BaseInstance + gl_InstanceID];
    vUV = texcoord0;
#else
    vUV = (textureTransform0 * vec4(texcoord0, 0.0, 1.0)).xy;
#endif
}
