uniform mat4 uMVPMatrix;
attribute vec2 vPosition;
attribute vec4 vInstance;
attribute vec4 vColor;
varying vec4 p;
varying vec4 col;

const float scale = 50.0;

void main() {
    vec2 localPosition = vPosition * vInstance.z;
    p = vec4(localPosition, vInstance.w, 0.0);
    col = vColor;
    gl_Position = uMVPMatrix * vec4(localPosition + vInstance.xy, 0.0, 1.0);
}
