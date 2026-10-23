#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 cellPosition;
layout(location = 1) in vec2 muscleStripeDirection;
layout(location = 2) in float visualContraction;
layout(location = 3) in float wallDistance;
layout(location = 4) in float visualBending;
layout(location = 5) in vec4 cellColor;
layout(location = 6) in float cellRadius;

const float SCALE = 50.0;
const float PI = 3.1415926535897932384626433832795;

const float NUCLEUS_RADIUS = 0.005 * SCALE;
const float EDGE_WIDTH = 0.002 * SCALE;
const float EDGE_HALF_WIDTH = 0.001 * SCALE;
const float MUSCLE_STRIPE_COUNT = 4.0;
const float MUSCLE_STRIPE_FACTOR = (MUSCLE_STRIPE_COUNT + 0.2) * 2.0 / PI;
const float BENDING_VISUAL_SCALE = 0.7;
const float CONTRACTION_VISUAL_SCALE = 0.4;
const float CYTOPLASM_ALPHA = 0.5;
const float SOLID_ALPHA = 1.0;
const float SHADED_CELL_FACTOR = 0.5;
const float MUSCLE_STRIPE_SHADE_FACTOR = 0.75;
const float MUSCLE_STRIPE_MIN_PHASE = 0.3;
const float MUSCLE_STRIPE_MAX_PHASE = 0.7;
const float MUSCLE_STRIPE_WAVE_WIDTH = 0.1;

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
    float muscleShapeRatio = max(radialRatio, wallRatio);

    bool isCytoplasm = distSq > NUCLEUS_RADIUS_SQ
            && muscleShapeRatio < 1.0;

    if (isCytoplasm) {
        vec2 patternPosition = cellPosition * max(wallDistance / sqrt(distSq), 1.0);
        vec2 muscleCrossDirection = vec2(muscleStripeDirection.y, -muscleStripeDirection.x);

        float stripeX = dot(patternPosition, muscleStripeDirection);
        float stripeY = dot(patternPosition, muscleCrossDirection);
        float visualBend = visualBending * BENDING_VISUAL_SCALE;
        float stripeScale = (1.0 - CONTRACTION_VISUAL_SCALE * visualContraction)
                / sqrt(1.0 - visualBend * visualBend);
        float normalizedStripeX = stripeX / innerRadius * stripeScale;
        float normalizedStripeY = (stripeY / innerRadius + visualBend) * stripeScale;

        // Map local muscle coordinates through a bent stripe field.
        float stripePhaseCoord = (atan(
                2.0 * normalizedStripeY,
                1.0 - normalizedStripeX * normalizedStripeX - normalizedStripeY * normalizedStripeY)
                - 2.0 * visualBend * stripeScale)
                / (1.0 - 1.5 * visualBend * visualBend);
        float stripeWidthCoord = -0.5 * log(
                ((normalizedStripeX - 1.0) * (normalizedStripeX - 1.0)
                        + normalizedStripeY * normalizedStripeY)
                / ((normalizedStripeX + 1.0) * (normalizedStripeX + 1.0)
                        + normalizedStripeY * normalizedStripeY));
        float stripePhase = fract(clamp(stripePhaseCoord * MUSCLE_STRIPE_FACTOR, -4.0, 4.0));
        float stripeWaveWidth = abs(sin(2.0 * stripeWidthCoord * MUSCLE_STRIPE_FACTOR))
                * MUSCLE_STRIPE_WAVE_WIDTH;

        bool isMuscleStripe = stripePhase > MUSCLE_STRIPE_MIN_PHASE - stripeWaveWidth
                && stripePhase < MUSCLE_STRIPE_MAX_PHASE + stripeWaveWidth;
        if (isMuscleStripe) {
            outColor.rgb = MUSCLE_STRIPE_SHADE_FACTOR * cellColor.rgb;
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
