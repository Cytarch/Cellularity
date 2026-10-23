precision highp float;

const float scale = 50.0;
const float yscale = 20.0 / scale;

varying vec2 p;
varying vec2 tp;
varying vec4 col;

#define M_PI 3.1415926535897932384626433832795

const float N = 5.0;
const float Nhat = N / (2.0 * M_PI);
const float MAX_R = 0.03 * scale;
const float R = MAX_R * 0.04;
const float H = MAX_R * 0.3;
const float HW = MAX_R * 0.1 / 2.0;
const float X = HW + R * 0.84712708838; //tan(atan(H/HW)/2.);
const float x1 = X - R * 0.98639392383; //sin(atan(H/HW));
const int MAX_IT = 9;
const mat2 m = mat2(
1.0, 1.0,
1.0, -1.0
);

float GiveCx(float x, float y)
{
    return (y * y * y * y - 4.0 * y * y * y + (2.0 * x * x + 2.0) * y * y + (4.0 - 4.0 * x * x) * y + x * x * x * x + 6.0 * x * x - 3.0)
    / (4.0 * y * y * y * y - 16.0 * y * y * y + (8.0 * x * x + 24.0) * y * y + (-16.0 * x * x - 16.0) * y + 4.0 * x * x * x * x + 8.0 * x * x + 4.0);
}

float GiveCy(float x, float y)
{
    return -(2.0 * x * y - 2.0 * x) / (y * y * y * y - 4.0 * y * y * y + (2.0 * x * x + 6.0) * y * y + (-4.0 * x * x - 4.0) * y + x * x * x * x + 2.0 * x * x + 1.0);
}


void main() {
    float d = length(p) - col.a;

    if (d < -0.05 * col.a) {
        discard;
    }

    float a = 2.0 * fract(atan(tp.y, tp.x) * Nhat);
    if (a > 1.0) {
        a = 2.0 - a;
    }

    vec2 c = vec2(GiveCx(1.563 + a / 3.0, d * yscale), GiveCy(1.563 + a / 3.0, d * yscale));
    vec2 z = c;
    for (int i = 0; i < MAX_IT; i++) {
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        z = vec2(z.x * z.x - z.y * z.y, z.x * z.y * 2.0) + c;
        if (dot(z, z) > 4.0) {
            z = vec2(2.0, 2.0);
        }
    }

    gl_FragColor.rgb = col.rgb * 0.5;
    if (dot(z, z) > 4.0) {
        gl_FragColor.a = 0.0;
    } else {
        gl_FragColor.a = 1.0;
    }
}