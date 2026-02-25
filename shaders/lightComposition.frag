#version 460 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D lightBufferTex;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gPositionVS;
uniform sampler2D gEmissiveTex;
uniform sampler2D gNormalDepthPacked;
uniform int DisableLighting;
uniform int DisableFog;
uniform vec4 fogColor;
uniform vec4 fogDistances;

vec4 DecodeHDR(vec4 rgba) {
    return rgba * vec4(2.0, 2.0, 2.0, 1.0);
}

vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
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
    float spec = min(lightValues.a, 1.0);
    color.rgb += specColor * specIntensity * spec * normedColor;
    return color;
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

    // AO path disabled: keep lighting unaffected by gPositionVS.a payload.
    float ao = 1.0;
    vec3 emsvColor = texture(gEmissiveTex, TexCoords).rgb;
    vec3 viewPos = texture(gPositionVS, TexCoords).xyz;

    lightValues.rgb *= ao;

    vec4 oColor;
    if (DisableLighting > 0) {
        oColor = vec4(albedo + emsvColor, 1.0);
    } else {
        oColor = psLightMaterial(lightValues, vec4(albedo, 1.0),
        emsvColor, 1.0,
        vec3(specInt), 1.0);
    }

    // Force fog fully disabled for diagnosis: no camera-distance greying.

    vec3 color = oColor.rgb;
    color *= 0.96;
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    color *= vec3(1.06, 1.00, 0.92);
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, 1.04);
    color = (color - 0.5) * 1.03 + 0.5;

    FragColor = vec4(clamp(color, 0.0, 1.0), oColor.a);
}
