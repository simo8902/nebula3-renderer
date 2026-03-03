#version 460 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D gNormalDepthPacked;
uniform sampler2D gPositionWS;
uniform mat4 view;
uniform sampler2DShadow shadowMapCascade0;
uniform sampler2DShadow shadowMapCascade1;
uniform sampler2DShadow shadowMapCascade2;
uniform sampler2DShadow shadowMapCascade3;

uniform vec3 CameraPos;
uniform vec3 LightDirWS;
uniform vec3 LightColor;
uniform vec3 AmbientColor;
uniform vec3 BackLightColor;
uniform float BackLightOffset;
uniform mat4 lightSpaceMatrices[4];
uniform float cascadeSplits[5];
uniform int numCascades;
uniform int DisableShadows;
uniform int DisableViewDependentSpecular;

const float specPower = 32.0;
const vec3 luminanceValue = vec3(0.299, 0.587, 0.114);
const float exaggerateSpec = 1.8;

vec4 EncodeHDR(vec4 rgba) {
    return rgba * vec4(0.5, 0.5, 0.5, 1.0);
}

float SampleShadowCascade(int cascadeIndex, vec3 projCoords, float currentDepth, float bias) {
    const vec2 poissonDisk[25] = vec2[](
    vec2(-0.978698, -0.0884121), vec2(-0.841121, 0.521165), vec2(-0.71746, -0.50322),
    vec2(-0.702933, 0.903134), vec2(-0.663198, 0.15482), vec2(-0.495102, -0.232887),
    vec2(-0.364238, -0.961791), vec2(-0.345866, 0.706487), vec2(-0.33626, 0.381031),
    vec2(-0.275275, -0.668151), vec2(0.0107666, 0.638907), vec2(0.0639475, -0.0681226),
    vec2(0.113128, 0.268634), vec2(0.18706, -0.793242), vec2(0.291974, -0.369673),
    vec2(0.429639, 0.883924), vec2(0.523456, 0.12399), vec2(0.606333, -0.644151),
    vec2(0.646219, -0.0992476), vec2(0.668338, 0.687348), vec2(0.753849, 0.443304),
    vec2(0.771438, 0.00860578), vec2(0.790032, -0.399114), vec2(0.886966, 0.325719),
    vec2(0.963744, -0.237328)
    );

    float shadow = 0.0;
    vec2 texelSize;
    if (cascadeIndex == 0) {
        texelSize = 1.0 / vec2(textureSize(shadowMapCascade0, 0));
    } else if (cascadeIndex == 1) {
        texelSize = 1.0 / vec2(textureSize(shadowMapCascade1, 0));
    } else if (cascadeIndex == 2) {
        texelSize = 1.0 / vec2(textureSize(shadowMapCascade2, 0));
    } else {
        texelSize = 1.0 / vec2(textureSize(shadowMapCascade3, 0));
    }
    float filterRadius = 2.5;

    for(int i = 0; i < 25; ++i) {
        vec2 offset = poissonDisk[i] * texelSize * filterRadius;
        float s;
        if(cascadeIndex == 0)
        s = texture(shadowMapCascade0, vec3(projCoords.xy + offset, currentDepth - bias));
        else if(cascadeIndex == 1)
        s = texture(shadowMapCascade1, vec3(projCoords.xy + offset, currentDepth - bias));
        else if(cascadeIndex == 2)
        s = texture(shadowMapCascade2, vec3(projCoords.xy + offset, currentDepth - bias));
        else
        s = texture(shadowMapCascade3, vec3(projCoords.xy + offset, currentDepth - bias));
        shadow += s;
    }

    return shadow / 25.0;
}

float CalcShadowCascaded(vec3 worldPos, vec3 viewPos, vec3 normal, float NdotL) {
    float depth = abs(viewPos.z);
    int cascadeCount = clamp(numCascades, 1, 4);
    int lastCascade = cascadeCount - 1;
    int cascadeIndex = lastCascade;
    float blendFactor = 0.0;
    const float blendRange = 5.0;

    for (int i = 0; i < lastCascade; ++i) {
        float splitFar = cascadeSplits[i + 1];
        if (depth < splitFar) {
            cascadeIndex = i;
            float distToEdge = splitFar - depth;
            if (distToEdge < blendRange) blendFactor = 1.0 - (distToEdge / blendRange);
            break;
        }
    }

    mat4 lightSpaceMatrix = lightSpaceMatrices[cascadeIndex];
    vec4 fragPosLight = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLight.xyz / fragPosLight.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
    projCoords.y < 0.0 || projCoords.y > 1.0)
    return 1.0;

    float currentDepth = projCoords.z;
    float bias = max(0.003 * (1.0 - NdotL), 0.0005);

    float shadow = SampleShadowCascade(cascadeIndex, projCoords, currentDepth, bias);

    if(blendFactor > 0.0 && cascadeIndex < lastCascade) {
        mat4 nextLightSpaceMatrix = lightSpaceMatrices[cascadeIndex + 1];
        vec4 nextFragPosLight = nextLightSpaceMatrix * vec4(worldPos, 1.0);
        vec3 nextProjCoords = nextFragPosLight.xyz / nextFragPosLight.w;
        nextProjCoords = nextProjCoords * 0.5 + 0.5;

        if (nextProjCoords.z <= 1.0 && nextProjCoords.x >= 0.0 && nextProjCoords.x <= 1.0 &&
        nextProjCoords.y >= 0.0 && nextProjCoords.y <= 1.0) {
            float nextDepth = nextProjCoords.z;
            float nextShadow = SampleShadowCascade(cascadeIndex + 1, nextProjCoords, nextDepth, bias);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}

void main() {
    vec4 nd = texture(gNormalDepthPacked, TexCoords);
    if (dot(nd.xyz, nd.xyz) < 0.001) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 normalWS = normalize(nd.rgb * 2.0 - 1.0);
    vec3 worldPos = texture(gPositionWS, TexCoords).xyz;
    vec3 viewPos = (view * vec4(worldPos, 1.0)).xyz;

    vec3 L = normalize(LightDirWS);
    float NL = dot(normalWS, L);

    float shadow = (DisableShadows > 0)
        ? 1.0
        : CalcShadowCascaded(worldPos, viewPos, normalWS, max(NL, 0.0));

    // Nebula: spec luminance uses unshadowed diffuse (shadow composed separately)
    vec3 fullDiff = AmbientColor;
    fullDiff += LightColor * clamp(NL, 0.0, 1.0);
    float backNL = clamp(-NL + BackLightOffset, 0.0, 1.0);
    fullDiff += BackLightColor * backNL;

    // Shadowed diffuse for RGB output
    vec3 diff = AmbientColor;
    diff += LightColor * clamp(NL, 0.0, 1.0) * shadow;
    diff += BackLightColor * backNL;

    float diffLuminance = dot(fullDiff, luminanceValue) * exaggerateSpec;
    float spec = 0.0;
    if (DisableViewDependentSpecular == 0) {
        vec3 V = normalize(CameraPos - worldPos);
        vec3 H = normalize(L + V);
        float NH = clamp(dot(normalWS, H), 0.0, 1.0);
        spec = pow(NH, specPower) * diffLuminance;
    } else {
        float NLclamped = clamp(NL, 0.0, 1.0);
        spec = pow(NLclamped, specPower) * diffLuminance;
    }

    FragColor = EncodeHDR(vec4(diff, spec));
}
