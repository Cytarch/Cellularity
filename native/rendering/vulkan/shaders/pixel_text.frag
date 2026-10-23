#version 450

layout(set = 0, binding = 3) uniform sampler2D glyphAtlas;
layout(location = 0) in vec2 glyphUv;
layout(location = 1) in vec4 glyphColor;
layout(location = 2) flat in int solidPrimitive;
layout(location = 0) out vec4 outColor;

void main() {
    if (solidPrimitive == 0
            && texture(glyphAtlas, glyphUv).r < 0.5) {
        discard;
    }
    outColor = glyphColor;
}
