precision highp float;

varying vec2 cellPosition;
varying float wallDistance;
varying float path1MarkerRadius;
varying float path2MarkerRadius;
varying float pathMarkerAngle;
varying vec4 cellColor;
varying float cellRadius;

const float SCALE = 50.0;
const float PI = 3.1415926535897932384626433832795;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const vec3 PATH1_MARKER_COLOR_BIAS = vec3(0.3, 0.0, 0.0);
const vec3 PATH2_MARKER_COLOR_BIAS = vec3(0.0, 0.0, 0.3);
const float PATH_MARKER_COLOR_BLEND = 0.75;
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
    float radialRatio = distSq / innerRadiusSq;
    float wallRatio = EDGE_WIDTH / max(EDGE_HALF_WIDTH, cellRadius - wallDistance);
    float interiorShapeRatio = max(radialRatio, wallRatio);
    bool isOutsideNucleus = distSq > NUCLEUS_RADIUS_SQ;
    bool isInsideInterior = interiorShapeRatio < 1.0;
    bool isInsidePathMarkerArea = isOutsideNucleus && isInsideInterior;

    if (isOutsideNucleus) {
        vec2 patternPosition = cellPosition * max(wallDistance / sqrt(distSq), 1.0);
        float patternRadius = sqrt(dot(patternPosition, patternPosition));

        if (patternRadius > cellRadius) {
            discard;
        }

        // Map local coordinates around the nucleus into the two differentiation path markers.
        float radialMarkerPosition = log((cellRadius - NUCLEUS_RADIUS) / (patternRadius - NUCLEUS_RADIUS));
        float angularMarkerPosition = atan(patternPosition.x, patternPosition.y) + pathMarkerAngle;

        if (angularMarkerPosition > PI) {
            angularMarkerPosition -= 2.0 * PI;
        }

        if (angularMarkerPosition < -PI) {
            angularMarkerPosition += 2.0 * PI;
        }

        vec2 path2MarkerPosition = vec2(radialMarkerPosition - path2MarkerRadius, angularMarkerPosition);
        float oppositeAngularMarkerPosition;
        if (angularMarkerPosition > 0.0) {
            oppositeAngularMarkerPosition = angularMarkerPosition - PI;
        } else {
            oppositeAngularMarkerPosition = angularMarkerPosition + PI;
        }

        vec2 path1MarkerPosition = vec2(radialMarkerPosition - path1MarkerRadius, oppositeAngularMarkerPosition);

        if (isInsidePathMarkerArea) {
            float path1MarkerRadiusSq = path1MarkerRadius * path1MarkerRadius;
            float path2MarkerRadiusSq = path2MarkerRadius * path2MarkerRadius;
            bool isInsidePath1Marker = dot(path1MarkerPosition, path1MarkerPosition)
                    / path1MarkerRadiusSq < 1.0;
            bool isInsidePath2Marker = dot(path2MarkerPosition, path2MarkerPosition)
                    / path2MarkerRadiusSq < 1.0;

            if (isInsidePath1Marker) {
                gl_FragColor.rgb = cellColor.rgb * PATH_MARKER_COLOR_BLEND + PATH1_MARKER_COLOR_BIAS;
                gl_FragColor.a = SOLID_ALPHA;
            } else if (isInsidePath2Marker) {
                gl_FragColor.rgb = cellColor.rgb * PATH_MARKER_COLOR_BLEND + PATH2_MARKER_COLOR_BIAS;
                gl_FragColor.a = SOLID_ALPHA;
            } else {
                gl_FragColor.rgb = cellColor.rgb;
                gl_FragColor.a = CYTOPLASM_ALPHA;
            }
        } else {
            gl_FragColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
            gl_FragColor.a = SOLID_ALPHA;
        }
    } else {
        gl_FragColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
        gl_FragColor.a = SOLID_ALPHA;
    }
}
