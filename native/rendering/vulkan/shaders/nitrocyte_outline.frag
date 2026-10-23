#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec2 texturePosition;
layout(location = 2) in vec4 col;
layout(location = 0) out vec4 fragmentColor;

const float PI = 3.14159265358979323846;
const float SCALE = 50.0;
const float Y_SCALE = 20.0 / SCALE;
const float ARM_COUNT = 5.0;
const float COUNT_OVER_CIRCLE = ARM_COUNT / (2.0 * PI);
const int MAX_ITERATIONS = 9;

float giveCx(float x, float y) {
    return (y * y * y * y - 4.0 * y * y * y + (2.0 * x * x + 2.0) * y * y + (4.0 - 4.0 * x * x) * y + x * x * x * x + 6.0 * x * x - 3.0) / (4.0 * y * y * y * y - 16.0 * y * y * y + (8.0 * x * x + 24.0) * y * y + (-16.0 * x * x - 16.0) * y + 4.0 * x * x * x * x + 8.0 * x * x + 4.0);
}

float giveCy(float x, float y) {
    return -(2.0 * x * y - 2.0 * x) / (y * y * y * y - 4.0 * y * y * y + (2.0 * x * x + 6.0) * y * y + (-4.0 * x * x - 4.0) * y + x * x * x * x + 2.0 * x * x + 1.0);
}

void main() {
    float distanceFromEdge = length(p) - col.a;
    if (distanceFromEdge < -0.05 * col.a) {
        discard;
    }
    float phase = 2.0 * fract(atan(texturePosition.y, texturePosition.x) * COUNT_OVER_CIRCLE);
    if (phase > 1.0) {
        phase = 2.0 - phase;
    }
    vec2 c = vec2(giveCx(1.563 + phase / 3.0, distanceFromEdge * Y_SCALE), giveCy(1.563 + phase / 3.0, distanceFromEdge * Y_SCALE));
    vec2 z = c;
    for (int iteration = 0; iteration < MAX_ITERATIONS; ++iteration) {
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        if (dot(z, z) > 4.0) {
            z = vec2(2.0);
        }
    }
    fragmentColor = vec4(col.rgb * 0.5, dot(z, z) > 4.0 ? 0.0 : 1.0);
}
