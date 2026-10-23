precision highp float;

varying vec2 cellPosition;
varying vec2 bubbleDirection;
varying vec2 bubbleTextureDirection;
varying float wallDistance;
varying vec4 cellColor;
varying float cellRadius;

uniform sampler2D tex1;

const float SCALE = 50.0;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const float BUBBLE_FOCUS_OFFSET = -0.6;
const float BUBBLE_DISTORTION_AMOUNT = 0.1;
const float BUBBLE_NUCLEUS_OFFSET = 0.55;
const float BUBBLE_OUTER_LIMIT = 0.9;
const float BUBBLE_INNER_LIMIT_GRADIENT_SCALE = 0.08;
const float BUBBLE_TEXTURE_OFFSET = 0.3;
const float BUBBLE_TEXTURE_ALPHA = 0.7;
const float BUBBLE_TEXTURE_THRESHOLD = 0.2;
const float CYTOPLASM_ALPHA = 0.4;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;
const float BUBBLE_CELL_COLOR_FACTOR = 0.75;

const float NUCLEUS_RADIUS_SQ = NUCLEUS_RADIUS * NUCLEUS_RADIUS;
const float MIN_DIST_SQ = 0.000001;

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
    bool isInsideInterior = interiorShapeRatio < 1.0;
    bool isInsideBubbleArea = isInsideInterior;

    if (isInsideBubbleArea) {
        vec2 normalizedPatternPosition = cellPosition * max(wallDistance / sqrt(max(distSq, MIN_DIST_SQ)), 1.0) / cellRadius;
        vec2 bubbleCrossDirection = vec2(bubbleDirection.y, -bubbleDirection.x);

        float bubbleLocalX = dot(normalizedPatternPosition, bubbleDirection);
        float bubbleLocalY = dot(normalizedPatternPosition, bubbleCrossDirection);

        vec2 shiftedNucleusPosition = normalizedPatternPosition - bubbleDirection * BUBBLE_NUCLEUS_OFFSET;
        bool isOutsideShiftedNucleus = dot(shiftedNucleusPosition, shiftedNucleusPosition)
                * radiusSq > NUCLEUS_RADIUS_SQ;

        if (isOutsideShiftedNucleus) {
            vec2 focusPosition = normalizedPatternPosition + bubbleDirection * BUBBLE_FOCUS_OFFSET;
            float bubbleFocusDistSq = dot(focusPosition, focusPosition);
            // Distort the local radius around an off-center focus to form the buoyancy bubble.
            float bubbleShapeValue = length(normalizedPatternPosition)
                    + BUBBLE_DISTORTION_AMOUNT / bubbleFocusDistSq;
            bool isInsideBubbleOuterShape = bubbleShapeValue < BUBBLE_OUTER_LIMIT;

            if (isInsideBubbleOuterShape) {
                float localRadiusSq = bubbleLocalX * bubbleLocalX + bubbleLocalY * bubbleLocalY;
                float bubbleFocusLocalDistSq = (BUBBLE_FOCUS_OFFSET + bubbleLocalX)
                        * (BUBBLE_FOCUS_OFFSET + bubbleLocalX)
                        + bubbleLocalY * bubbleLocalY;
                float bubbleGradientX = bubbleLocalX / sqrt(localRadiusSq)
                        - (2.0 * BUBBLE_DISTORTION_AMOUNT * (BUBBLE_FOCUS_OFFSET + bubbleLocalX))
                        / (bubbleFocusLocalDistSq * bubbleFocusLocalDistSq);
                float bubbleGradientY = bubbleLocalY * (1.0 / sqrt(localRadiusSq)
                        - 2.0 * BUBBLE_DISTORTION_AMOUNT
                        / (bubbleFocusLocalDistSq * bubbleFocusLocalDistSq));
                float bubbleEdgeGradient = sqrt(bubbleGradientX * bubbleGradientX + bubbleGradientY * bubbleGradientY);
                float bubbleInnerLimit = BUBBLE_OUTER_LIMIT - BUBBLE_INNER_LIMIT_GRADIENT_SCALE * bubbleEdgeGradient;
                bool isInsideBubbleCore = bubbleShapeValue < bubbleInnerLimit;

                if (isInsideBubbleCore) {
                    vec2 bubbleTextureBaseCoord = cellPosition / cellRadius
                            + bubbleDirection * BUBBLE_TEXTURE_OFFSET;
                    vec2 bubbleTextureCoord = vec2(
                            bubbleTextureBaseCoord.x * bubbleTextureDirection.x
                                    + bubbleTextureBaseCoord.y * bubbleTextureDirection.y,
                            bubbleTextureBaseCoord.x * bubbleTextureDirection.y
                                    - bubbleTextureBaseCoord.y * bubbleTextureDirection.x);
                    float bubbleShapeValueSq = bubbleShapeValue * bubbleShapeValue;
                    float bubbleTextureWarpFactor = bubbleShapeValueSq / (bubbleInnerLimit * bubbleInnerLimit);
                    float bubbleTextureWarpScale = (1.0 - bubbleTextureWarpFactor)
                            + bubbleTextureWarpFactor * bubbleShapeValue
                            / (bubbleInnerLimit * length(bubbleTextureCoord));
                    bool isInsideBubbleTexture = texture2D(
                            tex1,
                            vec2(0.5, 0.5) + 0.5 * bubbleTextureCoord * bubbleTextureWarpScale).b
                            > BUBBLE_TEXTURE_THRESHOLD;
                    float bubbleTextureAlpha = isInsideBubbleTexture ? BUBBLE_TEXTURE_ALPHA : 0.0;

                    gl_FragColor.rgb = cellColor.rgb * BUBBLE_CELL_COLOR_FACTOR;
                    gl_FragColor.a = bubbleTextureAlpha;
                } else {
                    gl_FragColor.rgb = SHADED_CELL_FACTOR * cellColor.rgb;
                    gl_FragColor.a = SOLID_ALPHA;
                }
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
