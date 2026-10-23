#include "app/EnvironmentFieldPreviewRenderer.h"

#include <array>
#include <cmath>
#include <vector>

#include "core/Environment.h"
#include "render/NativeCellRendererResources.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/gl/NativeGlApi.h"

namespace {

void appendThickSegment(std::vector<float>& vertices, float x1, float y1, float x2, float y2, float width) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.000001f) {
        return;
    }
    float nx = -dy / length * width * 0.5f;
    float ny = dx / length * width * 0.5f;
    float segment[12] = {
            x1 + nx, y1 + ny,
            x1 - nx, y1 - ny,
            x2 - nx, y2 - ny,
            x1 + nx, y1 + ny,
            x2 - nx, y2 - ny,
            x2 + nx, y2 + ny
    };
    vertices.insert(vertices.end(), segment, segment + 12);
}

void appendArrowHead(std::vector<float>& vertices, float x, float y, float dx, float dy, float length, float width) {
    float directionLength = std::sqrt(dx * dx + dy * dy);
    if (directionLength <= 0.000001f) {
        return;
    }
    dx /= directionLength;
    dy /= directionLength;
    float nx = -dy;
    float ny = dx;
    float head[6] = {
            x + dx * length, y + dy * length,
            x - dx * length * 0.45f + nx * width, y - dy * length * 0.45f + ny * width,
            x - dx * length * 0.45f - nx * width, y - dy * length * 0.45f - ny * width
    };
    vertices.insert(vertices.end(), head, head + 6);
}

} // namespace

int EnvironmentFieldPreviewRenderer::draw(
        const cellularity::render::EnvironmentFieldPreviewCommand& command,
        NativeGlApi& gl,
        NativeCellRendererResources& resources) {
    static constexpr std::array<float, 3> previewBackgroundColor{0.78f, 0.80f, 0.83f};

    const auto& matrix = command.matrix;
    gl.viewport(0, 0, command.surfaceWidth, command.surfaceHeight);
    gl.clearColor(26.0f / 255.0f, 28.0f / 255.0f, 31.0f / 255.0f, 1.0f);
    gl.depthMask(true);
    gl.clear(gl.colorBufferBit() | gl.depthBufferBit());
    gl.disable(gl.depthTest());

    resources.backgroundShader().draw(matrix,
            Environment::PI / 2.0,
            0.75,
            0.18,
            command.dishRadius,
            previewBackgroundColor);
    gl.enable(gl.blend());
    gl.blendFunc(gl.srcAlpha(), gl.oneMinusSrcAlpha());
    gl.depthMask(false);
    if (!command.vectorPreview) {
        resources.densityFieldShader().draw(matrix,
                command.fieldType,
                command.fieldBase,
                command.fieldStrength,
                command.fieldAngle,
                command.fieldOffset,
                command.fieldSoftness,
                command.dishRadius,
                command.displayMin,
                command.displayMax);
    }

    std::vector<float> arrowVertices;
    float radius = static_cast<float>(command.dishRadius);
    if (command.vectorPreview) {
        if (command.gravityFieldType == Environment::VECTOR_FIELD_RADIAL_INWARD
                || command.gravityFieldType == Environment::VECTOR_FIELD_RADIAL_OUTWARD) {
            float sign = command.gravityFieldType == Environment::VECTOR_FIELD_RADIAL_INWARD ? -1.0f : 1.0f;
            for (int i = 0; i < 8; i++) {
                float angle = static_cast<float>(i) * static_cast<float>(Environment::PI) * 0.25f;
                float px = std::cos(angle) * radius * 0.48f;
                float py = std::sin(angle) * radius * 0.48f;
                float dx = std::cos(angle) * sign;
                float dy = std::sin(angle) * sign;
                appendThickSegment(arrowVertices,
                        px - dx * radius * 0.08f,
                        py - dy * radius * 0.08f,
                        px + dx * radius * 0.08f,
                        py + dy * radius * 0.08f,
                        radius * 0.018f);
                appendArrowHead(arrowVertices,
                        px + dx * radius * 0.10f,
                        py + dy * radius * 0.10f,
                        dx,
                        dy,
                        radius * 0.06f,
                        radius * 0.04f);
            }
            resources.markerShader().draw(
                    matrix, 0.0f, 0.0f, radius * 0.05f, 0.9f);
        } else {
            float dx = std::cos(static_cast<float>(command.gravityFieldAngle));
            float dy = std::sin(static_cast<float>(command.gravityFieldAngle));
            appendThickSegment(arrowVertices,
                    -dx * radius * 0.42f,
                    -dy * radius * 0.42f,
                    dx * radius * 0.34f,
                    dy * radius * 0.34f,
                    radius * 0.025f);
            appendArrowHead(arrowVertices,
                    dx * radius * 0.40f,
                    dy * radius * 0.40f,
                    dx,
                    dy,
                    radius * 0.10f,
                    radius * 0.07f);
        }
        resources.solidColorShader().draw(matrix, arrowVertices);
    } else if (command.fieldType == Environment::DENSITY_FIELD_RADIAL) {
        resources.markerShader().draw(
                matrix, 0.0f, 0.0f, radius * 0.05f, 0.9f);
    } else if (command.fieldType != Environment::DENSITY_FIELD_CONSTANT) {
        float angle = static_cast<float>(command.fieldAngle);
        float direction = command.fieldStrength < 0.0 ? -1.0f : 1.0f;
        float dx = std::cos(angle) * direction;
        float dy = std::sin(angle) * direction;
        appendThickSegment(arrowVertices,
                -dx * radius * 0.42f,
                -dy * radius * 0.42f,
                dx * radius * 0.34f,
                dy * radius * 0.34f,
                radius * 0.025f);
        appendArrowHead(arrowVertices,
                dx * radius * 0.40f,
                dy * radius * 0.40f,
                dx,
                dy,
                radius * 0.10f,
                radius * 0.07f);
        resources.solidColorShader().draw(matrix, arrowVertices);
    }
    gl.depthMask(true);
    return 1;
}
