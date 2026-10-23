#version 450

layout(location = 0) in vec2 vLocalPosition;
layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform EnvironmentFieldUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) vec4 field;
    layout(offset = 80) vec4 fieldExtra;
    layout(offset = 96) vec4 preview;
    layout(offset = 112) mat2 preRotation;
} environmentField;

const int FIELD_CONSTANT = 0;
const int FIELD_LINEAR_DIRECTIONAL = 1;
const int FIELD_RADIAL = 2;
const int VECTOR_FIELD_LINEAR_DIRECTIONAL = 0;
const int VECTOR_FIELD_RADIAL_INWARD = 1;
const int VECTOR_FIELD_RADIAL_OUTWARD = 2;
const vec3 LOW_COLOR = vec3(0.18, 0.42, 0.78);
const vec3 MID_COLOR = vec3(0.55, 0.72, 0.68);
const vec3 HIGH_COLOR = vec3(0.96, 0.64, 0.25);
const vec3 ARROW_COLOR = vec3(0.41, 0.80, 0.29);
const vec3 MARKER_COLOR = vec3(0.407843, 0.8, 0.294118);
const vec3 MARKER_BACKING_COLOR = vec3(0.12549);

float cross2d(vec2 left, vec2 right) {
    return left.x * right.y - left.y * right.x;
}

bool pointInTriangle(vec2 point, vec2 a, vec2 b, vec2 c) {
    float first = cross2d(b - a, point - a);
    float second = cross2d(c - b, point - b);
    float third = cross2d(a - c, point - c);
    bool hasNegative = first < 0.0 || second < 0.0 || third < 0.0;
    bool hasPositive = first > 0.0 || second > 0.0 || third > 0.0;
    return !(hasNegative && hasPositive);
}

bool pointInThickSegment(vec2 point, vec2 start, vec2 end, float width) {
    vec2 segment = end - start;
    float segmentLength = length(segment);
    if (segmentLength <= 0.000001) {
        return false;
    }
    vec2 direction = segment / segmentLength;
    vec2 relative = point - start;
    float along = dot(relative, direction);
    float across = abs(cross2d(direction, relative));
    return along >= 0.0 && along <= segmentLength && across <= width * 0.5;
}

bool pointInArrowHead(vec2 point, vec2 center, vec2 direction, float headLength, float headWidth) {
    vec2 normal = vec2(-direction.y, direction.x);
    vec2 tip = center + direction * headLength;
    vec2 base = center - direction * headLength * 0.45;
    return pointInTriangle(
        point,
        tip,
        base + normal * headWidth,
        base - normal * headWidth
    );
}

bool pointInArrow(vec2 point, vec2 direction) {
    return pointInThickSegment(
        point,
        -direction * 0.42,
        direction * 0.34,
        0.025
    ) || pointInArrowHead(
        point,
        direction * 0.40,
        direction,
        0.10,
        0.07
    );
}

bool pointInRadialArrow(vec2 point, vec2 radialDirection, float directionSign) {
    vec2 center = radialDirection * 0.48;
    vec2 arrowDirection = radialDirection * directionSign;
    return pointInThickSegment(
        point,
        center - arrowDirection * 0.08,
        center + arrowDirection * 0.08,
        0.018
    ) || pointInArrowHead(
        point,
        center + arrowDirection * 0.10,
        arrowDirection,
        0.06,
        0.04
    );
}

bool pointInRadialArrows(vec2 point, float directionSign) {
    const float QUARTER_PI = 0.7853981633974483;
    for (int index = 0; index < 8; ++index) {
        float angle = float(index) * QUARTER_PI;
        vec2 radialDirection = vec2(cos(angle), sin(angle));
        if (pointInRadialArrow(point, radialDirection, directionSign)) {
            return true;
        }
    }
    return false;
}

void compositeOver(inout vec3 premultipliedColor, inout float alpha, vec3 color, float sourceAlpha) {
    premultipliedColor = color * sourceAlpha + premultipliedColor * (1.0 - sourceAlpha);
    alpha = sourceAlpha + alpha * (1.0 - sourceAlpha);
}

void main() {
    float radiusSquared = dot(vLocalPosition, vLocalPosition);
    if (radiusSquared > 1.02) {
        discard;
    }
    if (radiusSquared > 1.0) {
        // GLSurfaceView used an opaque window. Keep the Vulkan RGBA surface
        // opaque across the transparent rim emitted by the background pass.
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float radiusPosition = sqrt(radiusSquared);
    int fieldType = int(environmentField.field.x + 0.5);
    float base = environmentField.field.y;
    float strength = environmentField.field.z;
    float angle = environmentField.field.w;
    float offset = environmentField.fieldExtra.x;
    float softness = clamp(environmentField.fieldExtra.y, 0.0, 1.0);
    float displayMin = environmentField.fieldExtra.z;
    float displayMax = environmentField.fieldExtra.w;
    float dishRadius = environmentField.preview.x;
    bool vectorPreview = environmentField.preview.y > 0.5;
    int vectorFieldType = int(environmentField.preview.z + 0.5);
    float vectorFieldAngle = environmentField.preview.w;

    vec3 premultipliedColor = vec3(0.0);
    float alpha = 0.0;
    if (!vectorPreview) {
        float coordinate = 0.0;
        if (fieldType == FIELD_LINEAR_DIRECTIONAL) {
            coordinate = dot(vLocalPosition, vec2(cos(angle), sin(angle)));
        } else if (fieldType == FIELD_RADIAL) {
            coordinate = radiusPosition;
        }
        coordinate -= offset;
        coordinate = coordinate / (1.0 + softness * abs(coordinate) * 2.0);
        float value = base + strength * coordinate;
        float displaySize = max(displayMax - displayMin, 0.0001);
        float normalized = clamp((value - displayMin) / displaySize, 0.0, 1.0);
        vec3 fieldColor = normalized < 0.5
            ? mix(LOW_COLOR, MID_COLOR, normalized * 2.0)
            : mix(MID_COLOR, HIGH_COLOR, (normalized - 0.5) * 2.0);
        float rim = smoothstep(0.98, 1.0, radiusPosition);
        fieldColor = mix(fieldColor, vec3(0.0), rim * 0.25);
        compositeOver(premultipliedColor, alpha, fieldColor, 0.48);
    }

    bool radialVector = vectorPreview && vectorFieldType != VECTOR_FIELD_LINEAR_DIRECTIONAL;
    bool markerVisible = (!vectorPreview && fieldType == FIELD_RADIAL) || radialVector;
    if (markerVisible) {
        vec2 worldPosition = vLocalPosition * dishRadius;
        float markerRadius = dishRadius * 0.05;
        float ringDistance = abs(length(worldPosition) - markerRadius);
        float backingCoverage = 1.0 - smoothstep(0.0045, 0.0049, ringDistance);
        float colorCoverage = 1.0 - smoothstep(0.0025, 0.0029, ringDistance);
        vec3 markerColor = mix(MARKER_BACKING_COLOR, MARKER_COLOR, colorCoverage);
        compositeOver(
            premultipliedColor,
            alpha,
            markerColor,
            backingCoverage * 0.9
        );
    }

    bool arrowVisible = false;
    if (vectorPreview) {
        if (vectorFieldType == VECTOR_FIELD_LINEAR_DIRECTIONAL) {
            vec2 direction = vec2(cos(vectorFieldAngle), sin(vectorFieldAngle));
            arrowVisible = pointInArrow(vLocalPosition, direction);
        } else {
            float directionSign = vectorFieldType == VECTOR_FIELD_RADIAL_INWARD ? -1.0 : 1.0;
            arrowVisible = pointInRadialArrows(vLocalPosition, directionSign);
        }
    } else if (fieldType == FIELD_LINEAR_DIRECTIONAL) {
        float directionSign = strength < 0.0 ? -1.0 : 1.0;
        vec2 direction = vec2(cos(angle), sin(angle)) * directionSign;
        arrowVisible = pointInArrow(vLocalPosition, direction);
    }

    if (arrowVisible) {
        outColor = vec4(ARROW_COLOR, 1.0);
        return;
    }
    if (alpha <= 0.0) {
        discard;
    }
    outColor = vec4(premultipliedColor / alpha, alpha);
}
