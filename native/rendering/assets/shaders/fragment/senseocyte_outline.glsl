precision highp float;

varying vec2 p;
varying vec2 tp;
varying vec4 col;
varying float rot;
varying vec2 move;
#define M_PI 3.1415926535897932384626433832795

const float scale = 50.0;

const float N = 9.0;
const float Nhat = N / (2.0 * M_PI);
const float MAX_R = 0.03 * scale;
const float R = MAX_R * 0.02;
const float H = MAX_R * 0.4;
const float HW = MAX_R * 0.1 / 2.0 / 2.0;
const float X = HW + R * 0.93945122136; //tan(atan(H/HW)/2.);
const float x1 = X - R * 0.99805257848; //sin(atan(H/HW));
const float EW = 0.002 * scale;

const float n = 2.0;
const float d1overd2 = 1.075;
const float d2 = M_PI / (n - 1.0 + 2.0 * d1overd2);
const float d1 = d2 * d1overd2;
const float spikeLength = MAX_R * 0.2;
const float factor = spikeLength / (1.0 / (d1 - d2) + 1.0 / (2.0 * d1 - d2 - (d1 - d2)));

void main() {

    float lp = length(p);
    float d = lp - col.a + 0.0001;

    if (d < 0.0) {
        discard;
    }

    float phi = atan(tp.y, tp.x);
    float phif = floor(phi * Nhat) * (1.0 / Nhat);

    float a = 2.0 * Nhat * (phi - phif);

    float x = (1.0 - a) * col.a * M_PI / N;
    float angle = atan(d, x);


    if (angle > 0.5 * M_PI) {
        angle = M_PI - angle;
    }

    if (angle > d1) {
        angle = fract((angle - d1) / d2) * d2 + d1 - d2;
    }

    float ml = (1.0 / angle + 1.0 / (2.0 * d1 - d2 - angle)) * factor;


    gl_FragColor.rgb = col.rgb * 0.5;
    if (x * x + d * d < ml * ml) {
        gl_FragColor.a = 1.0;
    } else {
        gl_FragColor.a = 0.0;
    }
}