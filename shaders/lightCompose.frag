#version 460 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D sceneColorTex;
uniform float saturation;
uniform vec4 balance;
uniform float fadeValue;
uniform vec4 luminance;

void main() {
    vec4 c = texture(sceneColorTex, TexCoords);
    vec4 grey = vec4(vec3(dot(c.rgb, luminance.rgb)), c.a);
    vec4 color = balance * mix(grey, c, saturation);
    color.rgb *= fadeValue;
    FragColor = color;
}