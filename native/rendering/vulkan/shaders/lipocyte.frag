#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 1) in float lipidFill;
layout(location = 2) in float wallDistance;
layout(location = 3) in vec4 cellColor;
layout(location = 4) in float cellRadius;

const float SCALE = 50.0;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const vec3 LIPID_COLOR = vec3(0.55, 0.4, 0.3);
const float LIPID_COLOR_BLEND = 0.7;
const float LIPID_CELL_COLOR_BLEND = 0.3;
const float LIPID_STORAGE_BASE_THRESHOLD = 0.3;
const float LIPID_STORAGE_FILL_SCALE = 0.7;
const float CYTOPLASM_ALPHA = 0.5;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;

void main() {
    float distSq = dot(cellPosition, cellPosition);
    float radiusSq = cellRadius * cellRadius;

    if (distSq > radiusSq) {
        discard;
    }

    float innerRadius = cellRadius - EDGE_WIDTH;
    float innerRadiusSq = innerRadius * innerRadius;
    bool isCytoplasm = distSq > NUCLEUS_RADIUS_SQ
            && distSq < innerRadiusSq
            && wallDistance < innerRadius;

    float radialRatio = distSq / innerRadiusSq;
    float wallRatio = EDGE_WIDTH / max(EDGE_HALF_WIDTH, cellRadius - wallDistance);
    float lipidStorageShapeRatio = max(radialRatio, wallRatio);
    float lipidStorageThreshold = LIPID_STORAGE_BASE_THRESHOLD + LIPID_STORAGE_FILL_SCALE * lipidFill;

    if (isCytoplasm) {
        outColor.rgb = cellColor.rgb;
        outColor.a = CYTOPLASM_ALPHA;
        if (lipidStorageShapeRatio < lipidStorageThreshold) {
            outColor.rgb = LIPID_COLOR * LIPID_COLOR_BLEND + cellColor.rgb * LIPID_CELL_COLOR_BLEND;
            outColor.a = SOLID_ALPHA;
        }
    } else {
        outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
        outColor.a = SOLID_ALPHA;
    }
}
