#version 430 core

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 particlePos;
layout(location = 2) in vec4 particleStretchPos;
layout(location = 3) in vec4 particleColor;
layout(location = 4) in vec4 uvMinMax;
layout(location = 5) in vec4 rotSizeId;

uniform mat4 viewProj;
uniform mat4 view;
uniform mat4 invView;
uniform vec3 eyePos;
uniform mat4 emitterTransform;
uniform int billboard;
uniform int numAnimPhases;
uniform float animFramesPerSecond;
uniform float time;

out vec4 vColor;
out vec2 vTexCoord;
out vec3 vViewSpacePos;
out vec3 vWorldEyeVec;
out float vProjDepth;

void main() {
    float rotation = rotSizeId.x;
    const float kParticleSizeScale = 1.0;
    float size = rotSizeId.y * kParticleSizeScale;

    float rotSin = sin(rotation);
    float rotCos = cos(rotation);

    vec2 extrude = ((corner * 2.0) - 1.0) * size;
    mat2 rotMat = mat2(rotCos, rotSin, -rotSin, rotCos);
    extrude = rotMat * extrude;

    mat4 transform = (billboard != 0) ? invView : emitterTransform;
    vec4 worldExtrude = transform * vec4(extrude, 0.0, 0.0);

    float nph = max(float(numAnimPhases), 1.0);
    float du = fract(floor(time * animFramesPerSecond) / nph);

    float u = (corner.x != 0.0) ? (uvMinMax.z / nph) + du : (uvMinMax.x / nph) + du;
    float v;
    vec4 worldPos;
    if (corner.y != 0.0) {
        worldPos = particlePos + worldExtrude;
        v = uvMinMax.w;
    } else {
        worldPos = particleStretchPos + worldExtrude;
        v = uvMinMax.y;
    }

    vTexCoord = vec2(u, v);
    vColor = particleColor;
    vViewSpacePos = (view * worldPos).xyz;
    vWorldEyeVec = normalize(eyePos - worldPos.xyz);
    gl_Position = viewProj * worldPos;
    vProjDepth = gl_Position.z;
}
