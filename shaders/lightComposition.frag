#version 460 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D lightBufferTex;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gDepthTex;
uniform sampler2D gEmissiveTex;
uniform sampler2D gNormalDepthPacked;
uniform mat4 invProjection;
uniform int DisableLighting;
uniform int DisableFog;
uniform vec4 fogColor;
uniform vec4 fogDistances;

vec4 DecodeHDR(vec4 rgba) {
    return rgba * vec4(2.0, 2.0, 2.0, 1.0);
}

vec4 psLightMaterial(vec4 lightValues, vec4 diffColor, vec3 emsvColor,
float emsvIntensity, vec3 specColor, float specIntensity) {
    lightValues = DecodeHDR(lightValues);
    vec4 color = diffColor;
    color.rgb *= lightValues.rgb;
    color.rgb += emsvColor * emsvIntensity;
    vec3 normedColor = normalize(lightValues.rgb + vec3(0.0001));
    float maxColor = max(max(normedColor.x, normedColor.y), normedColor.z);
    normedColor /= maxColor;
    float spec = lightValues.a;
    color.rgb += specColor * specIntensity * spec * normedColor;
    return color;
}

vec3 reconstructViewPosFromDepth(vec2 uv, float depthSample) {
    vec2 ndcXY = uv * 2.0 - 1.0;
    float ndcZ = depthSample * 2.0 - 1.0;
    vec4 clipPos = vec4(ndcXY, ndcZ, 1.0);
    vec4 viewPos = invProjection * clipPos;
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

void main() {
    vec4 nd = texture(gNormalDepthPacked, TexCoords);
    if (dot(nd.xyz, nd.xyz) < 0.001) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 lightValues = texture(lightBufferTex, TexCoords);

    vec4 albedoSpec = texture(gAlbedoSpec, TexCoords);
    vec3 albedo = albedoSpec.rgb;
    float specInt = albedoSpec.a;

    // AO path disabled.
    float ao = 1.0;
    vec3 emsvColor = texture(gEmissiveTex, TexCoords).rgb;
    float depthSample = texture(gDepthTex, TexCoords).r;
    vec3 viewPos = reconstructViewPosFromDepth(TexCoords, depthSample);

    lightValues.rgb *= ao;

    vec4 oColor;
    if (DisableLighting > 0) {
        oColor = vec4(albedo + emsvColor, 1.0);
    } else {
        oColor = psLightMaterial(lightValues, vec4(albedo, 1.0),
        emsvColor, 1.0,
        vec3(specInt), 1.0);
    }

    if (DisableFog == 0) {
        float fogRange = max(fogDistances.y - fogDistances.x, 0.0001);
        float fogFactor = clamp((fogDistances.y - abs(viewPos.z)) / fogRange, fogColor.w, 1.0);
        oColor.rgb = mix(fogColor.rgb, oColor.rgb, fogFactor);
    }

    FragColor = oColor;
}
