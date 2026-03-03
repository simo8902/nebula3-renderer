#version 460 core
layout(location = 0) in vec3 position;
uniform mat4 lightSpaceMatrix;
layout(std430, binding = 1) readonly buffer ModelMatrices {
    mat4 modelMatrices[];
};
void main() {
    mat4 model = modelMatrices[gl_BaseInstance + gl_InstanceID];
    gl_Position = lightSpaceMatrix * model * vec4(position, 1.0);
}
