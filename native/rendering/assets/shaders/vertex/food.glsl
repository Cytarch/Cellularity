uniform mat4 uMVPMatrix;

attribute vec2 vPosition;
attribute vec4 vInstance;
attribute vec4 color;

varying vec2 p;
varying vec4 c;


void main() {
    p = vPosition;
    c = color;
    gl_Position = uMVPMatrix * vec4(vInstance.xy + vPosition * vInstance.z, 0.0, 1.0);
}
