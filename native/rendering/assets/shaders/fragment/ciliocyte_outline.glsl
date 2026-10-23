precision highp float;

varying vec2 p;
varying vec2 tp, t12;
varying vec4 col;
#define M_PI 3.1415926535897932384626433832795

const float scale = 50.0;

const float N = 33.0;
const float Nhat = N / (2.0 * M_PI);
const float MAX_R = 0.03 * scale;
const float R = MAX_R * 0.02;
const float H = MAX_R * 0.4;
const float HW = MAX_R * 0.1 / 2.0 / 2.0;
const float X = HW + R * 0.93945122136; //tan(atan(H/HW)/2.);
const float x1 = X - R * 0.99805257848; //sin(atan(H/HW));

void main() {

    float lp = length(p);
    float d = lp - col.a;

    if (d < -0.01 * col.a) {
        discard;
    }

    gl_FragColor.rgb = col.rgb * 0.5;

    vec2 ntp = normalize(tp);
    float x1 = ntp.x;
    float y1 = ntp.y;
    float d12 = (x1 - 1.0) * (x1 - 1.0) + y1 * y1;
    float d22 = (x1 + 1.0) * (x1 + 1.0) + y1 * y1;
    float y2 = -0.5 * log(d12 / d22);

    float w;
    if (y1 > 0.0) {
        w = abs(sin(log(d12 / d22) * 6.66 + t12.x)) * 0.075 * sqrt(d12 * d22) * col.a;
    } else {
        w = abs(sin(log(d12 / d22) * 6.66 + t12.y)) * 0.075 * sqrt(d12 * d22) * col.a;
    }

    if (d < w) {
        gl_FragColor.a = 1.0;
    } else {
        gl_FragColor.a = 0.0;
    }
}