uniform mat4 uMVPMatrix;
attribute vec4 vPosition;
attribute vec4 vTPosition;
attribute vec4 vCenter;
attribute vec4 vColor;

varying vec2 cellPosition;
varying float wallDistance;
varying float path1MarkerRadius;
varying float path2MarkerRadius;
varying float pathMarkerAngle;
varying vec4 cellColor;
varying float cellRadius;

const float SCALE = 50.0;

void main() {
    cellPosition = vPosition.xy * SCALE;
    wallDistance = vTPosition.y * SCALE;
    path1MarkerRadius = vTPosition.x;
    path2MarkerRadius = vTPosition.w;
    pathMarkerAngle = vTPosition.z;
    cellColor = vColor;
    cellRadius = cellColor.a * SCALE;

    vec4 mp = vPosition;
    mp.xy += vCenter.xy;
    gl_Position = uMVPMatrix * mp;
}
