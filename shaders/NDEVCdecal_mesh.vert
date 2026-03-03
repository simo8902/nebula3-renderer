#version 460 core
layout(location = 0) in vec3 position;

layout(std430, binding = 1) readonly buffer ModelMatrices {
    mat4 modelMatrices[];
};

#ifdef BINDLESS
layout(std430, binding = 5) readonly buffer DecalMaterialIndexBuffer {
    uint decalMatIndices[];
};
flat out uint vMaterialID;
#endif

uniform mat4 view;
uniform mat4 projection;

out vec3 vWorldPos;
flat out uint vInstanceID;

void main() {
    vInstanceID = uint(gl_BaseInstance + gl_InstanceID);
    mat4 model = modelMatrices[vInstanceID];
    vec4 worldPos = model * vec4(position, 1.0);
    vWorldPos = worldPos.xyz;
    gl_Position = projection * view * worldPos;
#ifdef BINDLESS
    vMaterialID = decalMatIndices[vInstanceID];
#endif
}
