#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 1) in float vesicleAnimationTime;
layout(location = 2) in float wallDistance;
layout(location = 3) in float vesicleGridAngle;
layout(location = 4) in vec4 cellColor;
layout(location = 5) in float cellRadius;

const float SCALE = 50.0;
const float PI = 3.1415926535897932384626433832795;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const float VESICLE_GRID_ROW_HEIGHT = 2.0 * sin(PI / 3.0);
const float VESICLE_GRID_FREQUENCY = 9.0 * VESICLE_GRID_ROW_HEIGHT * 0.5 / PI;
const float VESICLE_GRID_SCROLL_SPEED = 0.2;
const float VESICLE_OUTER_RADIUS = 0.5 * 0.4;
const float VESICLE_CORE_SCALE = 0.08 * 0.7;
const float CYTOPLASM_ALPHA = 0.5;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;
const float VESICLE_OUTER_RADIUS_SQ = VESICLE_OUTER_RADIUS * VESICLE_OUTER_RADIUS;

void main() {
    float distSq = dot(cellPosition, cellPosition);
    float radiusSq = cellRadius * cellRadius;

    if (distSq > radiusSq) {
        discard;
    }

    float innerRadius = cellRadius - EDGE_WIDTH;
    float innerRadiusSq = innerRadius * innerRadius;
    float radialRatio = distSq / innerRadiusSq;
    float wallRatio = EDGE_WIDTH / max(EDGE_HALF_WIDTH, cellRadius - wallDistance);
    float interiorShapeRatio = max(radialRatio, wallRatio);
    bool isOutsideNucleus = distSq > NUCLEUS_RADIUS_SQ;

    if (isOutsideNucleus) {
        vec2 patternPosition = cellPosition * max(wallDistance / sqrt(distSq), 1.0);
        float patternRadius = sqrt(dot(patternPosition, patternPosition));

        if (patternRadius > cellRadius) {
            discard;
        }

        // Map local coordinates into a scrolling log-polar vesicle grid.
        float radialGridPosition = log(patternRadius / cellRadius) * VESICLE_GRID_FREQUENCY
                - VESICLE_GRID_SCROLL_SPEED * vesicleAnimationTime;
        float angularGridPosition = (atan(patternPosition.x, patternPosition.y) + vesicleGridAngle)
                / VESICLE_GRID_ROW_HEIGHT * VESICLE_GRID_FREQUENCY;

        vec2 primaryVesicleCell = vec2(fract(radialGridPosition), fract(angularGridPosition) * VESICLE_GRID_ROW_HEIGHT)
                - vec2(0.5, 0.5 * VESICLE_GRID_ROW_HEIGHT);
        vec2 offsetVesicleCell = vec2(fract(radialGridPosition - 0.5), fract(angularGridPosition - 0.5) * VESICLE_GRID_ROW_HEIGHT)
                - vec2(0.5, 0.5 * VESICLE_GRID_ROW_HEIGHT);

        float nearestVesicleDistSq = min(dot(primaryVesicleCell, primaryVesicleCell), dot(offsetVesicleCell, offsetVesicleCell));
        bool isInsideVesicle = nearestVesicleDistSq < VESICLE_OUTER_RADIUS_SQ;

        if (isInsideVesicle) {
            outColor.rgb = cellColor.rgb * SHADED_CELL_FACTOR;
            float vesicleCoreRadius = max(VESICLE_OUTER_RADIUS - VESICLE_CORE_SCALE / patternRadius, 0.0);
            bool isInsideVesicleCore = nearestVesicleDistSq < vesicleCoreRadius * vesicleCoreRadius;
            if (isInsideVesicleCore) {
                outColor.a = 0.0;
            } else {
                outColor.a = SOLID_ALPHA;
            }
        } else {
            if (interiorShapeRatio < 1.0) {
                outColor.rgb = cellColor.rgb;
                outColor.a = CYTOPLASM_ALPHA;
            } else {
                outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
                outColor.a = SOLID_ALPHA;
            }
        }
    } else {
        outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
        outColor.a = SOLID_ALPHA;
    }
}
