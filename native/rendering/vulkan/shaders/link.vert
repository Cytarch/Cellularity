#version 450

layout(std140, set = 0, binding = 0) uniform CellUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) mat2 preRotation;
} cell;
layout(location = 0) in vec4 vLinkVertex;
layout(location = 1) in float vLinkPatternPosition;
layout(location = 0) out vec3 linkLocal;

void main() {
    linkLocal = vec3(vLinkVertex.zw, vLinkPatternPosition);
    vec4 clipPosition = cell.mvpMatrix * vec4(vLinkVertex.xy, 0.0, 1.0);
    clipPosition.y = -clipPosition.y;
    clipPosition.xy = cell.preRotation * clipPosition.xy;
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
