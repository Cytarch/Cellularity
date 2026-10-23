#version 450

layout(location = 0) in vec3 linkLocal;
layout(location = 0) out vec4 fragmentColor;

const float LINK_TAPER_TIP_Y = 1.0;
const float LINK_TAPER_EPSILON = 0.000001;
const float LINK_STRIPE_FREQUENCY = 3.0;
const float LINK_STRIPE_PHASE_SHIFT = 0.3;
const float LINK_STRIPE_VISIBLE_PHASE_WIDTH = 0.4;
const float LINK_STRIPE_PATTERN_LIMIT = 0.7;
const float LINK_ALPHA_INSET = 0.1;

void main() {
    float taperWidth = LINK_TAPER_TIP_Y - linkLocal.y;
    if (taperWidth <= LINK_TAPER_EPSILON) {
        fragmentColor = vec4(0.0);
        return;
    }
    float taperedSide = linkLocal.x / taperWidth;
    float linkPatternPosition = mix(taperedSide, linkLocal.z, LINK_TAPER_TIP_Y - abs(taperedSide) * linkLocal.y);
    float stripePhase = fract(linkPatternPosition * LINK_STRIPE_FREQUENCY - LINK_STRIPE_PHASE_SHIFT);
    float linkAlpha = (linkLocal.y - LINK_TAPER_TIP_Y) * (linkLocal.y - LINK_TAPER_TIP_Y)
            + linkLocal.x * linkLocal.x - LINK_ALPHA_INSET;
    if (stripePhase < LINK_STRIPE_VISIBLE_PHASE_WIDTH && linkPatternPosition < LINK_STRIPE_PATTERN_LIMIT && linkPatternPosition > -LINK_STRIPE_PATTERN_LIMIT) {
        fragmentColor = vec4(0.0, 0.0, 0.0, linkAlpha);
    } else {
        fragmentColor = vec4(0.0);
    }
}
