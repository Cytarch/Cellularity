precision highp float;

uniform sampler2D tex1;

varying vec4 p;
varying vec4 col;

const float ratio = 0.4;

float rho(float r2, float st) {
    return texture2D(tex1, vec2(r2 * 0.22596, st * 0.285212)).r;
}

void main() {
    float ds = dot(p.xy, p.xy);

    // MAX_R=0.03;
    // st to rho max=7.12028, stmax=7.12028/Ri<7.12028*MAX_R=0.213608
    // We want t to be more than tmax when p.z=1
    float st = p.z * 0.105185;


    float Ri = 1.0 / col.a;
    gl_FragColor.rgb = (ratio * 0.5 + (1.0 - ratio)) * col.rgb;
    gl_FragColor.a = (ratio + 0.5 * (1.0 - ratio)) * rho(ds * Ri * Ri, st * Ri);
}