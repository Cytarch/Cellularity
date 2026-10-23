#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec4 vTPosition;
layout(location = 2) in vec2 vCenter;
layout(location = 3) in vec4 vColor;
layout(location = 0) out vec2 cellPosition;
layout(location = 1) out float cellRadius;
layout(location = 2) out float wallDistance;
layout(location = 3) out vec4 cellColor;

const float SCALE = 50.0;

void main() {
    cellPosition = vPosition.xy * SCALE;
    wallDistance = vTPosition.y * SCALE;
    cellColor = vColor;
    cellRadius = cellColor.a * SCALE;
    vec4 clipPosition =
            cell.mvpMatrix * vec4(vPosition + vCenter, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
