precision highp float;

varying vec2 vWorldPosition;

uniform float uWorldRadius;
uniform float uGridSize;

const vec3 BACKGROUND_COLOR = vec3(0.125, 0.125, 0.125);
const vec3 GRID_COLOR = vec3(0.0, 0.0, 0.0);
const float GRID_LINE_WIDTH = 0.018;

void main() {
    float cellSize = (2.0 * uWorldRadius) / uGridSize;
    vec2 gridPosition = (vWorldPosition + uWorldRadius) / cellSize;
    vec2 distanceToLine = abs(fract(gridPosition + 0.5) - 0.5);
    float gridDistance = min(distanceToLine.x, distanceToLine.y);
    float gridLine = 1.0 - smoothstep(GRID_LINE_WIDTH, GRID_LINE_WIDTH * 1.8, gridDistance);

    vec3 color = mix(BACKGROUND_COLOR, GRID_COLOR, gridLine * 0.55);
    gl_FragColor = vec4(color, 1.0);
}
