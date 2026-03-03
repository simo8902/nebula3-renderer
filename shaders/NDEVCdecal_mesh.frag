#version 460 core
#ifdef BINDLESS
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#endif

// 0 Normal rendering
// 1 Show world position (red=X, green=Z, blue=Y)
// 2 Show mask UVs (local-space 0-1)
// 3 Show diffuse UVs (world-space tiled)
// 4 Show mask texture only (black/white)
// 5 Show diffuse texture only (no mask)
// 6 Show diffuse UV fractional part (to see tiling pattern)
#define DEBUG_MODE 0

#ifdef BINDLESS
struct DecalMaterialGPU {
    uint64_t diffuseHandle;
    uint64_t emissiveHandle;
    float    decalScale;
    uint     decalDiffuseMode;
    float    pad0;
    float    pad1;
};

layout(std430, binding = 4) readonly buffer DecalMaterialBuffer {
    DecalMaterialGPU decalMaterials[];
};

flat in uint vMaterialID;
#else
uniform sampler2D DiffMap0;
uniform sampler2D EmsvMap0;
uniform float DecalScale;
uniform int DecalDiffuseMode;
#endif

uniform sampler2D gPositionWS;
uniform sampler2D gNormalDepthPacked;

layout(std430, binding = 1) readonly buffer ModelMatrices {
    mat4 modelMatrices[];
};

layout(std430, binding = 3) readonly buffer DecalParams {
    vec4 decalParams[];
};

uniform vec2 screenSize;

flat in uint vInstanceID;

layout(location = 0) out vec4 outAlbedoSpec;

void main() {
    mat4 model = modelMatrices[vInstanceID];

    vec4 params1 = decalParams[vInstanceID * 2u + 0u];
    vec4 params2 = decalParams[vInstanceID * 2u + 1u];
    vec3 boxMin = vec3(params1.xyz);
    vec3 boxMax = vec3(params1.w, params2.xy);

    vec2 uv = gl_FragCoord.xy / screenSize;

    vec4 ws = texture(gPositionWS, uv);

    if (ws.w < 0.5) discard;

    vec3 worldPos = ws.xyz;
    if (dot(worldPos, worldPos) < 0.01) discard;

    vec4 existingNormalDepth = texture(gNormalDepthPacked, uv);
    vec3 surfaceNormal = normalize(existingNormalDepth.rgb * 2.0 - 1.0);

    if (surfaceNormal.y < -0.25) discard;

    mat4 invModel = inverse(model);
    vec4 local = invModel * vec4(worldPos, 1.0);

    if (local.x < boxMin.x || local.x > boxMax.x) discard;
    if (local.y < boxMin.y || local.y > boxMax.y) discard;
    if (local.z < boxMin.z || local.z > boxMax.z) discard;

    vec3 boxSize = boxMax - boxMin;
    vec3 safeSize = max(boxSize, vec3(1e-5));
    vec3 localNorm = (local.xyz - boxMin) / safeSize;

    vec3 axisX = normalize(vec3(model[0]));
    vec3 axisY = normalize(vec3(model[1]));
    vec3 axisZ = normalize(vec3(model[2]));
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    float ax = abs(dot(axisX, worldUp));
    float ay = abs(dot(axisY, worldUp));
    float az = abs(dot(axisZ, worldUp));

    vec2 maskUV;
    if (ax >= ay && ax >= az) {
        maskUV = localNorm.yz;
    } else if (ay >= ax && ay >= az) {
        maskUV = localNorm.xz;
    } else {
        maskUV = localNorm.xy;
    }
    maskUV.y = 1.0 - maskUV.y;
    maskUV = clamp(maskUV, 0.0, 1.0);

#ifdef BINDLESS
    DecalMaterialGPU mat = decalMaterials[vMaterialID];
    sampler2D diffSampler = sampler2D(mat.diffuseHandle);
    sampler2D emsvSampler = sampler2D(mat.emissiveHandle);
    float dScale = mat.decalScale;
    int dMode = int(mat.decalDiffuseMode);
#else
    float dScale = DecalScale;
    int dMode = DecalDiffuseMode;
#endif

    vec2 diffuseUV = maskUV;
    if (dMode != 0) {
        diffuseUV = worldPos.xz;
        if (dScale > 0.001) {
            diffuseUV *= dScale;
        }
    }

#if DEBUG_MODE == 1
    vec3 worldVis = fract(worldPos * 0.1);
    outAlbedoSpec = vec4(worldVis, 1.0);
    return;
#elif DEBUG_MODE == 2
    outAlbedoSpec = vec4(maskUV, 0.0, 1.0);
    return;
#elif DEBUG_MODE == 3
    vec2 diffuseVis = fract(diffuseUV);
    outAlbedoSpec = vec4(diffuseVis, 0.0, 1.0);
    return;
#elif DEBUG_MODE == 4
#ifdef BINDLESS
    float maskVis = texture(emsvSampler, maskUV).r;
#else
    float maskVis = texture(EmsvMap0, maskUV).r;
#endif
    outAlbedoSpec = vec4(vec3(maskVis), 1.0);
    return;
#elif DEBUG_MODE == 5
#ifdef BINDLESS
    vec3 diffVis = texture(diffSampler, diffuseUV).rgb;
#else
    vec3 diffVis = texture(DiffMap0, diffuseUV).rgb;
#endif
    outAlbedoSpec = vec4(diffVis, 1.0);
    return;
#elif DEBUG_MODE == 6
    vec2 uvFract = fract(diffuseUV * 0.5);
    outAlbedoSpec = vec4(uvFract, 0.0, 1.0);
    return;
#endif

#ifdef BINDLESS
    vec3 decalAlbedo = texture(diffSampler, diffuseUV).rgb;
    float mask = texture(emsvSampler, maskUV).r;
#else
    vec3 decalAlbedo = texture(DiffMap0, diffuseUV).rgb;
    float mask = texture(EmsvMap0, maskUV).r;
#endif

    // Softer edge fade for more natural blending
    vec2 fadeXZ = min(localNorm.xz, 1.0 - localNorm.xz);
    float edge = smoothstep(0.0, 0.2, min(fadeXZ.x, fadeXZ.y));

    // Normal-based fade for realistic angle blending
    float normalFade = smoothstep(0.3, 0.7, surfaceNormal.y);

    // Reduce opacity for softer, more natural look
    float alpha = mask * edge * normalFade * 0.85;
    alpha = clamp(alpha, 0.0, 1.0);

    if (alpha < 0.01) discard;

    outAlbedoSpec = vec4(decalAlbedo, alpha);
}
