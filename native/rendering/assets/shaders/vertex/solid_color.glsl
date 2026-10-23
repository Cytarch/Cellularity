uniform mat4 uMVPMatrix;

attribute vec2 aPosition;

void main() {
    gl_Position = uMVPMatrix * vec4(aPosition.xy, 0.0, 1.0);
}
