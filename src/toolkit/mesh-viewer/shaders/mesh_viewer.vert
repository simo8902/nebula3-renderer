#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord0;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = model * vec4(position, 1.0);
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(mat3(transpose(inverse(model))) * normal);
    vUV = texcoord0;
    gl_Position = projection * view * worldPos;
}
