#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec2 vGridParameters;
layout(location = 0) out vec2 worldPosition;
layout(location = 1) flat out vec2 gridParameters;

void main() {
    worldPosition = vPosition;
    gridParameters = vGridParameters;
    vec4 clipPosition = cell.mvpMatrix * vec4(vPosition, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
