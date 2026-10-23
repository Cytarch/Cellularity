precision highp float;

varying vec2 cellPosition;
varying float wallDistance;
varying vec4 cellColor;
varying float cellRadius;

const float SCALE = 50.0;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
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

    if (isCytoplasm) {
        gl_FragColor.rgb = cellColor.rgb;
        gl_FragColor.a = CYTOPLASM_ALPHA;
    } else {
        gl_FragColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
        gl_FragColor.a = SOLID_ALPHA;
    }
}
