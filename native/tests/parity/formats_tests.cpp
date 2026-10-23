#include "core/CellData.h"
#include "core/CellSweepBroadphase.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/RenderContactChord.h"
#include "core/RendererMemoryModel.h"
#include "core/SimulationMemoryPolicy.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/LegacySubstrateCodec.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "io/JavaObjectStreamReader.h"
#include "io/NativeByteStream.h"
#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactCollector.h"
#include "render/frame/RenderContactLimits.h"
#include "tests/support/FormatTestData.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cellularity::tests;

namespace {

void testEnvironmentAllocationValidation() {
    Environment environment = Environment::constructDefaultEnvironment(1000, 3500, 1.0);
    cellularity::snapshot_codec::validateEnvironmentForAllocation(environment);

    Environment invalid = environment;
    invalid.maxCellCount = 0;
    assertThrows(
            [&invalid] {
                cellularity::snapshot_codec::validateEnvironmentForAllocation(
                        invalid);
            },
            "Environment allocation accepts zero cells");

    invalid = environment;
    invalid.maxFoodCount = -1;
    assertThrows(
            [&invalid] {
                cellularity::snapshot_codec::validateEnvironmentForAllocation(
                        invalid);
            },
            "Environment allocation accepts negative food capacity");

    invalid = environment;
    invalid.mRadius = std::numeric_limits<double>::quiet_NaN();
    assertThrows(
            [&invalid] {
                cellularity::snapshot_codec::validateEnvironmentForAllocation(
                        invalid);
            },
            "Environment allocation accepts NaN radius");

    invalid = environment;
    invalid.maxCellCount = 1000000;
    assertThrows(
            [&invalid] {
                cellularity::snapshot_codec::validateEnvironmentForAllocation(
                        invalid);
            },
            "Environment allocation accepts a world over 2 GiB");
}

void testRendererMemoryIsCheckedAndIncludedInAdmissionEstimate() {
    cellularity::memory::RendererInitialBufferLayout layout;
    assertTrue(cellularity::memory::tryBuildRendererInitialBufferLayout(1000, layout),
            "Renderer layout accepts the default Cell capacity");
    assertTrue(layout.sourceOrderedCellEntriesPerBucket == 1000,
            "Renderer layout bounds each source-order bucket from Cell capacity");
    assertTrue(layout.cellVisitEntries == 1000
                    && layout.sourceRecordIndexEntries == 1000
                    && layout.sourceRecordGenerationEntries == 1000,
            "Renderer layout sizes capture scratch from Cell capacity");
    assertTrue(layout.cellTriangleFloatEntries == 224000,
            "Renderer layout preserves the current initial triangle allocation");
    assertTrue(layout.cellTextureFloatEntries == 448000,
            "Renderer layout preserves the current initial texture allocation");
    assertTrue(layout.cellIndexEntries == 168000,
            "Renderer layout preserves the current initial index allocation");
    assertTrue(layout.maximumNormalizedContactChordEntries
                    == 1000U * cellularity::render::kMaximumRenderContactChordsPerCell,
            "Renderer layout bounds compact normalized walls");
    assertTrue(layout.maximumRawContactChordEntries
                    == 1000U * cellularity::render::kMaximumRenderContactChordsPerCell,
            "Renderer layout independently bounds sparse raw walls");
    assertTrue(layout.maximumNormalEdgeCount == 1000U * CellData::MAX_NEIGHBORS / 2U
                    && layout.linkCandidateWordEntries == (layout.maximumNormalEdgeCount + 63U) / 64U,
            "Renderer candidate bitset stays bounded by normal topology edges");
    assertTrue(layout.maximumDrawableLinkCount == 1000U * CellData::MAX_NEIGHBORS
                    && layout.maximumLinkVertexCount == layout.maximumDrawableLinkCount * 6U,
            "Renderer output includes every legal normal and boundary Link");
    assertTrue(!cellularity::memory::tryBuildRendererInitialBufferLayout(0, layout),
            "Renderer layout rejects zero Cell capacity");
    assertTrue(!cellularity::memory::tryBuildRendererInitialBufferLayout(
                       std::numeric_limits<int>::max(), layout),
            "Renderer layout rejects upload-range arithmetic overflow");

    Environment environment = Environment::constructDefaultEnvironment(1000, 3500, 1.0);
    cellularity::memory::RendererMemoryEstimate rendererEstimate;
    assertTrue(cellularity::memory::tryEstimateRendererMemory(
                       environment.maxCellCount,
                       environment.maxFoodCount,
                       64,
                       rendererEstimate),
            "Renderer memory estimate accepts the default configuration");
    assertTrue(rendererEstimate.initialPayloadBytes > 0,
            "Renderer memory estimate includes reset payload");
    assertTrue(rendererEstimate.rendererEmbeddedStateBytes > 0,
            "Renderer memory estimate includes inline renderer tables and vector controls");
    assertTrue(rendererEstimate.ghostSnapshotEmbeddedStateBytes > 0,
            "Renderer memory estimate includes inline Ghost snapshot state");
    assertTrue(rendererEstimate.maximumReusablePayloadBytes
                    > rendererEstimate.initialPayloadBytes,
            "Renderer memory estimate includes bounded high-water payload");
    assertTrue(rendererEstimate.maximumGhostSnapshotPayloadBytes > 0,
            "Renderer memory estimate includes Ghost snapshot payload");
    assertTrue(rendererEstimate.budgetedBytes
                    > rendererEstimate.maximumReusablePayloadBytes,
            "Renderer memory estimate includes vector growth headroom");

    const cellularity::memory::WorldAllocationEstimate worldEstimate =
            cellularity::memory::estimateWorldAllocation(environment, 64);
    assertTrue(worldEstimate.rendererBudgetedBytes == rendererEstimate.budgetedBytes,
            "World admission estimate exposes the included renderer budget");
    assertTrue(worldEstimate.bytes > worldEstimate.rendererBudgetedBytes,
            "World admission estimate combines simulation and renderer storage");

    const Environment representativeLargeEnvironment =
            Environment::constructDefaultEnvironment(4000, 7500, 4.0);
    const cellularity::memory::WorldAllocationEstimate representative64 =
            cellularity::memory::estimateWorldAllocation(
                    representativeLargeEnvironment, 64);
    assertTrue(representative64.bytes <= representative64.budgetBytes,
            "Renderer headroom is counted twice for a supported 64-bit world");
    const cellularity::memory::WorldAllocationEstimate representative32 =
            cellularity::memory::estimateWorldAllocation(
                    representativeLargeEnvironment, 32);
    assertTrue(representative32.bytes > representative32.budgetBytes,
            "Representative large world unexpectedly bypasses 32-bit admission");
}

void testSyntheticLegacySubstrateStillLoads() {
    constexpr int legacyVersion = 24;
    ByteWriter headerPayload;
    headerPayload.writeInt(legacyVersion);
    headerPayload.writeDouble(2.5);
    headerPayload.writeInt(0);

    headerPayload.writeInt(legacyVersion);
    headerPayload.writeDouble(0.2);
    headerPayload.writeDouble(0.1);
    headerPayload.writeDouble(0.0);
    headerPayload.writeDouble(1.0);
    headerPayload.writeDouble(1.0);
    headerPayload.writeDouble(1.0);
    headerPayload.writeDouble(60.0);
    headerPayload.writeInt(1);
    headerPayload.writeBool(true);
    headerPayload.writeBool(false);
    headerPayload.writeDouble(0.0);
    headerPayload.writeDouble(0.0);
    headerPayload.writeDouble(0.0);
    headerPayload.writeBool(false);
    headerPayload.writeDouble(1.0);
    headerPayload.writeInt(8);
    headerPayload.writeInt(12);
    headerPayload.writeDouble(1.0);
    headerPayload.writeDouble(0.0);
    headerPayload.writeDouble(0.0);
    headerPayload.writeBool(false);
    headerPayload.writeFloat(0.5f);
    headerPayload.writeBool(true);
    headerPayload.writeDouble(0.0);
    headerPayload.writeDouble(0.0);
    headerPayload.writeBool(true);
    headerPayload.writeFloat(0.0f);

    ByteWriter bodyPayload;
    bodyPayload.writeDouble(0.25);
    bodyPayload.writeInt(0);
    std::vector<uint8_t> body = gzipBytes(javaPrimitiveStream(bodyPayload.bytes()));

    std::vector<uint8_t> legacy = javaPrimitiveStream(headerPayload.bytes());
    legacy.insert(legacy.end(), body.begin(), body.end());

    Environment fallback = Environment::constructDefaultEnvironment(4, 4, 1.0);
    cellularity::snapshot_codec::LegacySubstrateLoadResult loaded =
            cellularity::snapshot_codec::loadLegacySubstrateBytesDirect(
                    std::move(legacy), false, fallback);
    assertTrue(loaded.productVersion == legacyVersion,
            "Synthetic legacy substrate version");

    std::unique_ptr<SimulationWorld> decodedWorld = std::move(loaded.world);
    const SimulationWorld& world = *decodedWorld;
    assertNear(2.5, world.mTime, 0.0, "Synthetic legacy substrate time");
    assertTrue(world.mEnvironment.maxCellCount == 8,
            "Synthetic legacy substrate cell capacity");
    assertTrue(world.mEnvironment.maxFoodCount == 12,
            "Synthetic legacy substrate food capacity");
    assertTrue(world.mCellData.mCellCount == 0,
            "Synthetic legacy substrate cell count");
    assertTrue(world.mFoodData.mFoodCount == 0,
            "Synthetic legacy substrate food count");
}

void testReleaseSnapshotRoundTripsTopologyLinks() {
    Environment environment = Environment::constructDefaultEnvironment(16, 0, 1.0);
    SimulationWorld sourceWorld(false, environment);
    CellData& cellData = sourceWorld.mCellData;
    int first = addAliveCell(cellData, CellType::PHAGOCYTE);
    int second = addAliveCell(cellData, CellType::KERATINOCYTE);
    int boundaryCell = addAliveCell(cellData, CellType::GLUEOCYTE);
    cellData.mPositionX[first] = 0.0;
    cellData.mPositionY[first] = 0.0;
    cellData.mRadius[first] = 0.03;
    cellData.refreshRadiusSq(first);
    cellData.mPositionX[second] = 0.04;
    cellData.mPositionY[second] = 0.0;
    cellData.mRadius[second] = 0.03;
    cellData.refreshRadiusSq(second);
    cellData.mPositionX[boundaryCell] = 0.5;
    cellData.mPositionY[boundaryCell] = 0.0;
    cellData.mRadius[boundaryCell] = 0.02;
    cellData.refreshRadiusSq(boundaryCell);

    int edgeSlot = cellData.addLink(
            first,
            second,
            CellLinkCreationReason::SPLIT_INHERITANCE,
            0.25,
            1.25,
            0.5f,
            0.01f);
    assertTrue(edgeSlot == 0, "Native snapshot topology test normal link added");
    cellData.mTopology.setEdgeRenderOffsets(0, -0.002f, 0.003f, 0.004f, -0.005f);
    int boundarySlot = cellData.addLink(boundaryCell,
            CellData::BOUNDARY_CONNECTION_INDEX,
            CellLinkCreationReason::UNSPECIFIED,
            0.75,
            0.0,
            0.25f,
            0.02f);
    assertTrue(boundarySlot == 0, "Native snapshot topology test boundary link added");
    cellData.mTopology.removeBoundaryLinkOfCell(boundaryCell);
    int boundaryIndex = cellData.mTopology.addBoundaryLink(boundaryCell, 0.75, 0.6, -0.4, 0.25f, 0.02f);
    cellData.mTopology.setBoundaryRenderStartOffset(boundaryIndex, 0.125f, -0.25f);

    const std::vector<std::uint8_t> snapshot =
            cellularity::save_format::release::encodeReleaseSubstrate(
                    sourceWorld)
                    .bytes;
    std::unique_ptr<SimulationWorld> loadedWorld =
            cellularity::save_format::release::decodeReleaseSubstrate(
                    snapshot, false);
    CellData& loaded = loadedWorld->mCellData;

    assertTrue(loaded.mTopology.edgeCount() == 1, "Native snapshot topology normal edge count");
    assertTrue(loaded.mTopology.isLinked(first, second), "Native snapshot topology normal edge");
    assertTrue(
            loaded.mTopology.edgeCreationReason(0)
                    == CellLinkCreationReason::SPLIT_INHERITANCE,
            "Native snapshot topology edge creation reason");
    assertNear(0.25, loaded.mTopology.edgeAngleA(0), 0.0, "Native snapshot topology edge angleA");
    assertNear(1.25, loaded.mTopology.edgeAngleB(0), 0.0, "Native snapshot topology edge angleB");
    assertNear(0.5f, loaded.mTopology.edgeTorsionalStiffness(0), 0.0, "Native snapshot topology edge stiffness");
    assertNear(0.01f, loaded.mTopology.edgeRestLength(0), 0.0, "Native snapshot topology edge rest length");
    assertNear(-0.002f, loaded.mTopology.edgeRenderStartOffsetAX(0), 0.0, "Native snapshot topology edge render start x");
    assertNear(-0.005f, loaded.mTopology.edgeRenderEndOffsetAY(0), 0.0, "Native snapshot topology edge render end y");

    assertTrue(loaded.mTopology.boundaryCount() == 1, "Native snapshot topology boundary count");
    assertTrue(loaded.mTopology.hasBoundaryLink(boundaryCell), "Native snapshot topology boundary link");
    assertNear(0.75, loaded.mTopology.boundaryAngleA(0), 0.0, "Native snapshot topology boundary angle");
    assertNear(0.6, loaded.mTopology.boundaryAnchorX(0), 0.0, "Native snapshot topology boundary anchor x");
    assertNear(-0.4, loaded.mTopology.boundaryAnchorY(0), 0.0, "Native snapshot topology boundary anchor y");
    assertNear(0.125f, loaded.mTopology.boundaryRenderStartOffsetX(0), 0.0, "Native snapshot topology boundary render x");

    assertTrue(!loadedWorld->cellContactCacheIsFresh(),
            "Native snapshot load leaves the empty physics contact cache stale");
    RenderContactChordBuffer renderContactChords(
            loadedWorld->mEnvironment.maxCellCount);
    RenderContactCollector renderContactCollector;
    renderContactCollector.collect(
            RenderContactInput::CurrentGeometry,
            loaded,
            loadedWorld->mEnvironment,
            renderContactChords);
    assertTrue(renderContactChords.activeChordCount(first) == 1
                    && renderContactChords.activeChords(first)[0]
                                    .neighborCellIndex
                            == second,
            "Loaded overlap has render walls before the first simulation tick");
}

void testJavaObjectStreamPrimitiveBlockData() {
    JavaObjectStreamReader reader(bytes({0xac, 0xed, 0x00, 0x05,
            0x77, 0x1b,
            0x01, 0x02, 0x03, 0x04,
            0x40, 0x09, 0x21, 0xfb, 0x54, 0x44, 0x2d, 0x18,
            0x01,
            0x3f, 0xc0, 0x00, 0x00,
            0x12, 0x34,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

    assertTrue(reader.readInt() == 0x01020304, "JavaObjectStreamReader int");
    assertNear(3.141592653589793, reader.readDouble(), 1e-15, "JavaObjectStreamReader double");
    assertTrue(reader.readBoolean(), "JavaObjectStreamReader boolean");
    assertNear(1.5f, reader.readFloat(), 0.0, "JavaObjectStreamReader float");
    assertTrue(reader.readShort() == 0x1234, "JavaObjectStreamReader short");
    assertTrue(reader.readLong() == 0x0102030405060708LL, "JavaObjectStreamReader long");
    assertTrue(!reader.hasRemainingBytes(), "JavaObjectStreamReader primitive stream consumed");
}

void testJavaObjectStreamStringAndReference() {
    JavaObjectStreamReader reader(bytes({0xac, 0xed, 0x00, 0x05,
            0x74, 0x00, 0x04, 'C', 'e', 'l', 'l',
            0x71, 0x00, 0x7e, 0x00, 0x00}));

    JavaObjectStreamReader::ObjectValue string = reader.readObject();
    assertTrue(string.kind == JavaObjectStreamReader::ObjectKind::String, "JavaObjectStreamReader string kind");
    assertTrue(string.stringValue == "Cell", "JavaObjectStreamReader string value");

    JavaObjectStreamReader::ObjectValue reference = reader.readObject();
    assertTrue(reference.kind == JavaObjectStreamReader::ObjectKind::Reference, "JavaObjectStreamReader reference kind");
    assertTrue(reference.stringValue == "Cell", "JavaObjectStreamReader reference value");
}

void testJavaObjectStreamSkipsSerializableObjectGraph() {
    JavaObjectStreamReader reader(bytes({0xac, 0xed, 0x00, 0x05, 0x73, 0x72, 0x00, 0x04,
            'L', 'i', 's', 't',
            0x69, 0xc8, 0x8a, 0x15, 0x40, 0x16, 0xae, 0x68,
            0x02, 0x00, 0x02,
            'I', 0x00, 0x05, 'v', 'a', 'l', 'u', 'e',
            'L', 0x00, 0x04, 'n', 'e', 'x', 't',
            0x74, 0x00, 0x06, 'L', 'L', 'i', 's', 't', ';',
            0x78, 0x70,
            0x00, 0x00, 0x00, 0x11,
            0x73, 0x71, 0x00, 0x7e, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x13,
            0x70,
            0x71, 0x00, 0x7e, 0x00, 0x03}));

    JavaObjectStreamReader::ObjectValue first = reader.readObject();
    assertTrue(first.kind == JavaObjectStreamReader::ObjectKind::Object, "JavaObjectStreamReader object kind");
    assertTrue(first.className == "List", "JavaObjectStreamReader object class name");

    JavaObjectStreamReader::ObjectValue second = reader.readObject();
    assertTrue(second.kind == JavaObjectStreamReader::ObjectKind::Reference, "JavaObjectStreamReader object reference kind");
    assertTrue(second.className == "List", "JavaObjectStreamReader object reference class name");
    assertTrue(!reader.hasRemainingBytes(), "JavaObjectStreamReader object graph consumed");
}

} // namespace

int main() {
    testEnvironmentAllocationValidation();
    testRendererMemoryIsCheckedAndIncludedInAdmissionEstimate();
    testSyntheticLegacySubstrateStillLoads();
    testReleaseSnapshotRoundTripsTopologyLinks();
    testJavaObjectStreamPrimitiveBlockData();
    testJavaObjectStreamStringAndReference();
    testJavaObjectStreamSkipsSerializableObjectGraph();
    std::cout << "native formats parity tests passed\n";
    return 0;
}
