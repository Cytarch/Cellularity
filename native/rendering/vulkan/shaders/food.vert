#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec4 vInstance;
layout(location = 2) in vec4 color;
layout(location = 0) out vec2 p;
layout(location = 1) out vec4 c;

void main() {
    p = vPosition;
    c = color;
    vec4 clipPosition = cell.mvpMatrix * vec4(vInstance.xy + vPosition * vInstance.z, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
