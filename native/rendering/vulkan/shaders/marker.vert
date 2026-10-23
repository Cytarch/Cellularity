#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec4 vCenter;
layout(location = 0) out vec2 p;
layout(location = 1) out vec4 cp;

void main() {
    vec2 shifted = vPosition * 1.1 * (vCenter.z + 0.005) + vCenter.xy;
    p = shifted;
    cp = vCenter;
    vec4 clipPosition = cell.mvpMatrix * vec4(shifted, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
