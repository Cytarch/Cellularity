precision highp float;

varying vec2 p;
varying vec4 c;

void main() {
    float dist = dot(p, p);

    if (dist > 1.01) {
        discard;
    }

    gl_FragColor = c;
}