#version 450

layout(location = 0) in vec2 worldPosition;
layout(location = 1) flat in vec2 gridParameters;
layout(location = 0) out vec4 fragmentColor;

const vec3 BACKGROUND_COLOR = vec3(0.125);
const vec3 GRID_COLOR = vec3(0.0);
const float GRID_LINE_WIDTH = 0.018;

void main() {
    float worldRadius = gridParameters.x;
    float gridSize = gridParameters.y;
    float cellSize = (2.0 * worldRadius) / gridSize;
    vec2 gridPosition = (worldPosition + worldRadius) / cellSize;
    vec2 distanceToLine = abs(fract(gridPosition + 0.5) - 0.5);
    float gridDistance = min(distanceToLine.x, distanceToLine.y);
    float gridLine = 1.0 - smoothstep(
        GRID_LINE_WIDTH,
        GRID_LINE_WIDTH * 1.8,
        gridDistance
    );
    vec3 color = mix(BACKGROUND_COLOR, GRID_COLOR, gridLine * 0.55);
    fragmentColor = vec4(color, 1.0);
}
