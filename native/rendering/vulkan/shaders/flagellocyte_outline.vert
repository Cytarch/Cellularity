#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec4 vTPosition;
layout(location = 2) in vec4 vCenter;
layout(location = 3) in vec4 vColor;
layout(location = 0) out vec2 p;
layout(location = 1) out vec2 texturePosition;
layout(location = 2) out vec4 col;
layout(location = 3) out float animationTime;

const float SCALE = 50.0;

void main() {
    p = vPosition * SCALE;
    texturePosition = vTPosition.xy;
    animationTime = vTPosition.z * (45.0 / 0.4);
    col = vColor;
    col.a *= SCALE;
    vec4 clipPosition = cell.mvpMatrix * vec4(vPosition + vCenter.xy, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
