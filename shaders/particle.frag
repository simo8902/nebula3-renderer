#version 430 core

in vec4 vColor;
in vec2 vTexCoord;
in vec3 vViewSpacePos;
in vec3 vWorldEyeVec;
in float vProjDepth;

uniform sampler2D particleTexture;
uniform sampler2D gPositionWSTex;
uniform mat4 view;
uniform int colorMode; // 0 = none, 1 = additive, 2 = alpha (unlit)
uniform vec2 invViewport;
uniform vec2 fogDistances;
uniform vec4 fogColor;
uniform float hdrScale;
uniform float Intensity0;
uniform float MatEmissiveIntensity;

out vec4 fragColor;

vec4 EncodeHDR(vec4 rgba) {
    return rgba * vec4(hdrScale, hdrScale, hdrScale, 1.0);
}

void main() {
    vec4 texColor = texture(particleTexture, vTexCoord);
    float intensity = max(Intensity0, 0.0);
    float emissive = max(MatEmissiveIntensity, 0.0);

    // soft particle factor using background world-position reconstruction
    float softAlphaMod = 1.0;
    if (invViewport.x > 0.0 && invViewport.y > 0.0) {
        vec2 screenUv = gl_FragCoord.xy * invViewport;
        float backGroundViewDepth = 1e6;
        vec4 bgWorldPos = texture(gPositionWSTex, screenUv);
        if (bgWorldPos.w > 0.0) {
            vec3 bgViewPos = (view * vec4(bgWorldPos.xyz, 1.0)).xyz;
            backGroundViewDepth = length(bgViewPos);
        }
        if (backGroundViewDepth < 1e-5) backGroundViewDepth = 1e6;
        float particleViewDepth = length(vViewSpacePos);
        float softAlpha = clamp((backGroundViewDepth - particleViewDepth) * 0.5, 0.0, 1.0);
        softAlphaMod = mix(softAlpha, 1.0, abs(vWorldEyeVec.y));
    }

    vec4 outColor = texColor;
    float fogIntensity = clamp((fogDistances.y - vProjDepth) / (fogDistances.y - fogDistances.x), fogColor.a, 1.0);
    if (colorMode == 1) {
        // additive particles: modulate by per-particle color (Nebula ParticleAdditive)
        outColor = texColor * vColor;
        outColor.rgb *= emissive;
        outColor *= fogIntensity * softAlphaMod;
        outColor.a *= intensity;
    } else if (colorMode == 2) {
        // alpha particles: unlit variant with fog (Nebula AlphaUnlit behavior)
        outColor = texColor * vColor;
        outColor.rgb = mix(fogColor.rgb, outColor.rgb, fogIntensity);
        outColor.a *= softAlphaMod * intensity;
    } else {
        outColor.a = texColor.a * softAlphaMod;
    }
    fragColor = EncodeHDR(outColor);
}
