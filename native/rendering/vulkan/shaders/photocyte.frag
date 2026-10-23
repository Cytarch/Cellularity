#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 1) in vec2 chloroplastDirectionA;
layout(location = 2) in vec2 chloroplastDirectionB;
layout(location = 3) in float wallDistance;
layout(location = 4) in vec4 cellColor;
layout(location = 5) in float cellRadius;

layout(set = 0, binding = 1) uniform sampler2D chloroplastTexture;

const float SCALE = 50.0;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const vec3 CHLOROPLAST_COLOR = vec3(0.0, 0.6, 0.1);
const float CYTOPLASM_ALPHA = 0.4;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;

vec2 rotateByDirection(vec2 coord, vec2 direction) {
    return vec2(
            coord.x * direction.x - coord.y * direction.y,
            coord.x * direction.y + coord.y * direction.x);
}

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
    float chloroplastTextureWarp = max(radialRatio, wallRatio);

    vec2 chloroplastPatternCoord = 0.5 * cellPosition.xy / cellRadius * sqrt(chloroplastTextureWarp);
    float chloroplastMask =
            texture(chloroplastTexture, vec2(0.5, 0.5)
                    + rotateByDirection(chloroplastPatternCoord, chloroplastDirectionA)).r
            + texture(chloroplastTexture, vec2(0.5, 0.5)
                    + rotateByDirection(chloroplastPatternCoord, chloroplastDirectionB)).g;
    if (isCytoplasm) {
        if (chloroplastMask > 0.5) {
            outColor.rgb = CHLOROPLAST_COLOR;
            outColor.a = SOLID_ALPHA;
        } else {
            outColor.rgb = cellColor.rgb;
            outColor.a = CYTOPLASM_ALPHA;
        }
    } else {
        outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
        outColor.a = SOLID_ALPHA;
    }
}
