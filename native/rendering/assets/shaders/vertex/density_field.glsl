uniform mat4 uMVPMatrix;
uniform float uRadius;

attribute vec4 aPosition;

varying vec2 vLocalPosition;

void main() {
    vLocalPosition = aPosition.xy;
    gl_Position = uMVPMatrix * vec4(aPosition.xy * uRadius, aPosition.zw);
}
