#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec4 c;
layout(location = 0) out vec4 fragmentColor;

void main() {
    if (dot(p, p) > 1.01) {
        discard;
    }
    fragmentColor = c;
}
