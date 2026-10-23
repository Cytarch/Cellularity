#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 1) in vec2 gametePatternPosition;
layout(location = 2) in vec2 gameteSplitDirection;
layout(location = 3) in float wallDistance;
layout(location = 4) in vec4 cellColor;
layout(location = 5) in float cellRadius;

const float SCALE = 50.0;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH_SCALE = 0.5;
const float CYTOPLASM_ALPHA = 0.4;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;

float gameteEdgeWave(vec2 patternPosition) {
    float a = patternPosition.x;
    float b = patternPosition.y;
    float a2 = a * a;
    float a3 = a2 * a;
    float a5 = a3 * a2;
    float a7 = a5 * a2;
    float b2 = b * b;
    float b4 = b2 * b2;
    float b6 = b4 * b2;
    return a7 - 21.0 * a5 * b2 + 35.0 * a3 * b4 - 7.0 * a * b6;
}

void main() {
    float distSq = dot(cellPosition, cellPosition);
    float radiusSq = cellRadius * cellRadius;

    if (distSq > radiusSq) {
        discard;
    }

    float edgeWave = gameteEdgeWave(gametePatternPosition);
    float edgeWidth = EDGE_WIDTH * (1.1 + 5.0 * edgeWave * edgeWave);
    float innerRadius = cellRadius - edgeWidth;
    float innerRadiusSq = innerRadius * innerRadius;
    float radialRatio = distSq / innerRadiusSq;
    float wallRatio = edgeWidth / max(edgeWidth * EDGE_HALF_WIDTH_SCALE, cellRadius - wallDistance);
    float gameteShapeRatio = max(radialRatio, wallRatio);

    bool isOutsideNucleusOrOnOpenSide = distSq > NUCLEUS_RADIUS_SQ
            || 0.0 < dot(cellPosition, gameteSplitDirection);
    bool isCytoplasm = isOutsideNucleusOrOnOpenSide && gameteShapeRatio < 1.0;

    if (isCytoplasm) {
        outColor.rgb = cellColor.rgb;
        outColor.a = CYTOPLASM_ALPHA;
    } else {
        outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
        outColor.a = SOLID_ALPHA;
    }
}
