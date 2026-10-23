#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec2 texturePosition;
layout(location = 2) in vec4 col;
layout(location = 3) in float animationTime;
layout(location = 0) out vec4 fragmentColor;

const float SCALE = 50.0;
const float EDGE_WIDTH = 0.002 * SCALE;
const float END = 0.7;
const float RADIUS = 0.1;
const float TAIL_RADIUS = 4.0;

void main() {
    float distanceFromCenter = length(p);
    float alpha = distanceFromCenter < col.a - EDGE_WIDTH * 0.5 ? 0.0 : 1.0;
    float waveWidth = (TAIL_RADIUS - texturePosition.x) * 0.1 * (distanceFromCenter - col.a) / col.a;
    float middleY = sin(-animationTime + texturePosition.x * 4.0) * waveWidth;
    float curvature = 4.0 * cos(-animationTime + texturePosition.x * 4.0) * waveWidth;
    float width = (1.0 - texturePosition.x / (TAIL_RADIUS * END)) * 0.3 * (1.0 + 0.5 * curvature * curvature);
    if (distanceFromCenter - col.a < RADIUS * col.a && 1.0 > (1.0 - (distanceFromCenter - col.a) / col.a / RADIUS) * (1.0 - (distanceFromCenter - col.a) / col.a / RADIUS)) {
        width += RADIUS - RADIUS * sqrt(1.0 - (1.0 - (distanceFromCenter - col.a) / col.a / RADIUS) * (1.0 - (distanceFromCenter - col.a) / col.a / RADIUS));
    }
    if (abs(texturePosition.y - middleY) < width) {
        fragmentColor = vec4(col.rgb * 0.5, alpha);
    } else {
        fragmentColor = vec4(0.0);
    }
}
