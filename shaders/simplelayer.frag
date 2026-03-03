#version 460 core

#ifndef PASS
#define PASS 0
#endif

#if PASS == 4 || PASS == 5
// Standard G-buffer outputs for gbuffer passes
layout(location = 0) out vec4 gPositionVS;
layout(location = 1) out vec4 gNormalDepthPacked;
#else
layout(location = 0) out vec4 outColor;
#endif

#if PASS == 0

uniform vec4 highlightColor;
layout(binding = 0) uniform sampler2D diffMapSampler;
layout(location = 0) in vec2 vTexcoord;

void main()
{
    vec4 diffuse = texture(diffMapSampler, vTexcoord);
    outColor = vec4(highlightColor.rgb, diffuse.a);
}

#elif PASS >= 1 && PASS <= 3

uniform vec4 fogDistances;
uniform vec4 fogColor;
uniform vec4 heightFogColor;
uniform vec4 customColor2;

#if PASS == 1
uniform float alphaBlendFactor;
uniform float mayaAnimableAlpha;

layout(binding = 0) uniform sampler2D diffMapSampler;
layout(binding = 1) uniform sampler2D diffMap2Sampler;
layout(binding = 2) uniform sampler2D maskSampler;

#elif PASS == 2
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform float alphaBlendFactor;
uniform float mayaAnimableAlpha;
uniform float encodefactor;
uniform vec4 pixelSize;

layout(binding = 0) uniform sampler2D diffMapSampler;
layout(binding = 1) uniform sampler2D specMapSampler;
layout(binding = 2) uniform sampler2D emsvSampler;
layout(binding = 3) uniform sampler2D lightSampler;
layout(binding = 4) uniform sampler2D diffMap2Sampler;
layout(binding = 5) uniform sampler2D specMap1Sampler;
layout(binding = 6) uniform sampler2D maskSampler;

#elif PASS == 3
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform float encodefactor;
uniform vec4 pixelSize;

layout(binding = 0) uniform sampler2D diffMapSampler;
layout(binding = 1) uniform sampler2D specMapSampler;
layout(binding = 2) uniform sampler2D emsvSampler;
layout(binding = 3) uniform sampler2D lightSampler;
layout(binding = 4) uniform sampler2D diffMap2Sampler;
layout(binding = 5) uniform sampler2D specMap1Sampler;
layout(binding = 6) uniform sampler2D maskSampler;
#endif

layout(location = 1) in vec3 vUvAndHF;
layout(location = 2) in vec4 vClipPosCopy;
layout(location = 3) in vec4 vLayerUV;

void main()
{
    float invFogRange = 1.0 / (fogDistances.y - fogDistances.x);

    float distFog = (fogDistances.y - vClipPosCopy.z) * invFogRange;
    distFog = min(max(distFog, fogColor.w), 1.0);

    vec4 diffuse = texture(diffMapSampler, vUvAndHF.xy);
    vec4 layer2 = texture(diffMap2Sampler, vLayerUV.zw);
    vec4 mask = texture(maskSampler, vLayerUV.xy);

    vec4 blended = mix(diffuse, layer2, mask.x);

    vec3 tinted = mix(blended.rgb, customColor2.rgb, customColor2.w);

#if PASS == 1
    float alpha = blended.a * alphaBlendFactor * mayaAnimableAlpha;

    vec3 foggedColor = mix(fogColor.rgb, tinted, distFog);
    vec3 finalColor = mix(heightFogColor.rgb, foggedColor, vUvAndHF.z);

    outColor = vec4(finalColor * 0.5, alpha);

#else
    vec2 screenUV = (floor(gl_FragCoord.xy) + vec2(0.5)) * pixelSize.xy;
    vec4 lightColor = texture(lightSampler, screenUV);

    vec4 diffuseWithAlpha = vec4(blended.rgb, 1.0) * vec4(1.0, 1.0, 1.0, 0.0)
                          + vec4(0.0, 0.0, 0.0, 1.0);
    vec4 lit = lightColor * diffuseWithAlpha;
    vec3 color = lit.rgb * 2.0;

    float specPower = pow(abs(lit.w), MatSpecularPower);
    float encodedSpec = specPower * encodefactor;

    vec4 emissive = texture(emsvSampler, vUvAndHF.xy);
    color += emissive.rgb * MatEmissiveIntensity;

    vec4 spec0 = texture(specMapSampler, vUvAndHF.xy);
    vec4 spec1 = texture(specMap1Sampler, vLayerUV.zw);
    vec3 specBlended = mix(spec0.rgb, spec1.rgb, mask.x);
    color += specBlended * MatSpecularIntensity * encodedSpec;

    vec3 tintedColor = mix(color, customColor2.rgb, customColor2.w);

    vec3 foggedColor = mix(fogColor.rgb, tintedColor, distFog);
    vec3 finalColor = mix(heightFogColor.rgb, foggedColor, vUvAndHF.z);

#if PASS == 2
    float alpha = blended.a * alphaBlendFactor * mayaAnimableAlpha;
#else
    float alpha = 1.0;
#endif

    outColor = vec4(finalColor * 0.5, alpha);
#endif
}

#elif PASS == 4

uniform float AlphaClipRef;
uniform mat3 invViewRot;

layout(binding = 0) uniform sampler2D diffMapSampler;
layout(binding = 1) uniform sampler2D bumpMapSampler;
layout(binding = 2) uniform sampler2D bumpMap1Sampler;
layout(binding = 3) uniform sampler2D maskSampler;

layout(location = 0) in vec2 vTexcoord;
layout(location = 1) in vec3 vTangentVS;
layout(location = 2) in vec3 vNormalVS;
layout(location = 3) in vec3 vBinormalVS;
layout(location = 4) in vec4 vViewPos;
layout(location = 5) in vec4 vLayerUV;

void main()
{
    float clipThreshold = AlphaClipRef * (1.0 / 256.0);

    vec4 diffuse = texture(diffMapSampler, vTexcoord);
    if ((diffuse.a - clipThreshold) < 0.0) {
        discard;
    }

    vec4 bump0 = texture(bumpMapSampler, vTexcoord);
    vec2 n0xy = bump0.wy * 2.0 - 1.0;
    float n0z = sqrt(clamp(1.0 - dot(n0xy, n0xy), 0.0, 1.0));
    vec3 normal0 = vec3(n0xy, n0z);

    vec4 bump1 = texture(bumpMap1Sampler, vLayerUV.zw);
    vec2 n1xy = bump1.wy * 2.0 - 1.0;
    float n1z = sqrt(clamp(1.0 - dot(n1xy, n1xy), 0.0, 1.0));
    vec3 normal1 = vec3(n1xy, n1z);

    vec4 maskVal = texture(maskSampler, vLayerUV.xy);
    vec3 blendedNormal = mix(normal0, normal1, maskVal.x);

    vec3 viewNormal = normalize(blendedNormal.x * vTangentVS
                              + blendedNormal.y * vBinormalVS
                              + blendedNormal.z * vNormalVS);

    // Convert view-space normal to world-space, encode linearly
    vec3 worldNormal = normalize(invViewRot * viewNormal);

    gPositionVS = vec4(vViewPos.xyz, 1.0);
    gNormalDepthPacked = vec4(worldNormal * 0.5 + 0.5, 0.0);
}

#elif PASS == 5

uniform mat3 invViewRot;

layout(binding = 0) uniform sampler2D bumpMapSampler;
layout(binding = 1) uniform sampler2D bumpMap1Sampler;
layout(binding = 2) uniform sampler2D maskSampler;

layout(location = 0) in vec2 vTexcoord;
layout(location = 1) in vec3 vTangentVS;
layout(location = 2) in vec3 vNormalVS;
layout(location = 3) in vec3 vBinormalVS;
layout(location = 4) in vec4 vViewPos;
layout(location = 5) in vec4 vLayerUV;

void main()
{
    vec4 bump0 = texture(bumpMapSampler, vTexcoord);
    vec2 n0xy = bump0.wy * 2.0 - 1.0;
    float n0z = sqrt(clamp(1.0 - dot(n0xy, n0xy), 0.0, 1.0));
    vec3 normal0 = vec3(n0xy, n0z);

    vec4 bump1 = texture(bumpMap1Sampler, vLayerUV.zw);
    vec2 n1xy = bump1.wy * 2.0 - 1.0;
    float n1z = sqrt(clamp(1.0 - dot(n1xy, n1xy), 0.0, 1.0));
    vec3 normal1 = vec3(n1xy, n1z);

    vec4 maskVal = texture(maskSampler, vLayerUV.xy);
    vec3 blendedNormal = mix(normal0, normal1, maskVal.x);

    vec3 viewNormal = normalize(blendedNormal.x * vTangentVS
                              + blendedNormal.y * vBinormalVS
                              + blendedNormal.z * vNormalVS);

    // Convert view-space normal to world-space, encode linearly
    vec3 worldNormal = normalize(invViewRot * viewNormal);

    gPositionVS = vec4(vViewPos.xyz, 1.0);
    gNormalDepthPacked = vec4(worldNormal * 0.5 + 0.5, 0.0);
}

#elif PASS == 6

layout(binding = 0) uniform sampler2D diffMapSampler;

layout(location = 0) in vec2 vTexcoord;
layout(location = 1) in vec4 vClipPosCopy;

void main()
{
    vec4 diffuse = texture(diffMapSampler, vTexcoord);
    if ((diffuse.a - 0.01) < 0.0) {
        discard;
    }

    float depth = vClipPosCopy.z / vClipPosCopy.w;
    outColor = vec4(depth);
}

#elif PASS == 7

layout(location = 0) in vec4 vClipPosCopy;

void main()
{
    float depth = vClipPosCopy.z / vClipPosCopy.w;
    outColor = vec4(depth);
}

#endif
