#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec2 texturePosition;
layout(location = 2) in vec4 col;
layout(location = 3) in float rotation;
layout(location = 4) in vec2 movement;
layout(location = 0) out vec4 fragmentColor;

const float PI = 3.14159265358979323846;
const float SCALE = 50.0;
const float TOOTH_COUNT = 33.0;
const float COUNT_OVER_CIRCLE = TOOTH_COUNT / (2.0 * PI);
const float MAX_RADIUS = 0.03 * SCALE;
const float ROUNDING_RADIUS = MAX_RADIUS * 0.02;
const float HEIGHT = MAX_RADIUS * 0.4;
const float HALF_WIDTH = MAX_RADIUS * 0.1 / 4.0;
const float X = HALF_WIDTH + ROUNDING_RADIUS * 0.93945122136;
const float X1 = X - ROUNDING_RADIUS * 0.99805257848;

void main() {
    float distanceFromEdge = length(p) - col.a;
    if (distanceFromEdge < -0.01 * col.a) {
        discard;
    }
    float side = (rotation + dot(movement, vec2(-p.y, p.x)) / col.a) * distanceFromEdge / col.a * 30.0;
    distanceFromEdge = sqrt(distanceFromEdge * distanceFromEdge + side * side * col.a * col.a);
    side = (rotation + dot(movement, vec2(-p.y, p.x)) / col.a) * distanceFromEdge / col.a * 30.0;
    distanceFromEdge = sqrt(distanceFromEdge * distanceFromEdge + side * side * col.a * col.a);
    float phase = 2.0 * fract(COUNT_OVER_CIRCLE * (atan(texturePosition.y, texturePosition.x) - side));
    if (phase > 1.0) {
        phase = 2.0 - phase;
    }
    float x = (1.0 - phase) * col.a * PI / TOOTH_COUNT;
    float height = x < X1 || x > X ? HEIGHT * (1.0 - x / HALF_WIDTH) : ROUNDING_RADIUS - sqrt(ROUNDING_RADIUS * ROUNDING_RADIUS - (x - X) * (x - X));
    fragmentColor = vec4(col.rgb * 0.5, distanceFromEdge < height ? 1.0 : 0.0);
}
