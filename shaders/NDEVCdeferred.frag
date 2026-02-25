#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D SpecMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;

uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform float depthScale;         // added — original PS had this for depth packing

uniform int ReceivesDecals;
uniform int alphaTest;
uniform float alphaCutoff;
uniform int twoSided;
uniform int isFlatNormal;

in vec3 sWorldPos;
in vec3 sViewPos;
in vec2 sUV;
in vec2 sUV1;
in vec3 sTangent;
in vec3 sNormal;
in vec3 sBinormal;

layout(location=0) out vec4 gNormalDepth;   // matches original oC0: xy=encoded normal, zw=packed depth
layout(location=1) out vec4 gAlbedoSpec;
layout(location=2) out vec4 gPositionWS;
layout(location=3) out vec4 gEmissive;

void main() {
    const float MipBias = -0.5;

    vec4 diffColor = texture(DiffMap0, sUV, MipBias);
    if (alphaTest > 0 && diffColor.a < alphaCutoff) discard;

    vec3 T = normalize(sTangent);
    vec3 B = normalize(sBinormal);
    vec3 N = normalize(sNormal);

    if (twoSided > 0 && !gl_FrontFacing) { N = -N; T = -T; B = -B; }

    // DXT5nm unpack (.w and .y channels like original)
    vec4 bump = texture(BumpMap0, sUV, MipBias);
    vec2 n2   = bump.wy * 2.0 - 1.0;
    float nz  = sqrt(max(1.0 - dot(n2, n2), 0.0));

    // TBN is view-space so result is view-space normal — matches original pipeline
    vec3 viewN = (isFlatNormal > 0) ? N : normalize(n2.x * T + n2.y * B + nz * N);

    // spheremap encode — matches original exactly
    vec3 sn = viewN;
    sn.z += 1.0;
    vec2 encodedN = (sn.xy / sn.z) * 0.281262308 + 0.5;

    // depth pack: split into integer and fractional parts across .zw
    float depth     = length(sViewPos) * depthScale * (1.0 / 256.0);
    float depthFrac = fract(depth);
    float depthInt  = (depth - depthFrac) * (1.0 / 256.0);

    vec4 specColor    = texture(SpecMap0, sUV, MipBias);
    float specIntensity  = clamp(specColor.r * MatSpecularIntensity, 0.0, 1.0);
    float specPowerPacked = clamp(MatSpecularPower / 255.0, 0.0, 1.0);

    vec3 emsvColor = texture(EmsvMap0, sUV, MipBias).rgb * MatEmissiveIntensity;

    gNormalDepth = vec4(encodedN, depthInt, depthFrac);
    gAlbedoSpec  = vec4(diffColor.rgb, specIntensity);
    gPositionWS  = vec4(sWorldPos, (ReceivesDecals > 0) ? 1.0 : 0.0);
    gEmissive    = vec4(emsvColor, specPowerPacked);
}