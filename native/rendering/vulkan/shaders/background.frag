#version 450

layout(location = 0) in vec2 vLocalPosition;
layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform BackgroundUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) vec4 lightDirectionRangeIntensity;
    layout(offset = 80) vec4 backgroundColorRadius;
    layout(offset = 96) mat2 preRotation;
    layout(offset = 128) vec4 geometry;
} background;

const vec3 WARM_COLOR = vec3(0.5, 0.35, 0.25);

void main() {
    float distanceSquared = dot(vLocalPosition, vLocalPosition);

    if (distanceSquared > 1.02 || (background.geometry.x > 0.0 && distanceSquared < background.geometry.x)) {
        discard;
    }

    if (distanceSquared > 0.999) {
        outColor = vec4(0.0);
        return;
    }

    vec4 direction = background.lightDirectionRangeIntensity;
    float lightRange = direction.z;
    float lightDistance = 1.0 +
        (1.0 - lightRange) * (1.0 - distanceSquared) / lightRange;
    float lightProjection = dot(direction.xy, vLocalPosition);
    float lightFactor =
        (lightProjection * (1.0 - lightRange) + lightRange) /
        (lightDistance * lightDistance);
    float illumination = direction.w * max(lightFactor, 0.0);

    vec3 finalColor = background.backgroundColorRadius.rgb +
        illumination * WARM_COLOR;
    outColor = vec4(finalColor, 1.0);
}
