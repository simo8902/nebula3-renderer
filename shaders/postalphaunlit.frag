#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D EmsvMap0;
uniform float MatEmissiveIntensity;
uniform float alphaBlendFactor;
uniform float diffContribution;

in vec2 sUV;
in vec4 sColor;

out vec4 FragColor;

void main() {
    vec4 diff = texture(DiffMap0, sUV) * sColor;
    vec4 emsvSample = texture(EmsvMap0, sUV);
    vec3 emsv = emsvSample.rgb * MatEmissiveIntensity * sColor.rgb;
    float emsvLuma = max(emsvSample.r, max(emsvSample.g, emsvSample.b));
    float alpha = max(diff.a, max(emsvSample.a, emsvLuma)) * alphaBlendFactor;
    FragColor = vec4(diff.rgb * diffContribution + emsv, alpha);
}
