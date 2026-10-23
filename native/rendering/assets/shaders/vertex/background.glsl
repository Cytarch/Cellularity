uniform mat4 uMVPMatrix;
uniform float uRadius;

attribute vec4 aPosition;

varying vec2 vLocalPosition;

void main() {
    vLocalPosition = aPosition.xy;

    vec4 transformedPosition = vec4(aPosition.xy * uRadius, aPosition.zw);

    gl_Position = uMVPMatrix * transformedPosition;
}