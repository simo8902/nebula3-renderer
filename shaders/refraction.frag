#version 460 core

#ifdef BINDLESS
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

struct RefractionMaterialGPU {
    uint64_t distortHandle;
    float velocityX;
    float velocityY;
    float distortionScale;
    float pad0;
    float pad1;
    float pad2;
};

layout(std430, binding = 2) readonly buffer RefractionMaterialBuffer {
    RefractionMaterialGPU refractionMaterials[];
};

flat in uint vMaterialID;
#else
uniform sampler2D DistortMap;
uniform vec2 velocity;
uniform float distortionScale;
#endif

in vec2 vUV;
uniform sampler2D sceneTex;
uniform float time;
uniform vec2 invViewport;

layout(location=0) out vec4 fragColor;

void main() {
#ifdef BINDLESS
    RefractionMaterialGPU rmat = refractionMaterials[vMaterialID];
    vec2 uv = vUV + vec2(rmat.velocityX, rmat.velocityY) * time;
    vec4 distort = texture(sampler2D(rmat.distortHandle), uv);
    float distScale = rmat.distortionScale;
#else
    vec2 uv = vUV + velocity * time;
    vec4 distort = texture(DistortMap, uv);
    float distScale = distortionScale;
#endif

    if (distort.a < 0.01) discard;

    vec2 offset = (distort.rg * 2.0 - 1.0) * distScale;
    vec2 baseUV = gl_FragCoord.xy * invViewport;
    vec2 sceneUV = clamp(baseUV + offset, vec2(0.001), vec2(0.999));
    vec3 sceneColor = texture(sceneTex, sceneUV).rgb;

    fragColor = vec4(sceneColor, 1.0);
}
