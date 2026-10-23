precision highp float;

varying vec2 vLocalPosition;

uniform vec4 uField;
uniform vec4 uFieldExtra;
uniform vec4 uDisplayRange;

const int FIELD_CONSTANT = 0;
const int FIELD_LINEAR_DIRECTIONAL = 1;
const int FIELD_RADIAL = 2;

void main() {
    float radiusPosition = length(vLocalPosition);
    if (radiusPosition > 1.0) {
        discard;
    }

    int fieldType = int(uField.x + 0.5);
    float base = uField.y;
    float strength = uField.z;
    float angle = uField.w;
    float offset = uFieldExtra.x;
    float softness = clamp(uFieldExtra.y, 0.0, 1.0);
    float coordinate = 0.0;
    if (fieldType == FIELD_LINEAR_DIRECTIONAL) {
        coordinate = dot(vLocalPosition, vec2(cos(angle), sin(angle)));
    } else if (fieldType == FIELD_RADIAL) {
        coordinate = radiusPosition;
    }

    coordinate -= offset;
    coordinate = coordinate / (1.0 + softness * abs(coordinate) * 2.0);
    float value = base + strength * coordinate;
    float displaySize = max(uDisplayRange.y - uDisplayRange.x, 0.0001);
    float normalized = clamp((value - uDisplayRange.x) / displaySize, 0.0, 1.0);
    vec3 lowColor = vec3(0.18, 0.42, 0.78);
    vec3 midColor = vec3(0.55, 0.72, 0.68);
    vec3 highColor = vec3(0.96, 0.64, 0.25);
    vec3 color = normalized < 0.5
            ? mix(lowColor, midColor, normalized * 2.0)
            : mix(midColor, highColor, (normalized - 0.5) * 2.0);
    float rim = smoothstep(0.98, 1.0, radiusPosition);
    gl_FragColor = vec4(mix(color, vec3(0.0), rim * 0.25), 0.48);
}
