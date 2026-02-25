#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform sampler2D DiffMap2;
uniform sampler2D SpecMap1;
uniform sampler2D BumpMap1;
uniform sampler2D MaskMap;
uniform int ReceivesDecals;

uniform float Intensity1;
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;

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

layout(location=0) out vec4 gPositionVS;
layout(location=1) out vec4 gNormalDepthPacked;
layout(location=2) out vec4 gAlbedoSpec;
layout(location=3) out vec4 gPositionWS;
layout(location=4) out vec4 gEmissive;

vec3 sampleNormal(sampler2D map, vec2 uv, float mipBias) {
    vec4 bumpSample = texture(map, uv, mipBias);
    vec3 n;
    n.xy = bumpSample.ag * 2.0 - 1.0;
    n.z  = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));
    return n;
}

void main() {
    const float MipBias = -0.5;

    vec4 diff0 = texture(DiffMap0, sUV, MipBias);
    if (alphaTest > 0 && diff0.a < alphaCutoff) discard;

    vec4 diff1 = texture(DiffMap2, sUV, MipBias);
    float mask = texture(MaskMap, sUV, MipBias).r;
    float blend = clamp(mask * Intensity1, 0.0, 1.0);

    vec3 T = normalize(sTangent);
    vec3 B = normalize(sBinormal);
    vec3 N = normalize(sNormal);

    if (twoSided > 0 && !gl_FrontFacing) {
        N = -N; T = -T; B = -B;
    }

    vec3 n0 = sampleNormal(BumpMap0, sUV, MipBias);
    vec3 n1 = sampleNormal(BumpMap1, sUV, MipBias);
    vec3 tNormal = normalize(mix(n0, n1, blend));
    vec3 worldSpaceNormal = (isFlatNormal > 0) ? N : normalize(mat3(T, B, N) * tNormal);

    vec4 spec0 = texture(SpecMap0, sUV, MipBias);
    vec4 spec1 = texture(SpecMap1, sUV, MipBias);
    vec4 specColor = mix(spec0, spec1, blend);
    float specIntensity = specColor.r * MatSpecularIntensity;

    vec3 emsvColor = texture(EmsvMap0, sUV, MipBias).rgb * MatEmissiveIntensity;
    // Keep emissive out of albedo so lighting doesn't tint/emphasize layer color unexpectedly.
    vec3 albedo = mix(diff0.rgb, diff1.rgb, blend);

    float specPowerPacked = clamp(MatSpecularPower / 255.0, 0.0, 1.0);

    gPositionVS = vec4(sViewPos, 1.0);
    gPositionWS = vec4(sWorldPos, (ReceivesDecals > 0) ? 1.0 : 0.0);
    gNormalDepthPacked = vec4(worldSpaceNormal * 0.5 + 0.5, specPowerPacked);
    gAlbedoSpec = vec4(albedo, specIntensity);
    gEmissive = vec4(emsvColor, 0.0);
}
