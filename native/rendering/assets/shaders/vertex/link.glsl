uniform mat4 uMVPMatrix;
attribute vec4 vLinkVertex;
attribute float vLinkPatternPosition;
varying vec3 linkLocal;

void main() {
    linkLocal.xy = vLinkVertex.zw;
    linkLocal.z = vLinkPatternPosition;
    gl_Position = uMVPMatrix * vec4(vLinkVertex.xy, 0.0, 1.0);
}
