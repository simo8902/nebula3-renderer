#version 460 core

in vec2 vUV;
uniform sampler2D sceneTex;
uniform sampler2D DistortMap;
uniform vec2 velocity;
uniform float distortionScale;
uniform float time;
uniform vec2 invViewport;

layout(location=0) out vec4 fragColor;

void main() {
    vec2 uv = vUV + velocity * time;
    vec4 distort = texture(DistortMap, uv);
    if (distort.a < 0.01) discard;

    vec2 offset = (distort.rg * 2.0 - 1.0) * distortionScale;
    vec2 baseUV = gl_FragCoord.xy * invViewport;
    vec2 sceneUV = clamp(baseUV + offset, vec2(0.001), vec2(0.999));
    vec3 sceneColor = texture(sceneTex, sceneUV).rgb;

    fragColor = vec4(sceneColor, 1.0);
}
