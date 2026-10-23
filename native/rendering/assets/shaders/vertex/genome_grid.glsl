uniform mat4 uMVPMatrix;

attribute vec4 aPosition;

varying vec2 vWorldPosition;

void main() {
    vWorldPosition = aPosition.xy;
    gl_Position = uMVPMatrix * vec4(aPosition.xy, aPosition.zw);
}
