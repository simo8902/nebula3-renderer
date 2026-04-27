#version 460 core

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;

layout(location = 0) uniform sampler2D colorBuffer;

void main()
{
    outColor = texture(colorBuffer, vTexCoord);
}
