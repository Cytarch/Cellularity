#include "render/frame/RenderLinkGeometryBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "core/CellDataConstants.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderSourceFrame.h"

void cellularity::render::RenderLinkGeometryBuilder::reset(
        std::size_t maximumVertexCount) noexcept {
    mMaximumVertexCount = maximumVertexCount;
}

void cellularity::render::RenderLinkGeometryBuilder::build(
        const RenderSourceFrame& source, PreparedRenderFrame& frame,
        RenderContactChordResolver& contactChordResolver,
        RenderContactChordResolver::Profile* contactChordProfile,
        RenderContactChordNormalizationProfile* normalizationProfile,
        Profile* profile) const {
    (void)profile;
#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        profile->topologyEdgeCount = static_cast<int>(std::min<std::uint64_t>(
                source.sourceTopologyEdgeCount,
                static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
        profile->topologyBoundaryCount = static_cast<int>(std::min<std::uint64_t>(
                source.sourceBoundaryLinkCount,
                static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
        profile->candidateEdgeCount = 0;
        profile->candidateBoundaryCount = 0;
        profile->contactCrossSectionCount = 0;
        profile->normalFallbackCount = 0;
        profile->boundaryFallbackCount = 0;
        profile->discardedContactCount = 0;
        profile->degenerateLinkCount = 0;
    }
#endif
    if (source.candidateLinks.empty()) {
        return;
    }

    const float nominalLinkWidth =
            static_cast<float>(CellDataConstants::MIN_CELL_RADIUS * 0.7);
    const float nominalLinkWidthSq = nominalLinkWidth * nominalLinkWidth;
    // This absolute threshold only handles an undefined fallback orientation.
    constexpr float minimumFallbackAxisLength = 1e-9f;
    constexpr float minimumFallbackAxisLengthSq = minimumFallbackAxisLength * minimumFallbackAxisLength;
    auto linkWritableVertexLimit = [&]() {
        return std::min({mMaximumVertexCount, frame.linkTriangleCoords.size() / 4U,
                frame.linkTextureCoords.size()});
    };
    std::size_t writableLinkVertexCount = linkWritableVertexLimit();
    auto ensureLinkAppendCapacity = [&]() {
        if (frame.drawLinksCount < 0) {
            throw std::length_error(
                    "RenderLinkGeometryBuilder output exceeds its bound");
        }
        const std::size_t currentVertexCount =
                static_cast<std::size_t>(frame.drawLinksCount);
        constexpr std::size_t appendedVertices = 6U;
        if (currentVertexCount <= writableLinkVertexCount && appendedVertices <= writableLinkVertexCount - currentVertexCount) {
            return;
        }
        if (currentVertexCount > mMaximumVertexCount || appendedVertices > mMaximumVertexCount - currentVertexCount) {
            throw std::length_error(
                    "RenderLinkGeometryBuilder output exceeds its bound");
        }
        const std::size_t requiredVertexCount =
                currentVertexCount + appendedVertices;
        const std::size_t requiredTriangleFloats = requiredVertexCount * 4U;
        if (frame.linkTriangleCoords.size() < requiredTriangleFloats) {
            frame.linkTriangleCoords.resize(requiredTriangleFloats);
        }
        if (frame.linkTextureCoords.size() < requiredVertexCount) {
            frame.linkTextureCoords.resize(requiredVertexCount);
        }
        writableLinkVertexCount = linkWritableVertexLimit();
    };
    auto skipDegenerateLink = [&]() {
        ++frame.diagnostics.degenerateLinkCount;
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            ++profile->degenerateLinkCount;
        }
#endif
    };
    auto appendLinkEdge = [&](const RenderSourceLink& link) {
        const float cellPositionX = link.cellAPositionX;
        const float cellPositionY = link.cellAPositionY;
        const float connectedX = link.cellBPositionX;
        const float connectedY = link.cellBPositionY;
        const float renderStartOffsetX = link.renderStartOffsetX;
        const float renderStartOffsetY = link.renderStartOffsetY;
        const float renderEndOffsetX = link.renderEndOffsetX;
        const float renderEndOffsetY = link.renderEndOffsetY;
        const float renderStartX = renderStartOffsetX + cellPositionX;
        const float renderStartY = renderStartOffsetY + cellPositionY;
        const float renderEndX = renderEndOffsetX + connectedX;
        const float renderEndY = renderEndOffsetY + connectedY;
        const RenderContactChordResolver::View chords = contactChordResolver.resolve(
                source, link.sourceCellARecordIndex,
                RenderContactChordResolver::Consumer::LinkLookup,
                contactChordProfile, normalizationProfile);
        const RenderContactChord* matchingChord = nullptr;
        for (int index = 0; index < chords.count; ++index) {
            if (chords.data[index].neighborCellIndex == link.sourceCellBIndex) {
                matchingChord = &chords.data[index];
                break;
            }
        }
        if (link.isBoundary() && source.environment.dishInnerRadius > 0.0) {
            const double innerRadius = source.environment.dishInnerRadius;
            const double middleRadius = (innerRadius + source.environment.dishRadius) * 0.5;
            const bool anchorIsInner = connectedX * connectedX + connectedY * connectedY < innerRadius * innerRadius;
            const bool contactIsInner = cellPositionX * cellPositionX + cellPositionY * cellPositionY < middleRadius * middleRadius;
            if (anchorIsInner != contactIsInner) {
                matchingChord = nullptr;
            }
        }
        if (matchingChord != nullptr && matchingChord->isDiscarded) {
#ifdef CELLULARITY_RENDER_PROFILE
            if (profile != nullptr) {
                ++profile->discardedContactCount;
            }
#endif
            return;
        }
        if (!std::isfinite(renderStartX) || !std::isfinite(renderStartY)
                || !std::isfinite(renderEndX) || !std::isfinite(renderEndY)) {
            skipDegenerateLink();
            return;
        }

        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        bool useContact = false;
        if (matchingChord != nullptr) {
            x1 = static_cast<float>(matchingChord->x1 + cellPositionX);
            y1 = static_cast<float>(matchingChord->y1 + cellPositionY);
            x2 = static_cast<float>(matchingChord->x2 + cellPositionX);
            y2 = static_cast<float>(matchingChord->y2 + cellPositionY);
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float distanceSq = dx * dx + dy * dy;
            useContact = std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2)
                    && std::isfinite(y2) && std::isfinite(distanceSq) && distanceSq > 0.0f;
            if (useContact && distanceSq < nominalLinkWidthSq) {
                const float centerX = 0.5f * (x1 + x2);
                const float centerY = 0.5f * (y1 + y2);
                const float scale = nominalLinkWidth / std::sqrt(distanceSq);
                x1 = centerX + (x1 - centerX) * scale;
                y1 = centerY + (y1 - centerY) * scale;
                x2 = centerX + (x2 - centerX) * scale;
                y2 = centerY + (y2 - centerY) * scale;
                useContact = std::isfinite(x1) && std::isfinite(y1)
                        && std::isfinite(x2) && std::isfinite(y2);
            }
        }
        if (!useContact) {
            const bool boundary = link.isBoundary();
            float axisX = boundary ? cellPositionX : connectedX - cellPositionX;
            float axisY = boundary ? cellPositionY : connectedY - cellPositionY;
            float distanceSq = axisX * axisX + axisY * axisY;
            if (!std::isfinite(distanceSq)) {
                skipDegenerateLink();
                return;
            }
            float centerX = 0.0f;
            float centerY = 0.0f;
            float width = nominalLinkWidth;
            bool radialOrientation = boundary;
            if (boundary && source.environment.dishInnerRadius > 0.0
                    && connectedX * connectedX + connectedY * connectedY < source.environment.dishInnerRadius * source.environment.dishInnerRadius) {
                axisX = -axisX;
                axisY = -axisY;
            }
            if (distanceSq <= minimumFallbackAxisLengthSq) {
                axisX = renderEndX - renderStartX;
                axisY = renderEndY - renderStartY;
                distanceSq = axisX * axisX + axisY * axisY;
                if (!std::isfinite(distanceSq) || distanceSq <= minimumFallbackAxisLengthSq) {
                    skipDegenerateLink();
                    return;
                }
                // Only undefined axes use the visual midpoint and a bounded width.
                centerX = 0.5f * (renderStartX + renderEndX);
                centerY = 0.5f * (renderStartY + renderEndY);
                width = std::min(width, std::sqrt(distanceSq));
                radialOrientation = false;
            } else if (boundary) {
                centerX = 0.5f * (cellPositionX + connectedX);
                centerY = 0.5f * (cellPositionY + connectedY);
            } else {
                const float ratio = link.fallbackCenterRatio;
                centerX = cellPositionX * ratio + connectedX * (1.0f - ratio);
                centerY = cellPositionY * ratio + connectedY * (1.0f - ratio);
            }
            const float scale = 0.5f * width / std::sqrt(distanceSq);
            const float normalX = (radialOrientation ? axisY : -axisY) * scale;
            const float normalY = (radialOrientation ? -axisX : axisX) * scale;
            x1 = centerX + normalX;
            y1 = centerY + normalY;
            x2 = centerX - normalX;
            y2 = centerY - normalY;
        }
        const float crossAxisX = x2 - x1;
        const float crossAxisY = y2 - y1;
        const float crossDistanceSq = crossAxisX * crossAxisX + crossAxisY * crossAxisY;
        if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2)
                || !std::isfinite(y2) || !std::isfinite(crossDistanceSq) || crossDistanceSq <= 0.0f) {
            skipDegenerateLink();
            return;
        }
        auto endpointPatternPosition = [&](float x, float y) {
            return static_cast<float>(-1.0 + 2.0 * ((static_cast<double>(x) - x1) * crossAxisX + (static_cast<double>(y) - y1) * crossAxisY) / crossDistanceSq);
        };
        const float startPattern = endpointPatternPosition(renderStartX, renderStartY);
        const float endPattern = endpointPatternPosition(renderEndX, renderEndY);
        if (!std::isfinite(startPattern) || !std::isfinite(endPattern)) {
            skipDegenerateLink();
            return;
        }
        ensureLinkAppendCapacity();
        auto writeTriangle = [&](std::size_t vertex, float apexX, float apexY, float pattern) {
            const std::size_t offset = 4U * vertex;
            frame.linkTriangleCoords[offset] = apexX;
            frame.linkTriangleCoords[offset + 1] = apexY;
            frame.linkTriangleCoords[offset + 2] = 0.0f;
            frame.linkTriangleCoords[offset + 3] = 1.0f;
            frame.linkTriangleCoords[offset + 4] = x1;
            frame.linkTriangleCoords[offset + 5] = y1;
            frame.linkTriangleCoords[offset + 6] = -1.0f;
            frame.linkTriangleCoords[offset + 7] = 0.0f;
            frame.linkTriangleCoords[offset + 8] = x2;
            frame.linkTriangleCoords[offset + 9] = y2;
            frame.linkTriangleCoords[offset + 10] = 1.0f;
            frame.linkTriangleCoords[offset + 11] = 0.0f;
            frame.linkTextureCoords[vertex] = pattern;
            frame.linkTextureCoords[vertex + 1] = -1.0f;
            frame.linkTextureCoords[vertex + 2] = 1.0f;
        };
        const std::size_t vertex = static_cast<std::size_t>(frame.drawLinksCount);
        writeTriangle(vertex, renderStartX, renderStartY, startPattern);
        writeTriangle(vertex + 3U, renderEndX, renderEndY, endPattern);
        frame.drawLinksCount += 6;
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            if (useContact) {
                ++profile->contactCrossSectionCount;
            } else if (link.isBoundary()) {
                ++profile->boundaryFallbackCount;
            } else {
                ++profile->normalFallbackCount;
            }
        }
#endif
    };

    for (const RenderSourceLink& link : source.candidateLinks) {
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            if (link.isBoundary()) {
                ++profile->candidateBoundaryCount;
            } else {
                ++profile->candidateEdgeCount;
            }
        }
#endif
        appendLinkEdge(link);
    }
}
