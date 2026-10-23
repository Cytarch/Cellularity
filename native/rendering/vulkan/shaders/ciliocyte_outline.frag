#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec2 texturePosition;
layout(location = 2) in vec2 animationTimes;
layout(location = 3) in vec4 col;
layout(location = 0) out vec4 fragmentColor;

void main() {
    float distanceFromEdge = length(p) - col.a;
    if (distanceFromEdge < -0.01 * col.a) {
        discard;
    }
    vec2 normalizedPosition = normalize(texturePosition);
    float firstDistanceSquared = (normalizedPosition.x - 1.0) * (normalizedPosition.x - 1.0) + normalizedPosition.y * normalizedPosition.y;
    float secondDistanceSquared = (normalizedPosition.x + 1.0) * (normalizedPosition.x + 1.0) + normalizedPosition.y * normalizedPosition.y;
    float animationTime = normalizedPosition.y > 0.0 ? animationTimes.x : animationTimes.y;
    float width = abs(sin(log(firstDistanceSquared / secondDistanceSquared) * 6.66 + animationTime)) * 0.075 * sqrt(firstDistanceSquared * secondDistanceSquared) * col.a;
    fragmentColor = vec4(col.rgb * 0.5, distanceFromEdge < width ? 1.0 : 0.0);
}
