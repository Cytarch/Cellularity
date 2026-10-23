#version 450

layout(set = 0, binding = 2) uniform sampler2D besselTexture;
layout(location = 0) in vec4 p;
layout(location = 1) in vec4 col;
layout(location = 0) out vec4 fragmentColor;

const float RATIO = 0.4;

float rho(float radiusSquared, float scaledTime) {
    return texture(besselTexture, vec2(radiusSquared * 0.22596, scaledTime * 0.285212)).r;
}

void main() {
    float distanceSquared = dot(p.xy, p.xy);
    float scaledTime = p.z * 0.105185;
    float inverseRadius = 1.0 / col.a;
    fragmentColor.rgb = (RATIO * 0.5 + (1.0 - RATIO)) * col.rgb;
    fragmentColor.a = (RATIO + 0.5 * (1.0 - RATIO)) * rho(distanceSquared * inverseRadius * inverseRadius, scaledTime * inverseRadius);
}
