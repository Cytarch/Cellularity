#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 2) in float wallDistance;
layout(location = 3) in vec4 cellColor;
layout(location = 1) in float cellRadius;

const float SCALE = 50.0;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;

void main() {
    float distSq = dot(cellPosition, cellPosition);
    float radiusSq = cellRadius * cellRadius;

    if (distSq > radiusSq) {
        discard;
    }

    float innerRadius = cellRadius - EDGE_WIDTH;
    float innerRadiusSq = innerRadius * innerRadius;

    float keratinBandOuterRadius = cellRadius - 2.0 * EDGE_WIDTH;
    float keratinBandInnerRadius = cellRadius - 3.0 * EDGE_WIDTH;
    float keratinBandOuterRadiusSq = keratinBandOuterRadius * keratinBandOuterRadius;
    float keratinBandInnerRadiusSq = keratinBandInnerRadius * keratinBandInnerRadius;

    bool isNotKeratinBand = distSq >= keratinBandOuterRadiusSq
            || wallDistance >= keratinBandOuterRadius
            || (distSq <= keratinBandInnerRadiusSq && wallDistance <= keratinBandInnerRadius);

    if (distSq > NUCLEUS_RADIUS_SQ
            && distSq < innerRadiusSq
            && wallDistance < innerRadius
            && isNotKeratinBand) {
        // Cytoplasm
        outColor.rgb = cellColor.rgb;
        outColor.a = 0.5;
    } else {
        // Nucleus, membrane, internal walls, and keratin reinforcement band.
        outColor.rgb = cellColor.rgb * 0.5;
        outColor.a = 1.0;
    }
}
