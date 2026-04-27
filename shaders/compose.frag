#version 460 core

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vTexCoord1;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D bloomSampler;
layout(binding = 1) uniform sampler2D colorSampler;
layout(binding = 2) uniform sampler2D normalMapSampler;
layout(binding = 3) uniform sampler2D highlightSampler;

uniform float hdrBloomScale;
uniform vec4 satBrightGamma;
uniform vec4 balance;
uniform vec4 luminance;
uniform float fadeValue;
uniform vec4 vignetteColor;
uniform vec4 vignetteControl;
uniform float brightnessOffset;
uniform float contrastScale;

vec3 saturate(vec3 v)
{
    return clamp(v, vec3(0.0), vec3(1.0));
}

float saturate(float v)
{
    return clamp(v, 0.0, 1.0);
}

void main()
{
    vec4 baseColor = texture(colorSampler, vTexCoord);

    vec3 color = baseColor.rgb;
    color += texture(bloomSampler, vTexCoord).rgb * hdrBloomScale;

    float alpha = dot(color, luminance.rgb);

    color = mix(vec3(alpha), color, satBrightGamma.x);
    color *= balance.rgb;

    if (abs(satBrightGamma.z - 1.0) > 0.01)
        color = pow(saturate(color), vec3(satBrightGamma.z));

    color = (color - vec3(0.5)) * contrastScale + vec3(0.5);
    color += vec3(brightnessOffset);
    color = saturate(color + satBrightGamma.y);
    color += texture(highlightSampler, vTexCoord).rgb;

    vec2 vignetteDelta = vec2(0.5) - vTexCoord;
    float vignette = saturate(pow(length(vignetteDelta), vignetteControl.x) * vignetteControl.y);

    color = mix(color, vignetteColor.rgb, vignette);
    color *= fadeValue;

    outColor = vec4(color, alpha);
}
