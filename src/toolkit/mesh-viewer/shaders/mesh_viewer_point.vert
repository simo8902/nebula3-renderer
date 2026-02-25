#version 460 core

layout(location = 0) in vec3 position;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform float pointSize;

void main() {
    vec4 worldPos = model * vec4(position, 1.0);
    gl_Position = projection * view * worldPos;
    gl_PointSize = pointSize;
}
