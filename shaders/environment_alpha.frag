#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform samplerCube EnvironmentMap;

uniform vec3 eyePos;
uniform float Reflectivity;
uniform int DisableViewDependentReflection;

uniform float MatSpecularIntensity;
uniform int twoSided;
uniform int isFlatNormal;
uniform float alphaBlendFactor;

in vec3 vWorldPos;
in vec2 vUV;
in vec2 vUV1;
in vec3 vTangent;
in vec3 vNormal;
in vec3 vBinormal;

out vec4 FragColor;

void main() {
    const float MipBias = -0.5;

    vec4 diffColor = texture(DiffMap0, vUV, MipBias);
    vec4 specColor = texture(SpecMap0, vUV, MipBias);
    vec4 emsvSample = texture(EmsvMap0, vUV, MipBias);
    float emsvLuma = max(emsvSample.r, max(emsvSample.g, emsvSample.b));
    // Some environment alpha assets pack coverage outside DiffMap0.a.
    float alphaMask = max(diffColor.a, max(specColor.a, max(emsvSample.a, emsvLuma)));
    float alpha = clamp(alphaMask * alphaBlendFactor, 0.0, 1.0);
    if (alpha <= 0.001) {
        discard;
    }

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
    tNormal.z  = sqrt(max(0.0, 1.0 - dot(tNormal.xy, tNormal.xy)));

    vec3 worldSpaceNormal = (isFlatNormal > 0) ? N : normalize(mat3(T, B, N) * tNormal);

    float specIntensity = specColor.r * MatSpecularIntensity;
    vec3 emsvColor = emsvSample.rgb;

    vec3 envColor = vec3(0.0);
    float refl = 0.0;
    if (DisableViewDependentReflection == 0) {
        vec3 viewVec = vWorldPos - eyePos;
        viewVec.y *= 0.25;
        vec3 worldViewVec = normalize(viewVec);
        vec3 envDir = reflect(worldViewVec, worldSpaceNormal);
        float gloss = clamp(specColor.r, 0.0, 1.0);
        float roughness = clamp(1.0 - gloss, 0.1, 1.0);
        float envMip = mix(0.25, 3.5, roughness * roughness);
        envColor = textureLod(EnvironmentMap, envDir, envMip).rgb;
        refl = clamp(Reflectivity * specColor.r, 0.0, 1.0);
    }

    vec3 litColor = diffColor.rgb + emsvColor + (envColor * refl);
    FragColor = vec4(litColor, alpha);
}
