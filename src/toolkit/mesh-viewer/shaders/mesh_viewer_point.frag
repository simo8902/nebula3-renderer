#version 460 core

uniform vec3 pointColor;

out vec4 FragColor;

void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    if (dot(c, c) > 1.0) discard;
    FragColor = vec4(pointColor, 1.0);
}
