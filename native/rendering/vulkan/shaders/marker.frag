#version 450

layout(location = 0) in vec2 p;
layout(location = 1) in vec4 cp;
layout(location = 0) out vec4 outColor;

const vec3 MARKER_COLOR = vec3(0.407843, 0.8, 0.294118);
const vec3 MARKER_BACKING_COLOR = vec3(0.12549);

void main() {
    vec2 d = p - cp.xy;
    float ringDistance = abs(length(d) - cp.z);
    float backingCoverage =
            1.0 - smoothstep(0.0045, 0.0049, ringDistance);
    if (backingCoverage <= 0.0) {
        discard;
    }

    float colorCoverage =
            1.0 - smoothstep(0.0025, 0.0029, ringDistance);
    vec3 color = mix(MARKER_BACKING_COLOR, MARKER_COLOR, colorCoverage);
    outColor = vec4(color, backingCoverage * cp.a);
}
