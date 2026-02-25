#version 460 core

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texcoord0;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 textureTransform0;

out vec2 vUV;

void main() {
    vec4 wpos = model * vec4(position, 1.0);
    vec4 clip = projection * view * wpos;
    gl_Position = clip;
    vUV = (textureTransform0 * vec4(texcoord0, 0.0, 1.0)).xy;
    // Screen-space UV derived in fragment from gl_FragCoord for stability.
}
