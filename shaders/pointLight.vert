#version 460 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 inLightPosRange; // xyz = pos, w = range
layout(location = 2) in vec4 inLightColor;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 mvp;
uniform int UseInstancedPointLights;

flat out vec4 vLightPosRange;
flat out vec4 vLightColor;

void main() {
    if (UseInstancedPointLights != 0) {
        vec3 worldPos = inLightPosRange.xyz + position * inLightPosRange.w;
        gl_Position = projection * view * vec4(worldPos, 1.0);
        vLightPosRange = inLightPosRange;
        vLightColor = inLightColor;
    } else {
        gl_Position = mvp * vec4(position, 1.0);
    }
}
