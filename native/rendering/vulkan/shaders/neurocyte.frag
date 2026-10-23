#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 1) in float signalOutputA;
layout(location = 2) in float signalOutputB;
layout(location = 3) in float signalPatternAngle;
layout(location = 4) in float wallDistance;
layout(location = 5) in vec4 cellColor;
layout(location = 6) in float cellRadius;

const float SCALE = 50.0;
const float PI = 3.1415926535897932384626433832795;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const float SIGNAL_SEGMENT_COUNT = 6.0;
const float SIGNAL_SEGMENT_FACTOR = SIGNAL_SEGMENT_COUNT * 0.5 / PI;
const float SIGNAL_TRACK_OUTER_RADIUS = 0.4;
const float SIGNAL_TRACK_CORE_INSET_SCALE = 0.08 * 0.7;
const vec3 SIGNAL_A_POSITIVE_COLOR = vec3(0.83, 0.7, 0.0);
const vec3 SIGNAL_A_NEGATIVE_COLOR = vec3(0.83, 0.0, 0.7);
const vec3 SIGNAL_B_POSITIVE_COLOR = vec3(0.42, 0.7, 1.0);
const vec3 SIGNAL_B_NEGATIVE_COLOR = vec3(0.7, 1.0, 0.42);
const float CYTOPLASM_ALPHA = 0.5;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;
const float SIGNAL_TRACK_OUTER_RADIUS_SQ = SIGNAL_TRACK_OUTER_RADIUS * SIGNAL_TRACK_OUTER_RADIUS;

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

    if (isOutsideNucleus) {
        vec2 patternPosition = cellPosition * max(wallDistance / sqrt(distSq), 1.0);
        float patternRadius = length(patternPosition);
        // Map the cytoplasm into alternating radial tracks for the first two signal outputs.
        float radialTrackPosition = (patternRadius - NUCLEUS_RADIUS)
                / (cellRadius - NUCLEUS_RADIUS - EDGE_WIDTH) - 0.5;
        float angularTrackPosition = (atan(cellPosition.x, cellPosition.y) + signalPatternAngle)
                * SIGNAL_SEGMENT_FACTOR;

        float signalSegmentIndex = floor(angularTrackPosition);
        vec2 signalTrackPosition = vec2(
                radialTrackPosition,
                (angularTrackPosition - signalSegmentIndex - 0.5) * 2.0);

        float signalTrackDistSq = dot(signalTrackPosition, signalTrackPosition);
        bool isInsideSignalTrack = signalTrackDistSq < SIGNAL_TRACK_OUTER_RADIUS_SQ;

        if (isInsideSignalTrack) {
            float signalCoreRadius = max(SIGNAL_TRACK_OUTER_RADIUS - SIGNAL_TRACK_CORE_INSET_SCALE / patternRadius, 0.0);
            float signalCoreRadiusSq = signalCoreRadius * signalCoreRadius;
            bool isInsideSignalCore = signalTrackDistSq < signalCoreRadiusSq;
            if (isInsideSignalCore) {
                outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
                bool isEvenSignalSegment = mod(signalSegmentIndex, 2.0) == 0.0;
                float signalOutput;
                if (isEvenSignalSegment) {
                    signalOutput = signalOutputA;
                    if (signalOutput > 0.0) {
                        outColor.rgb = SIGNAL_A_POSITIVE_COLOR;
                    } else {
                        outColor.rgb = SIGNAL_A_NEGATIVE_COLOR;
                    }
                } else {
                    signalOutput = signalOutputB;
                    if (signalOutput > 0.0) {
                        outColor.rgb = SIGNAL_B_POSITIVE_COLOR;
                    } else {
                        outColor.rgb = SIGNAL_B_NEGATIVE_COLOR;
                    }
                }
                outColor.a = abs(signalOutput);
            } else {
                outColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
                outColor.a = SOLID_ALPHA;
            }
        } else {
            if (isInsideInterior) {
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
