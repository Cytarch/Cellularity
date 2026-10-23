#version 450

layout(location = 0) out vec2 vLocalPosition;

layout(std140, set = 0, binding = 0) uniform BackgroundUniforms {
    layout(offset = 0) mat4 mvpMatrix;
    layout(offset = 64) vec4 lightDirectionRangeIntensity;
    layout(offset = 80) vec4 backgroundColorRadius;
    layout(offset = 96) mat2 preRotation;
    layout(offset = 128) vec4 geometry;
} background;

const vec2 POSITIONS[6] = vec2[](
    vec2(1.1, 1.1),
    vec2(1.1, -1.1),
    vec2(-1.1, -1.1),
    vec2(-1.1, -1.1),
    vec2(-1.1, 1.1),
    vec2(1.1, 1.1)
);

void main() {
    vec2 localPosition = POSITIONS[gl_VertexIndex];
    vLocalPosition = localPosition;

    float radius = background.backgroundColorRadius.w;
    vec4 clipPosition = background.mvpMatrix * vec4(localPosition * radius, 0.0, 1.0);

    // GLES framebuffer Y points up while Vulkan framebuffer Y points down.
    clipPosition.y = -clipPosition.y;
    // Render directly in the display's current orientation so Android does
    // not need a compositor rotation pass.
    clipPosition.xy = background.preRotation * clipPosition.xy;
    // Convert the GLES/OpenGL [-w, +w] depth convention to Vulkan [0, +w].
    clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w);
    gl_Position = clipPosition;
}
