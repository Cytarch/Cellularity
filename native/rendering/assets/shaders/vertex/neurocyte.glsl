uniform mat4 uMVPMatrix;
attribute vec4 vPosition;
attribute vec4 vTPosition;
attribute vec4 vCenter;
attribute vec4 vColor;

varying vec2 cellPosition;
varying float signalOutputA;
varying float signalOutputB;
varying float signalPatternAngle;
varying float wallDistance;
varying vec4 cellColor;
varying float cellRadius;

const float SCALE = 50.0;

void main() {
    cellPosition = vPosition.xy * SCALE;
    signalOutputA = vTPosition.x;
    signalOutputB = vTPosition.w;
    signalPatternAngle = vTPosition.z;
    wallDistance = vTPosition.y * SCALE;
    cellColor = vColor;
    cellRadius = cellColor.a * SCALE;

    vec4 mp = vPosition;
    mp.xy += vCenter.xy;
    gl_Position = uMVPMatrix * mp;
}
