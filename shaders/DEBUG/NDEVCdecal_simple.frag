#version 460 core

// Simple test shader - just render decal color without projection
uniform sampler2D DiffMap0;
uniform sampler2D EmsvMap0;
uniform float Scale;

in vec2 sUV;
in vec3 sViewPos;

layout(location=0) out vec4 gAlbedoSpec;

void main() {
    // ULTRA SIMPLE: Just output solid red, no textures, no discarding
    gAlbedoSpec = vec4(1.0, 0.0, 0.0, 1.0);
}
