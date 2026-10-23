precision highp float;

varying vec2 cellPosition;
varying float wallDistance;
varying vec4 cellColor;
varying float cellRadius;

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
        gl_FragColor.rgb = cellColor.rgb;
        gl_FragColor.a = 0.5;
    } else {
        // Nucleus, membrane, internal walls, and keratin reinforcement band.
        gl_FragColor.rgb = cellColor.rgb * 0.5;
        gl_FragColor.a = 1.0;
    }
}
