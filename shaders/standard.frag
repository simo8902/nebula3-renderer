#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;

uniform sampler2D diffMapSampler;
uniform sampler2D specMapSampler;
uniform sampler2D bumpMapSampler;
uniform sampler2D emsvSampler;

uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform float alphaBlendFactor;
uniform float mayaAnimableAlpha;
uniform float encodefactor;
uniform float AlphaClipRef;

uniform vec4 fogColor;
uniform vec4 fogDistances;
uniform vec4 heightFogColor;
uniform vec4 customColor2;
uniform vec4 pixelSize;

uniform int ReceivesDecals;
uniform int alphaTest;
uniform float alphaCutoff;
uniform int twoSided;
uniform int isFlatNormal;

in vec3 sWorldPos;
in vec3 sViewPos;
in vec2 sUV;
in vec2 sUV1;
in vec3 sTangent;
in vec3 sNormal;
in vec3 sBinormal;

layout(location = 0) out vec4 gPositionVS;
layout(location = 1) out vec4 gNormalDepthPacked;
layout(location = 2) out vec4 gAlbedoSpec;
layout(location = 3) out vec4 gPositionWS;
layout(location = 4) out vec4 gEmissive;
layout(location = 5) out vec4 gNormalDepthEncoded_out;

void main() {
    const float mipBias = -0.5;

    vec4 diffColor = texture(DiffMap0, sUV, mipBias);
    vec4 specColor = texture(SpecMap0, sUV, mipBias);
    vec4 bumpColor = texture(BumpMap0, sUV, mipBias);
    vec3 emsvColor = texture(EmsvMap0, sUV, mipBias).rgb * MatEmissiveIntensity;

    float finalAlpha = diffColor.a * alphaBlendFactor * mayaAnimableAlpha;
    float clipThreshold = clamp(max(alphaCutoff, AlphaClipRef * (1.0 / 256.0)), 0.0, 1.0);
    if (alphaTest > 0 && finalAlpha < clipThreshold) discard;

    vec3 T = normalize(sTangent);
    vec3 B = normalize(sBinormal);
    vec3 N = normalize(sNormal);

    if (twoSided > 0 && !gl_FrontFacing) {
        N = -N;
        T = -T;
        B = -B;
    }

    vec2 nxy = bumpColor.wy * 2.0 - 1.0;
    float nz = sqrt(max(1.0 - dot(nxy, nxy), 0.0));
    vec3 worldSpaceNormal = (isFlatNormal > 0) ? N : normalize(nxy.x * T + nxy.y * B + nz * N);

    float specIntensity = clamp(specColor.r * MatSpecularIntensity, 0.0, 1.0);
    float specPowerPacked = clamp(MatSpecularPower / 255.0, 0.0, 1.0);

    vec3 finalDiffuse = mix(diffColor.rgb, customColor2.rgb, clamp(customColor2.a, 0.0, 1.0));

    gPositionVS = vec4(sViewPos, 1.0);
    gNormalDepthPacked = vec4(worldSpaceNormal * 0.5 + 0.5, specPowerPacked);
    gAlbedoSpec = vec4(finalDiffuse, specIntensity);
    gPositionWS = vec4(sWorldPos, (ReceivesDecals > 0) ? 1.0 : 0.0);
    gEmissive = vec4(emsvColor, 0.0);
    gNormalDepthEncoded_out = vec4(0.0);
}
