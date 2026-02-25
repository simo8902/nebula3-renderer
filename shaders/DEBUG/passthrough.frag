#version 460 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D inputTex;

void main() {
    FragColor = vec4(texture(inputTex, TexCoord).rgb, 1.0);
}
