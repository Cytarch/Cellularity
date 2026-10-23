#version 300 es

precision mediump float;

uniform sampler2D uGlyphAtlas;

in highp vec2 glyphUv;
in highp vec4 glyphColor;
flat in highp int solidPrimitive;

out vec4 fragmentColor;

void main() {
    if (solidPrimitive == 0) {
        if (texture(uGlyphAtlas, glyphUv).r < 0.5) {
            discard;
        }
    }
    fragmentColor = glyphColor;
}
