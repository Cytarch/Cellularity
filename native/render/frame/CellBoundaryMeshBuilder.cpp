#include "render/frame/CellBoundaryMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "core/CellDataConstants.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderContactLimits.h"
#include "render/frame/RenderSourceFrame.h"

void cellularity::render::CellBoundaryMeshBuilder::reset(
        std::size_t maximumVertexCount, std::size_t maximumIndexCount) noexcept {
    mMaximumVertexCount = maximumVertexCount;
    mMaximumIndexCount = maximumIndexCount;
}

void cellularity::render::CellBoundaryMeshBuilder::build(
        const RenderSourceFrame& source, PreparedRenderFrame& frame,
        RenderContactChordResolver& contactChordResolver,
        RenderContactChordResolver::Profile* contactChordProfile,
        RenderContactChordNormalizationProfile* normalizationProfile) const {
    int size = 0;
    int index = 0;
    const int typeLimit =
            CellTypes::length + (source.environment.previewMode ? 1 : 0);
    // Keep the established draw-bunch partition stable. One Cell can append at
    // most a center plus five vertices per render contact, so the historical
    // 210-vertex guard still safely covers the independent 33-contact bound.
    constexpr int kDrawBunchSafetyVertexMargin = 210;
    static_assert(kDrawBunchSafetyVertexMargin >= 1 + kMaximumRenderContactChordsPerCell * 5,
            "Draw-bunch guard must cover one maximum-contact Cell");
    const int switchBunch = std::numeric_limits<unsigned short>::max() - kDrawBunchSafetyVertexMargin;

    auto baseWritableVertexLimit = [&]() {
        return std::min({mMaximumVertexCount, frame.cellTriangleCoords.size() / 2U,
                frame.cellTextureCoords.size() / 4U,
                frame.cellCenterCoords.size() / 2U,
                frame.cellColors.size() / 4U});
    };
    std::size_t writableVertexCount = baseWritableVertexLimit();
    auto ensureBaseVertexCapacity = [&](std::size_t requiredVertices) {
        if (requiredVertices > mMaximumVertexCount) {
            throw std::length_error(
                    "CellBoundaryMeshBuilder vertex output exceeds its bound");
        }
        const std::size_t requiredTriangleFloats = requiredVertices * 2U;
        const std::size_t requiredAttributeFloats = requiredVertices * 4U;
        if (frame.cellTriangleCoords.size() < requiredTriangleFloats) {
            frame.cellTriangleCoords.resize(requiredTriangleFloats);
        }
        if (frame.cellTextureCoords.size() < requiredAttributeFloats) {
            frame.cellTextureCoords.resize(requiredAttributeFloats);
        }
        if (frame.cellCenterCoords.size() < requiredTriangleFloats) {
            frame.cellCenterCoords.resize(requiredTriangleFloats);
        }
        if (frame.cellColors.size() < requiredAttributeFloats) {
            frame.cellColors.resize(requiredAttributeFloats);
        }
        writableVertexCount = baseWritableVertexLimit();
    };
    auto ensureIndexAppendCapacity = [&](std::size_t additionalIndices) {
        if (index < 0) {
            throw std::length_error(
                    "CellBoundaryMeshBuilder index output exceeds its bound");
        }
        const std::size_t currentIndex = static_cast<std::size_t>(index);
        if (currentIndex > mMaximumIndexCount || additionalIndices > mMaximumIndexCount - currentIndex) {
            throw std::length_error(
                    "CellBoundaryMeshBuilder index output exceeds its bound");
        }
        const std::size_t requiredIndices = currentIndex + additionalIndices;
        if (frame.cellIndices.size() < requiredIndices) {
            frame.cellIndices.resize(requiredIndices);
        }
    };
    auto writeIndex = [&](unsigned short value) {
        frame.cellIndices[static_cast<std::size_t>(index++)] = value;
    };

    for (int cellTypeIndex = 0; cellTypeIndex < typeLimit; cellTypeIndex++) {
        const bool isPreviewArrowBucket =
                cellTypeIndex == kPreviewExtraCellTypeIndex;
        frame.cellVertexCountBefore[cellTypeIndex] = size;
        frame.cellIndexCountBefore[cellTypeIndex] = index;
        int bunch = 0;
        int bunchStart = size;
        frame.bunchStart[cellTypeIndex][bunch] = size;
        frame.bunchIndexStart[cellTypeIndex][bunch] = index;
        frame.bunches[cellTypeIndex] = 1;
        const auto& sourceBucket =
                source
                        .orderedCellRecordIndices[static_cast<std::size_t>(cellTypeIndex)];
        for (const int recordIndex : sourceBucket) {
            const RenderSourceCell& cell =
                    source.cells[static_cast<std::size_t>(recordIndex)];
            const RenderContactChordResolver::View contactChords =
                    contactChordResolver.resolve(
                            source, recordIndex,
                            RenderContactChordResolver::Consumer::CellGeometry,
                            contactChordProfile, normalizationProfile);
            const int contactChordCount = contactChords.count;

            const float red = cell.red;
            const float green = cell.green;
            const float blue = cell.blue;
            const float aux = isPreviewArrowBucket ? cell.previewAux : cell.baseAux;
            const float aux4 =
                    isPreviewArrowBucket ? cell.previewAux4 : cell.baseAux4;
            const float cellAngle = cell.angle;
            const float cellRadius = cell.radius;
            const float cellPositionX = cell.positionX;
            const float cellPositionY = cell.positionY;

            if (size - bunchStart > switchBunch) {
                frame.bunchSize[cellTypeIndex][bunch] =
                        size - frame.bunchStart[cellTypeIndex][bunch];
                frame.bunchIndexSize[cellTypeIndex][bunch] =
                        index - frame.bunchIndexStart[cellTypeIndex][bunch];

                if (bunch + 1 >= kMaximumDrawBunches) {
                    break;
                }

                bunch++;
                frame.bunchStart[cellTypeIndex][bunch] = size;
                frame.bunchIndexStart[cellTypeIndex][bunch] = index;
                bunchStart = size;
            }

            const std::size_t maximumAdditionalVertices =
                    contactChordCount == 0
                    ? 4U
                    : 1U + static_cast<std::size_t>(contactChordCount) * 5U;
            const std::size_t currentVertexCount = static_cast<std::size_t>(size);
            if (currentVertexCount > writableVertexCount || maximumAdditionalVertices > writableVertexCount - currentVertexCount) {
                std::size_t exactAdditionalVertices = contactChordCount == 0 ? 4U : 1U;
                for (int contactChordIndex = 0; contactChordIndex < contactChordCount;
                        ++contactChordIndex) {
                    const RenderContactChord& contactChord =
                            contactChords.data[contactChordIndex];
                    if (contactChord.isDiscarded) {
                        continue;
                    }
                    ++exactAdditionalVertices;
                    if (contactChord.joinsNextChord) {
                        continue;
                    }
                    int next = (contactChordIndex + 1) % contactChordCount;
                    if (contactChords.data[next].isDiscarded) {
                        next = 0;
                    }
                    const RenderContactChord& nextContactChord = contactChords.data[next];
                    exactAdditionalVertices +=
                            contactChord.x2 * nextContactChord.y1 - nextContactChord.x1 * contactChord.y2 > 0.0
                            ? 3U
                            : 4U;
                }
                if (currentVertexCount > mMaximumVertexCount || exactAdditionalVertices > mMaximumVertexCount - currentVertexCount) {
                    throw std::length_error(
                            "CellBoundaryMeshBuilder vertex output exceeds its bound");
                }
                ensureBaseVertexCapacity(currentVertexCount + exactAdditionalVertices);
            }

            auto writeVertex = [&](float localX, float localY, float textureRadius) {
                frame.cellTriangleCoords[2 * size] = localX;
                frame.cellTriangleCoords[2 * size + 1] = localY;
                frame.cellTextureCoords[4 * size] = aux;
                frame.cellTextureCoords[4 * size + 1] = textureRadius;
                frame.cellTextureCoords[4 * size + 2] = cellAngle;
                frame.cellTextureCoords[4 * size + 3] = aux4;
                frame.cellCenterCoords[2 * size] = cellPositionX;
                frame.cellCenterCoords[2 * size + 1] = cellPositionY;
                frame.cellColors[4 * size] = red;
                frame.cellColors[4 * size + 1] = green;
                frame.cellColors[4 * size + 2] = blue;
                frame.cellColors[4 * size + 3] = cellRadius;
                size++;
            };

            if (contactChordCount == 0) {
                const int start = size - bunchStart;
                writeVertex(cellRadius, cellRadius, 0.0f);
                writeVertex(cellRadius, -cellRadius, 0.0f);
                writeVertex(-cellRadius, -cellRadius, 0.0f);
                writeVertex(-cellRadius, cellRadius, 0.0f);

                ensureIndexAppendCapacity(6U);
                writeIndex(static_cast<unsigned short>(start));
                writeIndex(static_cast<unsigned short>(start + 1));
                writeIndex(static_cast<unsigned short>(start + 2));

                writeIndex(static_cast<unsigned short>(start));
                writeIndex(static_cast<unsigned short>(start + 2));
                writeIndex(static_cast<unsigned short>(start + 3));
            } else {
                const int centerIndex = size;
                const int centerLocalIndex = centerIndex - bunchStart;
                writeVertex(0.0f, 0.0f, 0.0f);
                const float edgeTextureRadius =
                        isPreviewArrowBucket ? 0.0f : cellRadius;

                std::array<double, 9> xs{};
                std::array<double, 9> ys{};
                constexpr double sqrt3o4 = 0.4330127018922193;

                for (int contactChordIndex = 0; contactChordIndex < contactChordCount;
                        contactChordIndex++) {
                    const RenderContactChord& contactChord =
                            contactChords.data[contactChordIndex];
                    if (contactChord.isDiscarded) {
                        continue;
                    }

                    writeVertex(static_cast<float>(contactChord.x1),
                            static_cast<float>(contactChord.y1), edgeTextureRadius);

                    if (!contactChord.joinsNextChord) {
                        int next = (contactChordIndex + 1) % contactChordCount;
                        if (contactChords.data[next].isDiscarded) {
                            next = 0;
                        }
                        const RenderContactChord& nextContactChord =
                                contactChords.data[next];

                        if (contactChord.x2 * nextContactChord.y1 - nextContactChord.x1 * contactChord.y2 > 0.0f) {
                            xs[0] = contactChord.x2;
                            ys[0] = contactChord.y2;
                            xs[3] = nextContactChord.x1;
                            ys[3] = nextContactChord.y1;

                            double px = xs[0] + xs[3];
                            double py = ys[0] + ys[3];
                            double d = cellRadius / std::sqrt(px * px + py * py);
                            px *= d;
                            py *= d;
                            d = cellRadius * cellRadius / (ys[0] * px - xs[0] * py);
                            xs[1] = (ys[0] - py) * d;
                            ys[1] = -(xs[0] - px) * d;
                            xs[2] = -(ys[3] - py) * d;
                            ys[2] = (xs[3] - px) * d;

                            for (int k = 0; k < 3; k++) {
                                writeVertex(static_cast<float>(xs[k]),
                                        static_cast<float>(ys[k]), edgeTextureRadius);
                            }
                        } else {
                            xs[0] = contactChord.x2;
                            ys[0] = contactChord.y2;
                            xs[4] = nextContactChord.x1;
                            ys[4] = nextContactChord.y1;

                            xs[2] = xs[0] + xs[4];
                            ys[2] = ys[0] + ys[4];
                            double d =
                                    -2.0 * cellRadius / std::sqrt(xs[2] * xs[2] + ys[2] * ys[2]);
                            xs[2] *= d;
                            ys[2] *= d;
                            double px = xs[2] * 0.25 + ys[2] * sqrt3o4;
                            double py = ys[2] * 0.25 - xs[2] * sqrt3o4;

                            d = cellRadius * cellRadius / (ys[0] * px - xs[0] * py);
                            xs[1] = (ys[0] - py) * d;
                            ys[1] = (px - xs[0]) * d;

                            px = xs[2] * 0.25 - ys[2] * sqrt3o4;
                            py = ys[2] * 0.25 + xs[2] * sqrt3o4;
                            d = cellRadius * cellRadius / (ys[4] * px - xs[4] * py);
                            xs[3] = (ys[4] - py) * d;
                            ys[3] = (px - xs[4]) * d;

                            for (int k = 0; k < 4; k++) {
                                writeVertex(static_cast<float>(xs[k]),
                                        static_cast<float>(ys[k]), edgeTextureRadius);
                            }
                        }
                    }
                }

                const std::size_t cellVertexCount =
                        static_cast<std::size_t>(size - centerIndex);
                const std::size_t triangleCount =
                        std::max<std::size_t>(1U, cellVertexCount - 1U);
                ensureIndexAppendCapacity(triangleCount * 3U);
                for (int k = centerLocalIndex + 1; k < size - bunchStart - 1; k++) {
                    writeIndex(static_cast<unsigned short>(centerLocalIndex));
                    writeIndex(static_cast<unsigned short>(k));
                    writeIndex(static_cast<unsigned short>(k + 1));
                }
                writeIndex(static_cast<unsigned short>(centerLocalIndex));
                writeIndex(static_cast<unsigned short>(size - bunchStart - 1));
                writeIndex(static_cast<unsigned short>(centerLocalIndex + 1));
            }
        }
        frame.bunchSize[cellTypeIndex][bunch] =
                size - frame.bunchStart[cellTypeIndex][bunch];
        frame.bunchIndexSize[cellTypeIndex][bunch] =
                index - frame.bunchIndexStart[cellTypeIndex][bunch];
        frame.bunches[cellTypeIndex] = bunch + 1;
        frame.cellVertexCount[cellTypeIndex] =
                size - frame.cellVertexCountBefore[cellTypeIndex];
        frame.cellIndexCount[cellTypeIndex] =
                index - frame.cellIndexCountBefore[cellTypeIndex];
    }
    frame.cellDrawVertexCount = size;
    frame.cellDrawIndexCount = index;
    frame.cellCenterFloatCount = size * 2;
}
