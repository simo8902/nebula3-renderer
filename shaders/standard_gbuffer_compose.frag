#version 460 core

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;

layout(location = 0) uniform sampler2D packedGBuffer;
layout(location = 1) uniform sampler2D albedoBuffer;
layout(location = 2) uniform sampler2D lightBuffer;
layout(location = 3) uniform float depthScale;

layout(std140, binding = 0) uniform VSRegisters {
    vec4 vc[228];
};

#define VIEW_ROW0 vc[4]
#define VIEW_ROW1 vc[5]
#define VIEW_ROW2 vc[6]

vec3 decodeDualParaboloidNormal(vec2 encoded)
{
    vec2 p = (encoded - 0.5) / 0.281262308;
    float len2 = dot(p, p);
    float inv = 1.0 / max(1.0 + len2, 0.000001);
    return normalize(vec3(2.0 * p * inv, (1.0 - len2) * inv));
}

float decodeLinearDepth(vec2 packedDepth)
{
    float depthVal = packedDepth.x * 256.0 + packedDepth.y;
    return depthVal * 256.0 / max(depthScale, 0.000001);
}

vec3 transformDirectionToView(vec3 dirWS)
{
    return normalize(vec3(dot(dirWS, VIEW_ROW0.xyz),
                          dot(dirWS, VIEW_ROW1.xyz),
                          dot(dirWS, VIEW_ROW2.xyz)));
}

void main()
{
    vec4 gbufferSample = texture(packedGBuffer, vTexCoord);
    vec4 albedoSample = texture(albedoBuffer, vTexCoord);
    vec4 lightSample = texture(lightBuffer, vTexCoord);
    vec3 normalVS = decodeDualParaboloidNormal(gbufferSample.xy);
    float linearDepth = decodeLinearDepth(gbufferSample.zw);

    if (linearDepth <= 0.0 && gbufferSample.z <= 0.0 && gbufferSample.w <= 0.0) {
        outColor = vec4(0.015, 0.016, 0.018, 1.0);
        return;
    }

    vec3 base = albedoSample.rgb;
    vec3 color = base * (lightSample.rgb * 2.0);
    vec3 upDirVS = transformDirectionToView(vec3(0.0, 1.0, 0.0));
    color += vec3(0.01, 0.012, 0.014) * (0.5 + 0.5 * dot(normalVS, upDirVS));

    float fog = smoothstep(1800.0, 3000.0, linearDepth);
    color = mix(color, vec3(0.035, 0.038, 0.042), fog);
    outColor = vec4(color, 1.0);
}
