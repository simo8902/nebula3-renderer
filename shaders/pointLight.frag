#version 460 core
out vec4 FragColor;

uniform sampler2D gNormalDepthPacked;
uniform sampler2D gPositionWS;
uniform vec2 screenSize;
uniform vec3 CameraPos;
uniform vec4 lightPosRange;
uniform vec4 lightColorIn;
uniform int UseInstancedPointLights;
uniform int DisableViewDependentSpecular;

flat in vec4 vLightPosRange; // xyz = pos, w = range
flat in vec4 vLightColor;

const float specPower = 32.0;
// Nebula lightsources.fx uses standard luminance weights.
const vec3 luminanceValue = vec3(0.299, 0.587, 0.114);
const float exaggerateSpec = 1.8;

vec4 EncodeHDR(vec4 rgba) {
    return rgba * vec4(0.5, 0.5, 0.5, 1.0);
}

void main() {
    vec2 screenUv = gl_FragCoord.xy / screenSize;

    vec4 nd = texture(gNormalDepthPacked, screenUv);
    if (dot(nd.xyz, nd.xyz) < 0.001) discard;

    vec3 normalWS = normalize(nd.rgb * 2.0 - 1.0);
    vec3 surfacePos = texture(gPositionWS, screenUv).xyz;

    vec4 lp = (UseInstancedPointLights != 0) ? vLightPosRange : lightPosRange;
    vec4 lc = (UseInstancedPointLights != 0) ? vLightColor : lightColorIn;

    vec3 lightDir = lp.xyz - surfacePos;
    float dist = length(lightDir);
    float lightRange = max(lp.w, 0.001);
    float att = clamp(1.0 - dist / lightRange, 0.0, 1.0);
    att *= att;
    if (att < 0.004) discard;

    lightDir = normalize(lightDir);

    float NL = clamp(dot(lightDir, normalWS), 0.0, 1.0);
    vec3 diff = lc.rgb * NL * att;

    float diffLuminance = dot(diff, luminanceValue) * exaggerateSpec;
    float spec = 0.0;
    if (DisableViewDependentSpecular == 0) {
        vec3 V = normalize(CameraPos - surfacePos);
        vec3 H = normalize(lightDir + V);
        float NH = clamp(dot(normalWS, H), 0.0, 1.0);
        spec = pow(NH, specPower) * diffLuminance;
    } else {
        spec = pow(NL, specPower) * diffLuminance;
    }

    FragColor = EncodeHDR(vec4(diff, spec));
}
