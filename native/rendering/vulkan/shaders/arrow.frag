#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec4 col;
layout(location = 2) in vec4 dirs;
layout(location = 3) in float ratioShift;
layout(location = 0) out vec4 fragmentColor;

const float SCALE = 50.0;
const float ARROW_HALF_WIDTH = 0.0015 * SCALE;
const float SPLIT_HALF_WIDTH = 0.001 * SCALE;
const float HEAD_HEIGHT = 0.008 * SCALE;
const float HEAD_WIDTH = 3.0 * ARROW_HALF_WIDTH;

void main() {
    if (dot(p, p) > col.a * col.a) {
        discard;
    }
    float splitRadius = p.x * dirs.w - p.y * dirs.z;
    float directionRadius = dot(p, dirs.xy);
    float directionDistance = abs(p.x * dirs.y - p.y * dirs.x);
    float splitDistance = abs(dot(p, dirs.zw) - ratioShift);
    float alpha = 1.0;
    if ((directionDistance > ARROW_HALF_WIDTH || directionRadius < 0.0 || col.a - directionRadius < HEAD_HEIGHT)
            && (directionDistance * HEAD_HEIGHT > (col.a - directionRadius) * HEAD_WIDTH || col.a - directionRadius > HEAD_HEIGHT)
            && (splitDistance > SPLIT_HALF_WIDTH || fract(0.25 + 0.2 * splitRadius / ARROW_HALF_WIDTH) > 0.5)) {
        alpha = 0.0;
    }
    vec3 color = col.r + col.g + col.b > 1.5 ? vec3(0.0) : vec3(1.0);
    fragmentColor = vec4(color, alpha);
}
