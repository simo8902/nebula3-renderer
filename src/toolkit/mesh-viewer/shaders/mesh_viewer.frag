#version 460 core

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vUV;

uniform sampler2D diffuseTex;
uniform vec3 cameraPos;
uniform vec3 baseColor;
uniform int showUvGrid;
uniform float uvGridScale;
uniform float uvGridThickness;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(vec3(0.35, 0.8, 0.45));
    vec3 V = normalize(cameraPos - vWorldPos);
    vec3 H = normalize(L + V);

    float lambert = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.15;

    vec3 tex = texture(diffuseTex, vUV).rgb;
    vec3 albedo = tex * baseColor;

    if (showUvGrid > 0) {
        vec2 cell = abs(fract(vUV * uvGridScale) - 0.5);
        float line = 1.0 - smoothstep(0.5 - uvGridThickness, 0.5, max(cell.x, cell.y));
        vec3 gridColor = vec3(1.0, 0.18, 0.18);
        albedo = mix(albedo, gridColor, line);
    }

    vec3 color = albedo * (0.25 + lambert * 0.75) + vec3(spec);
    FragColor = vec4(color, 1.0);
}
