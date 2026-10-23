#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec4 aRect;
layout(location = 1) in float aGlyph;
layout(location = 2) in vec4 aColor;
layout(location = 0) out vec2 glyphUv;
layout(location = 1) out vec4 glyphColor;
layout(location = 2) flat out int solidPrimitive;

const vec2 QUAD_VERTICES[6] = vec2[](
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
    vec2 corner = QUAD_VERTICES[gl_VertexIndex];
    float glyphColumn = mod(aGlyph, ATLAS_COLUMNS);
    float glyphRow = floor(aGlyph / ATLAS_COLUMNS);
    vec2 atlasPixel = vec2(glyphColumn * GLYPH_SIZE.x,
                           glyphRow * GLYPH_SIZE.y) + corner * GLYPH_SIZE;

    glyphUv = atlasPixel / ATLAS_SIZE;
    glyphColor = aColor;
    solidPrimitive = aGlyph >= 42.5 ? 1 : 0;
    vec4 clipPosition = vec4(aRect.xy + corner * aRect.zw, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
