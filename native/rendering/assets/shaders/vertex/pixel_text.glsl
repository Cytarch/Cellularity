#version 300 es

precision highp float;

uniform mat4 uMVPMatrix;

in vec4 aRect;
in float aGlyph;
in vec4 aColor;

out highp vec2 glyphUv;
out highp vec4 glyphColor;
flat out highp int solidPrimitive;

const vec2 QUAD_VERTICES[6] = vec2[6](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0)
);

const vec2 GLYPH_SIZE = vec2(5.0, 7.0);
const vec2 ATLAS_SIZE = vec2(40.0, 42.0);
const float ATLAS_COLUMNS = 8.0;

void main() {
    vec2 corner = QUAD_VERTICES[gl_VertexID];
    float glyphColumn = mod(aGlyph, ATLAS_COLUMNS);
    float glyphRow = floor(aGlyph / ATLAS_COLUMNS);
    vec2 atlasPixel = vec2(glyphColumn * GLYPH_SIZE.x,
                           glyphRow * GLYPH_SIZE.y) + corner * GLYPH_SIZE;

    glyphUv = atlasPixel / ATLAS_SIZE;
    glyphColor = aColor;
    solidPrimitive = aGlyph >= 42.5 ? 1 : 0;
    gl_Position = uMVPMatrix * vec4(aRect.xy + corner * aRect.zw, 0.0, 1.0);
}
