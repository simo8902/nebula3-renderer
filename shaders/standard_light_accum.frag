#version 460 core

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;

layout(location = 0) uniform sampler2D packedGBuffer;
layout(location = 1) uniform float depthScale;

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
    vec3 normalVS = decodeDualParaboloidNormal(gbufferSample.xy);
    float linearDepth = decodeLinearDepth(gbufferSample.zw);

    if (linearDepth <= 0.0 && gbufferSample.z <= 0.0 && gbufferSample.w <= 0.0) {
        outColor = vec4(0.0);
        return;
    }

    vec3 keyDirVS = transformDirectionToView(normalize(vec3(-0.32, 0.44, 0.84)));
    vec3 fillDirVS = transformDirectionToView(normalize(vec3(0.56, -0.18, 0.81)));
    vec3 upDirVS = transformDirectionToView(vec3(0.0, 1.0, 0.0));
    vec3 halfDirVS = normalize(keyDirVS + vec3(0.0, 0.0, 1.0));

    float key = max(dot(normalVS, keyDirVS), 0.0);
    float fill = max(dot(normalVS, fillDirVS), 0.0);
    float horizon = 0.5 + 0.5 * dot(normalVS, upDirVS);
    float spec = pow(max(dot(normalVS, halfDirVS), 0.0), 16.0);

    vec3 light = vec3(0.18) +
                 vec3(0.76, 0.74, 0.68) * key +
                 vec3(0.16, 0.18, 0.22) * fill +
                 vec3(0.035, 0.04, 0.045) * horizon;

    float fog = smoothstep(1800.0, 3000.0, linearDepth);
    light = mix(light, vec3(0.1, 0.1, 0.1), fog);
    spec = mix(spec, 0.0, fog);

    outColor = vec4(light, spec);
}
