#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec2 texturePosition;
layout(location = 2) in vec4 col;
layout(location = 0) out vec4 fragmentColor;

const float PI = 3.14159265358979323846;
const float SCALE = 50.0;
const float SEGMENT_COUNT = 9.0;
const float COUNT_OVER_CIRCLE = SEGMENT_COUNT / (2.0 * PI);
const float MAX_RADIUS = 0.03 * SCALE;
const float FIRST_OVER_SECOND = 1.075;
const float SECOND_ANGLE = PI / (2.0 - 1.0 + 2.0 * FIRST_OVER_SECOND);
const float FIRST_ANGLE = SECOND_ANGLE * FIRST_OVER_SECOND;
const float SPIKE_LENGTH = MAX_RADIUS * 0.2;
const float FACTOR = SPIKE_LENGTH / (1.0 / (FIRST_ANGLE - SECOND_ANGLE) + 1.0 / (2.0 * FIRST_ANGLE - SECOND_ANGLE - (FIRST_ANGLE - SECOND_ANGLE)));

void main() {
    float distanceFromEdge = length(p) - col.a + 0.0001;
    if (distanceFromEdge < 0.0) {
        discard;
    }
    float phi = atan(texturePosition.y, texturePosition.x);
    float phaseFloor = floor(phi * COUNT_OVER_CIRCLE) / COUNT_OVER_CIRCLE;
    float phase = 2.0 * COUNT_OVER_CIRCLE * (phi - phaseFloor);
    float x = (1.0 - phase) * col.a * PI / SEGMENT_COUNT;
    float angle = atan(distanceFromEdge, x);
    if (angle > 0.5 * PI) {
        angle = PI - angle;
    }
    if (angle > FIRST_ANGLE) {
        angle = fract((angle - FIRST_ANGLE) / SECOND_ANGLE) * SECOND_ANGLE + FIRST_ANGLE - SECOND_ANGLE;
    }
    float maximumLength = (1.0 / angle + 1.0 / (2.0 * FIRST_ANGLE - SECOND_ANGLE - angle)) * FACTOR;
    fragmentColor = vec4(col.rgb * 0.5, x * x + distanceFromEdge * distanceFromEdge < maximumLength * maximumLength ? 1.0 : 0.0);
}
