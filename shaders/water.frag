#version 460 core

uniform sampler2D DiffMap0;
uniform sampler2D BumpMap0;
uniform sampler2D EmsvMap0;
uniform samplerCube CubeMap0;

uniform vec3 eyePos;
uniform float MatEmissiveIntensity;
uniform float MatSpecularIntensity;
uniform float MatSpecularPower;
uniform float Intensity0;
uniform float BumpScale;
uniform int DisableViewDependentReflection;

in vec2 sUV;
in vec3 sWorldPos;
in vec3 sNormal;
in vec3 sTangent;
in vec3 sBinormal;

out vec4 FragColor;

// Sea blue color constants
const vec3 SEA_BLUE_SHALLOW = vec3(0.0, 0.5, 0.8);  // Light sea blue for shallow water
const vec3 SEA_BLUE_DEEP = vec3(0.0, 0.2, 0.5);     // Darker sea blue for deep water
const float WATER_CLARITY = 0.7;                     // How much the diffuse texture affects color

vec3 sampleNormal(vec2 uv) {
    vec4 bumpSample = texture(BumpMap0, uv);
    vec3 n;
    n.xy = bumpSample.ag * 2.0 - 1.0;
    n.xy *= BumpScale;
    n.z = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));
    return n;
}

void main() {
    // DEBUG: Simple test to see if water renders at all
    // Uncomment the line below to test with solid color
    // FragColor = vec4(0.0, 0.6, 1.0, 0.7); return;

    vec4 diff = texture(DiffMap0, sUV);
    vec3 emsv = texture(EmsvMap0, sUV).rgb * MatEmissiveIntensity;

    // Calculate tangent-space normal mapping
    vec3 T = normalize(sTangent);
    vec3 B = normalize(sBinormal);
    vec3 N = normalize(sNormal);
    vec3 tNormal = sampleNormal(sUV);
    vec3 worldNormal = normalize(mat3(T, B, N) * tNormal);

    // View and reflection vectors
    vec3 reflectionEyePos = eyePos;
    reflectionEyePos.y = min(reflectionEyePos.y, 1.0);
    vec3 worldViewVec = normalize(sWorldPos - reflectionEyePos);
    vec3 envColor = vec3(0.0);
    if (DisableViewDependentReflection == 0) {
        vec3 reflectVec = reflect(worldViewVec, worldNormal);
        envColor = texture(CubeMap0, reflectVec).rgb;
    }

    // Enhanced Fresnel effect for water
    float ndv = max(dot(-worldViewVec, worldNormal), 0.0);
    float fresnel = pow(1.0 - ndv, 3.0);  // Changed from 5.0 to 3.0 for more subtle reflection

    // Calculate water depth factor (using view angle as approximation)
    float depthFactor = 1.0 - ndv;

    // Blend between shallow and deep sea blue based on depth
    vec3 seaBlue = mix(SEA_BLUE_SHALLOW, SEA_BLUE_DEEP, depthFactor);

    // Mix diffuse texture with sea blue color
    vec3 waterBase = mix(seaBlue, diff.rgb, WATER_CLARITY * diff.a);

    // Add emissive component
    vec3 baseColor = waterBase + emsv;

    // Apply reflection with fresnel
    float reflStrength = (DisableViewDependentReflection == 0)
        ? clamp(MatSpecularIntensity * fresnel, 0.0, 1.0)
        : 0.0;
    vec3 color = mix(baseColor, envColor, reflStrength * 0.6);  // Reduced reflection strength more

    // Enhanced alpha for better transparency
    float alpha = clamp(diff.a * Intensity0 * (0.6 + depthFactor * 0.4), 0.4, 0.85);

    FragColor = vec4(color, alpha);
}
