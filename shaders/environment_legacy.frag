#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform samplerCube CubeMap0;
uniform sampler2D AOMap;
uniform int ReceivesDecals;

uniform float Intensity0;
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;

uniform int alphaTest;
uniform float alphaCutoff;
uniform int twoSided;
uniform int isFlatNormal;
uniform int UseAO;
uniform float AOStrength;

uniform vec3 eyePos;

in vec3 vWorldPos;
in vec3 vViewPos;
in vec2 vUV;
in vec2 vUV1;
in vec3 vTangent;
in vec3 vNormal;
in vec3 vBinormal;

layout(location=0) out vec4 gPositionVS;
layout(location=1) out vec4 gNormalDepthPacked;
layout(location=2) out vec4 gAlbedoSpec;
layout(location=3) out vec4 gPositionWS;
layout(location=4) out vec4 gEmissive;

void main() {
    const float MipBias = -1.0;

    vec4 diffColor = texture(DiffMap0, vUV, MipBias);
    if (alphaTest > 0 && diffColor.a < alphaCutoff) discard;

    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBinormal);
    vec3 N = normalize(vNormal);

    if (twoSided > 0 && !gl_FrontFacing) {
        N = -N; T = -T; B = -B;
    }

    vec3 tNormal;
    vec4 bumpSample = texture(BumpMap0, vUV, MipBias);
    tNormal.xy = bumpSample.ag * 2.0 - 1.0;
    tNormal.z  = sqrt(max(0.0, 1.0 - dot(tNormal.xy, tNormal.xy)));

    vec3 worldSpaceNormal = (isFlatNormal > 0) ? N : normalize(mat3(T, B, N) * tNormal);

    vec3 I = normalize(vWorldPos - eyePos);
    vec3 R = reflect(I, worldSpaceNormal);
    vec4 environmentColor = texture(CubeMap0, R);

    vec4 specColor = texture(SpecMap0, vUV, MipBias);
    float specIntensity = specColor.r * MatSpecularIntensity;

    vec3 emsvColor = texture(EmsvMap0, vUV, MipBias).rgb * MatEmissiveIntensity;

    float ao = 1.0;
    if (UseAO > 0) {
        ao = texture(AOMap, vUV1, MipBias).r;
        if (ao < 0.01) ao = texture(AOMap, vUV, MipBias).r;
        ao = mix(1.0, ao, clamp(AOStrength, 0.0, 1.0));
    }

    float envFactor = clamp(Intensity0 * specColor.a, 0.0, 1.0);
    vec3 envMix = mix(diffColor.rgb, environmentColor.rgb, envFactor);
    // AO is applied in composition from gPositionVS.a; don't apply it here again.
    vec3 albedo = envMix + emsvColor;
    float specPowerPacked = clamp(MatSpecularPower / 255.0, 0.0, 1.0);

    // Compatibility with current deferred composition: AO is expected in gPositionVS.a.
    gPositionVS = vec4(vViewPos, ao);
    gPositionWS = vec4(vWorldPos, (ReceivesDecals > 0) ? 1.0 : 0.0);
    gNormalDepthPacked = vec4(worldSpaceNormal * 0.5 + 0.5, specPowerPacked);
    gAlbedoSpec = vec4(albedo, specIntensity);
    gEmissive = vec4(0.0);
}
