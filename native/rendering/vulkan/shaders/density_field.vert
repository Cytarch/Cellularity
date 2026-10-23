#version 450

layout(location = 0) out vec2 vLocalPosition;

layout(std140, set = 0, binding = 0) uniform EnvironmentFieldUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) vec4 field;
    layout(offset = 80) vec4 fieldExtra;
    layout(offset = 96) vec4 preview;
    layout(offset = 112) mat2 preRotation;
} environmentField;

const vec2 POSITIONS[6] = vec2[](
    vec2(1.1, 1.1),
    vec2(1.1, -1.1),
    vec2(-1.1, -1.1),
    vec2(-1.1, -1.1),
    vec2(-1.1, 1.1),
    vec2(1.1, 1.1)
);

void main() {
    vec2 localPosition = POSITIONS[gl_VertexIndex];
    vLocalPosition = localPosition;

    float radius = environmentField.preview.x;
    vec4 clipPosition = environmentField.mvpMatrix *
        vec4(localPosition * radius, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = environmentField.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
