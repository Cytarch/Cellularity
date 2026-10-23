uniform mat4 uMVPMatrix;
attribute vec4 vPosition;
attribute vec4 vTPosition;
attribute vec4 vCenter;
attribute vec4 vColor;

varying vec2 cellPosition;
varying vec2 muscleStripeDirection;
varying float visualContraction;
varying float wallDistance;
varying float visualBending;
varying vec4 cellColor;
varying float cellRadius;

const float SCALE = 50.0;

void main() {
    cellPosition = vPosition.xy * SCALE;
    visualContraction = vTPosition.x;
    wallDistance = vTPosition.y * SCALE;
    muscleStripeDirection = vec2(cos(vTPosition.z), sin(vTPosition.z));
    visualBending = vTPosition.w;
    cellColor = vColor;
    cellRadius = cellColor.a * SCALE;

    vec4 mp = vPosition;
    mp.xy += vCenter.xy;
    gl_Position = uMVPMatrix * mp;
}
