precision highp float;

varying vec2 vLocalPosition;
uniform vec4 uDirection;
uniform vec3 uBackgroundColor;
uniform float uInnerRadiusSquared;

const vec3 WARM_COLOR = vec3(0.5, 0.35, 0.25);

void main() {
    float distSq = dot(vLocalPosition, vLocalPosition);

    if (distSq > 1.02 || (uInnerRadiusSquared > 0.0 && distSq < uInnerRadiusSquared)) {
        discard;
    }

    if (distSq > 0.999) {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec2 lightDirection = uDirection.xy;
    float lightRange = uDirection.z;
    float lightIntensity = uDirection.w;

    float lightDistance = 1.0 + (1.0 - lightRange) * (1.0 - distSq) / lightRange;
    float lightProjection = dot(lightDirection, vLocalPosition);

    float lightFactor = (lightProjection * (1.0 - lightRange) + lightRange) / (lightDistance * lightDistance);
    float illumination = lightIntensity * max(lightFactor, 0.0);

    vec3 finalColor = uBackgroundColor + (illumination * WARM_COLOR);
    gl_FragColor = vec4(finalColor, 1.0);
}
