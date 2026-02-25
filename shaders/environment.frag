#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform samplerCube CubeMap0;

uniform float Intensity0;
uniform vec3 eyePos;
uniform int ReceivesDecals;
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform int DisableViewDependentReflection;
uniform int alphaTest;
uniform float alphaCutoff;
uniform int twoSided;
uniform int isFlatNormal;

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
    const float MipBias = -0.5;

    vec4 diffColor = texture(DiffMap0, vUV, MipBias);
    if (alphaTest > 0 && diffColor.a < alphaCutoff) discard;

    vec4 specColor = texture(SpecMap0, vUV, MipBias);

    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBinormal);
    vec3 N = normalize(vNormal);

    if (twoSided > 0 && !gl_FrontFacing) {
        N = -N;
        T = -T;
        B = -B;
    }

    vec3 tNormal;
    vec4 bumpSample = texture(BumpMap0, vUV, MipBias);
    tNormal.xy = bumpSample.ag * 2.0 - 1.0;
    tNormal.z = sqrt(max(0.0, 1.0 - dot(tNormal.xy, tNormal.xy)));
    vec3 worldSpaceNormal = (isFlatNormal > 0) ? N : normalize(mat3(T, B, N) * tNormal);

    vec3 envColor = vec3(0.0);
    if (DisableViewDependentReflection == 0) {
        vec3 viewVec = vWorldPos - eyePos;
        // Reduce vertical camera sensitivity without hard clamping.
        viewVec.y *= 0.25;
        vec3 I = normalize(viewVec);
        vec3 envDir = reflect(I, worldSpaceNormal);
        float gloss = clamp(specColor.r, 0.0, 1.0);
        float roughness = clamp(1.0 - gloss, 0.1, 1.0);
        float envMip = mix(0.25, 3.5, roughness * roughness);
        envColor = textureLod(CubeMap0, envDir, envMip).rgb;
    }

    float reflMask = specColor.r;
    float envFactor = (DisableViewDependentReflection == 0)
        ? clamp(Intensity0 * reflMask * 1.35, 0.0, 1.0)
        : 0.0;
    float specIntensity = clamp(specColor.r * MatSpecularIntensity, 0.0, 1.0);
    float specPowerPacked = clamp(MatSpecularPower / 255.0, 0.0, 1.0);
    vec3 emsvColor = texture(EmsvMap0, vUV, MipBias).rgb * MatEmissiveIntensity;
    vec3 V = normalize(eyePos - vWorldPos);
    float ndv = clamp(dot(worldSpaceNormal, V), 0.0, 1.0);
    float fresnel = 0.25 + 0.75 * pow(1.0 - ndv, 4.0);
    vec3 envContribution = envColor * envFactor * fresnel;

    gPositionVS = vec4(vViewPos, 1.0);
    gPositionWS = vec4(vWorldPos, (ReceivesDecals > 0) ? 1.0 : 0.0);
    gNormalDepthPacked = vec4(worldSpaceNormal * 0.5 + 0.5, specPowerPacked);
    gAlbedoSpec = vec4(diffColor.rgb, specIntensity);
    gEmissive = vec4(emsvColor + envContribution, 0.0);
}
