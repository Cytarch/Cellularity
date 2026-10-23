#include "formats/LegacySubstrateCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Gene.h"
#include "core/SimulationMemoryPolicy.h"
#include "core/SimulationRandom.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/LegacyEnvironmentCodec.h"
#include "formats/LegacyFormatConstants.h"
#include "formats/LegacyGeneCodec.h"
#include "formats/SnapshotCodecSupport.h"
#include "formats/SnapshotFileIO.h"
#include "io/JavaObjectStreamReader.h"

namespace cellularity::snapshot_codec {
namespace {

struct LegacyLinkData {
    int connectedCellIndex = 0;
    double angleA = 0.0;
    double angleB = 0.0;
    double positionX = 0.0;
    double positionY = 0.0;
    float torsionalStiffness = 0.0f;
    float restLength = 0.0f;
};

struct SerializedTopologyEdge {
    int cellA = 0;
    int cellB = 0;
    double angleA = 0.0;
    double angleB = 0.0;
    float torsionalStiffness = 0.0f;
    float restLength = 0.0f;
};

struct SerializedTopologyBoundary {
    int cell = 0;
    double angleA = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    float torsionalStiffness = 0.0f;
    float restLength = 0.0f;
};

LegacyLinkData readLegacyLink(JavaObjectStreamReader& input, int version) {
    LegacyLinkData link;
    link.connectedCellIndex = input.readInt();
    link.angleA = input.readDouble();
    link.angleB = input.readDouble();
    input.readBoolean();
    link.positionX = input.readDouble();
    link.positionY = input.readDouble();
    if (version >= 4) {
        link.torsionalStiffness = version >= 7
                ? input.readFloat()
                : static_cast<float>(input.readDouble());
    }
    if (version >= 16) {
        link.restLength = input.readFloat();
    }
    return link;
}

std::array<LegacyLinkData, CellData::MAX_NEIGHBORS> readLegacyLinks(
        JavaObjectStreamReader& input,
        int cellVersion) {
    const int readNeighbors = input.readInt();
    validateSerializedCount(
            readNeighbors, CellData::MAX_NEIGHBORS, "legacy Cell neighbor");
    std::array<LegacyLinkData, CellData::MAX_NEIGHBORS> links{};
    for (int index = 0; index < readNeighbors; index++) {
        links[index] = readLegacyLink(input, cellVersion);
    }
    return links;
}

struct LegacySubstrateHeaderData {
    int version;
    double time;
    int cellCount;
    Environment environment;
};

LegacySubstrateHeaderData readLegacySubstrateHeaderData(
        JavaObjectStreamReader& input,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    const int version = input.readInt();
    if (version > legacy_format::kMaximumProductVersion) {
        failDecode(DecodeErrorCode::NewerProductVersion,
                "Unsupported newer legacy substrate product version");
    }
    if (version < legacy_format::kMinimumProductVersion) {
        failDecode(DecodeErrorCode::InvalidVersion,
                "Invalid legacy substrate product version");
    }
    const double time = input.readDouble();
    requireFinite(time, "Legacy substrate time");
    const int cellCount = input.readInt();
    Environment environment = readLegacyEnvironment(
            input, previewMode, fallbackEnvironment);
    validateEnvironmentForAllocation(environment);
    if (cellCount < 0 || cellCount > environment.maxCellCount) {
        if (cellCount < 0) {
            failDecode(DecodeErrorCode::InvalidCount,
                    "Negative legacy substrate Cell count");
        }
        failDecode(DecodeErrorCode::LimitExceeded,
                "Legacy substrate Cell count exceeds the Environment limit");
    }
    return {version, time, cellCount, std::move(environment)};
}

void addLegacyTopologyDirect(
        CellData& cells,
        const std::vector<SerializedTopologyEdge>& edges,
        const std::vector<SerializedTopologyBoundary>& boundaries) {
    cells.mTopology.clearAll();
    for (const SerializedTopologyEdge& edge : edges) {
        for (double value : {
                     edge.angleA,
                     edge.angleB,
                     static_cast<double>(edge.torsionalStiffness),
                     static_cast<double>(edge.restLength)
             }) {
            requireFinite(value, "Legacy topology edge value");
        }
        const int edgeIndex = cells.mTopology.addCellLink(
                edge.cellA,
                edge.cellB,
                CellLinkCreationReason::UNSPECIFIED,
                edge.angleA,
                edge.angleB,
                edge.torsionalStiffness,
                edge.restLength);
        if (edgeIndex < 0) {
            failDecode(
                    DecodeErrorCode::InvalidValue,
                    "Invalid legacy topology edge adjacency");
        }
        cells.mTopology.setEdgeRenderOffsets(
                edgeIndex, 0.0f, 0.0f, 0.0f, 0.0f);
    }
    for (const SerializedTopologyBoundary& boundary : boundaries) {
        for (double value : {
                     boundary.angleA,
                     boundary.anchorX,
                     boundary.anchorY,
                     static_cast<double>(boundary.torsionalStiffness),
                     static_cast<double>(boundary.restLength)
             }) {
            requireFinite(value, "Legacy topology boundary value");
        }
        const int boundaryIndex = cells.mTopology.addBoundaryLink(
                boundary.cell,
                boundary.angleA,
                boundary.anchorX,
                boundary.anchorY,
                boundary.torsionalStiffness,
                boundary.restLength);
        if (boundaryIndex < 0) {
            failDecode(
                    DecodeErrorCode::InvalidValue,
                    "Invalid legacy topology boundary adjacency");
        }
        cells.mTopology.setBoundaryRenderStartOffset(
                boundaryIndex, 0.0f, 0.0f);
    }
    if (!cells.mTopology.validate()) {
        failDecode(
                DecodeErrorCode::InvalidValue,
                "Invalid legacy topology adjacency");
    }
}

void readLegacyCellsDirect(
        JavaObjectStreamReader& input,
        CellData& cells,
        int cellCount) {
    cells.replaceActiveCellCount(cellCount);
    std::vector<SerializedTopologyEdge> topologyEdges;
    std::vector<SerializedTopologyBoundary> topologyBoundaries;
    for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
        const int cellVersion = input.readInt();
        if (cellVersion > legacy_format::kMaximumProductVersion) {
            failDecode(
                    DecodeErrorCode::NewerProductVersion,
                    "Unsupported newer legacy Cell product version");
        }
        if (cellVersion < legacy_format::kMinimumCellProductVersion) {
            failDecode(
                    DecodeErrorCode::InvalidVersion,
                    "Invalid legacy Cell product version");
        }

        const double positionX = input.readDouble();
        const double positionY = input.readDouble();
        const double angle = input.readDouble();
        const double angleCytoplasm = input.readDouble();
        const double velocityX = input.readDouble();
        const double velocityY = input.readDouble();
        const double angularVelocity = input.readDouble();
        const double angularVelocityCytoplasm = input.readDouble();
        const double radius = input.readDouble();
        const double mass = input.readDouble();
        const double age = input.readDouble();
        const auto links = readLegacyLinks(input, cellVersion);
        const int linkCount = input.readInt();
        validateSerializedCount(
                linkCount, CellData::MAX_NEIGHBORS, "legacy Cell link");
        const bool dead = input.readBoolean();
        const float color0 = input.readFloat();
        const float color1 = input.readFloat();
        const float color2 = input.readFloat();
        const auto genome = readLegacyGenome(input, cellVersion);
        const int geneIndex = input.readInt();
        if (cellVersion >= legacy_format::kObsoleteExtensionProductVersion) {
            (void)input.readInt();
        }
        const int originTag = input.readInt();
        const int selectTag = input.readInt();
        const double hairRotation = input.readDouble();
        const double hairX = input.readDouble();
        const double hairY = input.readDouble();
        const double nitrates = input.readDouble();
        const bool mirrored = cellVersion >= 4
                ? input.readBoolean()
                : false;

        const int seldomCounter =
                SimulationRandom::global.nextInt(CellData::MUTATE_PERIOD);
        float lipids = 0.0f;
        int telomeresCount = -1;
        double positionZ = 0.0;
        std::array<float, CellData::SIGNAL_SUBSTANCE_COUNT * 2>
                signalSubstance{};
        std::array<float, CellData::SIGNAL_SUBSTANCE_COUNT>
                signalSubstanceOut{};
        float toxin = 0.0f;
        float toxication = 0.0f;
        float myocyteContraction = 0.0f;
        float myocyteBending = 0.0f;
        float myocyteLifting = 0.0f;
        if (cellVersion >= 9) {
            for (int index = 0; index < CellData::SIGNAL_SUBSTANCE_COUNT;
                    ++index) {
                signalSubstance[index] = input.readFloat();
                signalSubstanceOut[index] = input.readFloat();
            }
            toxin = input.readFloat();
            toxication = input.readFloat();
            myocyteContraction = input.readFloat();
            myocyteBending = input.readFloat();
            myocyteLifting = input.readFloat();
            if (cellVersion >= 12) {
                lipids = input.readFloat();
            }
        } else {
            signalSubstance.fill(CellData::SIGNAL_SUBSTANCE_INITIAL);
            signalSubstanceOut.fill(CellData::SIGNAL_SUBSTANCE_INITIAL);
        }
        const int mutationCount = cellVersion >= 15
                ? input.readInt()
                : 0;
        if (cellVersion >= 19) {
            telomeresCount = input.readInt();
        }
        if (cellVersion >= 23) {
            positionZ = input.readDouble();
        }

        cells.mPositionX[cellIndex] = positionX;
        cells.mPositionY[cellIndex] = positionY;
        cells.mPositionZ[cellIndex] = positionZ;
        cells.mRadius[cellIndex] = radius;
        cells.refreshRadiusSq(cellIndex);
        cells.mVelocityX[cellIndex] = velocityX;
        cells.mVelocityY[cellIndex] = velocityY;
        cells.mForceX[cellIndex] = 0.0;
        cells.mForceY[cellIndex] = 0.0;
        cells.mForceZ[cellIndex] = 0.0;
        cells.mTorque[cellIndex] = 0.0;
        cells.mAngle[cellIndex] = angle;
        cells.mAngleCytoplasm[cellIndex] = angleCytoplasm;
        cells.mAngularVelocity[cellIndex] = angularVelocity;
        cells.mAngularVelocityCytoplasm[cellIndex] =
                angularVelocityCytoplasm;
        cells.mAge[cellIndex] = age;
        cells.mNitrates[cellIndex] = nitrates;
        cells.mSwimAnimationTime[cellIndex] = 0.0f;
        cells.mSwimAnimationTime2[cellIndex] = 0.0f;
        cells.mHairRotation[cellIndex] = hairRotation;
        cells.hairX[cellIndex] = hairX;
        cells.hairY[cellIndex] = hairY;
        cells.mMass[cellIndex] = mass;
        cells.mIsDead[cellIndex] = dead;
        cells.mIsMirrored[cellIndex] = mirrored;
        cells.mIsStuck[cellIndex] = false;
        cells.mOriginTag[cellIndex] = originTag;
        cells.mSelectTag[cellIndex] = selectTag;
        cells.mMutationCount[cellIndex] = mutationCount;
        cells.mPendingMutationCount[cellIndex] = 0;
        cells.mSeldomCounter[cellIndex] = seldomCounter;
        cells.mOldLinkCount[cellIndex] = 0;
        cells.mLipids[cellIndex] = lipids;
        cells.mTelomeresCount[cellIndex] = telomeresCount;
        cells.mColor[cellIndex] = {color0, color1, color2, 0.0f};
        if (geneIndex < 0
                || geneIndex >= static_cast<int>(genome.size())
                || !cells.replaceGenome(
                        cellIndex,
                        genome,
                        cellularity::GeneIndex(static_cast<
                                cellularity::GeneIndex::Value>(geneIndex)))) {
            failDecode(
                    DecodeErrorCode::InvalidValue,
                    "Legacy Cell Genome cannot be imported");
        }
        cells.mSignalSubstance[cellIndex] = signalSubstance;
        cells.mNewSignalSubstance[cellIndex].fill(0.0f);
        cells.mSignalSubstanceOut[cellIndex] = signalSubstanceOut;
        cells.mToxin[cellIndex] = toxin;
        cells.mToxication[cellIndex] = toxication;
        cells.mNewToxin[cellIndex] = 0.0f;
        cells.mMyocyteContraction[cellIndex] = myocyteContraction;
        cells.mMyocyteBending[cellIndex] = myocyteBending;
        cells.mMyocyteLifting[cellIndex] = myocyteLifting;
        cells.mNewMass[cellIndex] = mass;
        cells.mNewNitrates[cellIndex] = nitrates;
        cells.mFertilizationTarget[cellIndex] = -1;
        cells.clearCellCollisionContacts(cellIndex);

        const int copiedLinkCount =
                std::min(std::max(linkCount, 0), CellData::MAX_NEIGHBORS);
        for (int linkIndex = 0; linkIndex < copiedLinkCount; ++linkIndex) {
            const LegacyLinkData& link = links[linkIndex];
            if (link.connectedCellIndex
                    == CellData::BOUNDARY_CONNECTION_INDEX) {
                topologyBoundaries.push_back({cellIndex,
                        link.angleA,
                        link.positionX,
                        link.positionY,
                        link.torsionalStiffness,
                        link.restLength});
            } else if (link.connectedCellIndex > cellIndex
                    && link.connectedCellIndex < cellCount) {
                topologyEdges.push_back({cellIndex,
                        link.connectedCellIndex,
                        link.angleA,
                        link.angleB,
                        link.torsionalStiffness,
                        link.restLength});
            }
        }
    }
    cells.updateAllCellAngleDirectionCaches();
    addLegacyTopologyDirect(cells, topologyEdges, topologyBoundaries);
}

void readLegacyFoodDirect(
        JavaObjectStreamReader& input,
        FoodData& food,
        int version) {
    const int foodCount = input.readInt();
    validateSerializedCount(
            foodCount, food.MAX_FOOD_COUNT, "legacy substrate food");
    food.replaceActiveFoodCount(foodCount);
    for (int foodIndex = 0; foodIndex < foodCount; ++foodIndex) {
        if (version >= 8) {
            food.mPositionX[foodIndex] = input.readFloat();
            food.mPositionY[foodIndex] = input.readFloat();
            food.mMass[foodIndex] = input.readFloat();
        } else {
            food.mPositionX[foodIndex] =
                    static_cast<float>(input.readDouble());
            food.mPositionY[foodIndex] =
                    static_cast<float>(input.readDouble());
            food.mMass[foodIndex] = static_cast<float>(input.readDouble());
        }
        food.mVelocityX[foodIndex] = 0.0f;
        food.mVelocityY[foodIndex] = 0.0f;
        food.mLipidLayer[foodIndex] = 0.0f;
        if (version >= 22) {
            food.mVelocityX[foodIndex] = input.readFloat();
            food.mVelocityY[foodIndex] = input.readFloat();
            food.mLipidLayer[foodIndex] = input.readFloat();
        }
    }
}

std::unique_ptr<SimulationWorld> readLegacyWorldDirect(
        JavaObjectStreamReader& bodyInput,
        LegacySubstrateHeaderData& header,
        bool previewMode) {
    if (header.version >= 24) {
        header.environment.mLightAngle = bodyInput.readDouble();
    }
    header.environment.mLightAngleCos = std::cos(
            header.environment.mLightAngle);
    header.environment.mLightAngleSin = std::sin(
            header.environment.mLightAngle);
    header.environment.mRandom.setSeed(
            header.version >= 96 ? bodyInput.readLong() : 95L);

    auto world = std::make_unique<SimulationWorld>(
            previewMode, header.environment);
    world->mTime = header.time;
    readLegacyCellsDirect(
            bodyInput, world->mCellData, header.cellCount);
    readLegacyFoodDirect(
            bodyInput, world->mFoodData, header.version);
    if (bodyInput.hasRemainingBytes()) {
        failDecode(
                DecodeErrorCode::TrailingData,
                "Unexpected trailing legacy substrate data");
    }
    validateDecodedWorldDynamicValues(*world);
    world->updateGrids();
    return world;
}

} // namespace

LegacySubstrateLoadResult loadLegacySubstrateBytesDirect(
        std::vector<std::uint8_t> bytes,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    JavaObjectStreamReader headerInput(std::move(bytes));
    LegacySubstrateHeaderData header = readLegacySubstrateHeaderData(
            headerInput, previewMode, fallbackEnvironment);
    std::unique_ptr<JavaObjectStreamReader> gzipBodyInput;
    JavaObjectStreamReader* bodyInput = &headerInput;
    if (header.version >= 13) {
        const cellularity::memory::RuntimeLimits runtimeLimits =
                cellularity::memory::currentRuntimeLimits();
        std::vector<std::uint8_t> bodyBytes = inflateGzipBytes(
                headerInput.remainingBytes(),
                runtimeLimits.operationalSubstrateFileBytes);
        gzipBodyInput =
                std::make_unique<JavaObjectStreamReader>(std::move(bodyBytes));
        bodyInput = gzipBodyInput.get();
    }
    const int version = header.version;
    return {
            readLegacyWorldDirect(*bodyInput, header, previewMode),
            version
    };
}

LegacySubstrateLoadResult loadLegacySubstratePathDirect(
        const std::string& inputPath,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    SnapshotInputFile source = openSnapshotFile(inputPath);
    JavaObjectStreamReader headerInput(source.stream, source.sizeBytes);
    LegacySubstrateHeaderData header = readLegacySubstrateHeaderData(
            headerInput, previewMode, fallbackEnvironment);
    const int version = header.version;

    const cellularity::memory::RuntimeLimits runtimeLimits =
            cellularity::memory::currentRuntimeLimits();
    std::unique_ptr<TemporarySnapshotFile> expandedFile;
    std::unique_ptr<SnapshotInputFile> expandedSource;
    std::unique_ptr<JavaObjectStreamReader> expandedInput;
    JavaObjectStreamReader* bodyInput = &headerInput;
    if (header.version >= 13) {
        headerInput.finishPrimitiveSection();
        expandedFile = std::make_unique<TemporarySnapshotFile>(inputPath);
        std::ofstream expandedOutput(
                expandedFile->path(), std::ios::binary | std::ios::trunc);
        if (!expandedOutput) {
            failDecode(
                    DecodeErrorCode::IoFailure,
                    "Could not open temporary legacy substrate output");
        }
        (void)inflateGzipStream(
                source.stream,
                expandedOutput,
                runtimeLimits.operationalSubstrateFileBytes);
        expandedOutput.close();
        if (!expandedOutput) {
            failDecode(
                    DecodeErrorCode::IoFailure,
                    "Could not close temporary legacy substrate output");
        }
        source.stream.close();
        expandedSource = std::make_unique<SnapshotInputFile>(
                openSnapshotFile(expandedFile->path()));
        expandedInput = std::make_unique<JavaObjectStreamReader>(
                expandedSource->stream, expandedSource->sizeBytes);
        bodyInput = expandedInput.get();
    }
    return {
            readLegacyWorldDirect(*bodyInput, header, previewMode),
            version
    };
}

NativeSubstrateInspection inspectLegacySubstrateHeader(
        std::vector<std::uint8_t> bytes,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    JavaObjectStreamReader input(std::move(bytes));
    const int productVersion = input.readInt();
    if (productVersion > legacy_format::kMaximumProductVersion) {
        failDecode(DecodeErrorCode::NewerProductVersion,
                "Unsupported newer legacy substrate product version");
    }
    if (productVersion < legacy_format::kMinimumProductVersion) {
        failDecode(DecodeErrorCode::InvalidVersion,
                "Invalid legacy substrate product version");
    }
    const double time = input.readDouble();
    requireFinite(time, "Legacy substrate time");
    const int cellCount = input.readInt();
    if (cellCount < 0) {
        failDecode(DecodeErrorCode::InvalidCount,
                "Negative legacy substrate Cell count");
    }
    Environment environment = readLegacyEnvironment(
            input, previewMode, fallbackEnvironment);
    validateEnvironmentForAllocation(environment);
    if (cellCount > environment.maxCellCount) {
        failDecode(DecodeErrorCode::LimitExceeded,
                "Legacy substrate Cell count exceeds the Environment limit");
    }
    NativeSubstrateInspection inspection;
    inspection.familyLocalVersion = productVersion;
    inspection.time = time;
    inspection.cellCount = cellCount;
    inspection.environment = std::move(environment);
    inspection.formatFamily = NativeSubstrateFormatFamily::OriginalJava;
    return inspection;
}

} // namespace cellularity::snapshot_codec
