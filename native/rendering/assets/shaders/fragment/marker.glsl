precision highp float;
precision mediump int;

varying vec2 p;
varying vec4 cp;

const vec3 markerColor = vec3(0.407843, 0.8, 0.294118);
const vec3 markerBackingColor = vec3(0.12549);

void main() {
    vec2 d = p - cp.xy;
    float ringDistance = abs(length(d) - cp.z);
    float backingCoverage = 1.0 - smoothstep(0.0045, 0.0049, ringDistance);

    if (backingCoverage <= 0.0) {
        discard;
    }

    float colorCoverage = 1.0 - smoothstep(0.0025, 0.0029, ringDistance);
    vec3 color = mix(markerBackingColor, markerColor, colorCoverage);
    gl_FragColor = vec4(color, backingCoverage * cp.a);
}
