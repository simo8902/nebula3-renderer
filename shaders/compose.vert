#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vTexCoord1;

uniform vec4 focalLength;
uniform vec4 halfPixelSize;

void main()
{
    vTexCoord = inTexCoord;
    vTexCoord1 = vec3(focalLength.xy * inPosition.xy, -1.0);
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
