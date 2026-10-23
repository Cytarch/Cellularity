#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec4 vInstance;
layout(location = 2) in vec4 vColor;
layout(location = 0) out vec4 p;
layout(location = 1) out vec4 col;

void main() {
    vec2 localPosition = vPosition * vInstance.z;
    p = vec4(localPosition, vInstance.w, 0.0);
    col = vColor;
    vec4 clipPosition = cell.mvpMatrix * vec4(localPosition + vInstance.xy, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
