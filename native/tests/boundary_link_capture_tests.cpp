#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "render/NativeSimulationWorldRenderer.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace {

using namespace cellularity::render;
using cellularity::tests::addAliveCell;
using cellularity::tests::addPairLink;
using cellularity::tests::assertTrue;

struct BoundaryFixture final {
    Environment environment;
    SimulationWorld world;
    RenderSourceFrame source;
    PreparedRenderFrame prepared;
    RenderSourceCapture capture;
    RenderFrameBuilder builder;

    explicit BoundaryFixture(int capacity = 8)
            : environment(Environment::constructDefaultEnvironment(capacity, 0, 1.0)),
              world(false, environment), capture(capacity, source), builder(capacity, prepared) {}

    int addCell(double x, double y) {
        CellData& cells = world.mCellData;
        const int cell = addAliveCell(cells, CellType::GLUEOCYTE);
        cells.mPositionX[cell] = x;
        cells.mPositionY[cell] = y;
        cells.mRadius[cell] = 0.02;
        cells.refreshRadiusSq(cell);
        cells.mMass[cell] = CellData::MASS_PER_AREA * cells.mRadiusSq[cell];
        return cell;
    }

    int addBoundary(int cell, double anchorX, double anchorY,
            float offsetX = 0.0f, float offsetY = 0.0f) {
        const int index = world.mCellData.mTopology.addBoundaryLink(
                cell, 0.35, anchorX, anchorY, 0.2f, 0.04f);
        assertTrue(index >= 0, "Fixture creates a legal boundary Link");
        world.mCellData.mTopology.setBoundaryRenderStartOffset(index, offsetX, offsetY);
        return index;
    }

    RenderSourceCapture::Profile captureWorld(const RenderViewBounds& view) {
        world.updateGrids();
        RenderFrameCaptureMetadata metadata;
        metadata.worldEpoch = 1;
        metadata.viewport.visibleBounds = view;
        metadata.viewport.coverageBounds = view;
        const auto profile = capture.profileCapture(world, view, metadata, source);
        assertTrue(source.valid, "Boundary capture publishes a complete source frame");
        return profile;
    }
};

const RenderSourceCell& assertBoundaryRecord(const BoundaryFixture& fixture,
        const RenderSourceLink& link, int boundary, int owner) {
    const CellData& cells = fixture.world.mCellData;
    const auto boundaries = cells.mTopology.boundaryView();
    assertTrue(link.isBoundary() && link.runtimeId == kInvalidTopologyEdgeRuntimeId
                    && link.cellARuntimeId == cells.runtimeId(owner) && link.cellBRuntimeId == kBoundaryCellRuntimeId
                    && link.sourceCellBIndex == CellData::BOUNDARY_CONNECTION_INDEX && link.sourceLinkIndex == boundary
                    && link.sourceCellARecordIndex >= 0
                    && static_cast<std::size_t>(link.sourceCellARecordIndex) < fixture.source.cells.size(),
            "Boundary DTO preserves owner identity, sentinel and frame-local lookup keys");
    const RenderSourceCell& record = fixture.source.cells[static_cast<std::size_t>(link.sourceCellARecordIndex)];
    assertTrue(record.sourceCellIndex == owner && record.hasContactChordPayload
                    && link.cellAPositionX == static_cast<float>(cells.mPositionX[owner])
                    && link.cellAPositionY == static_cast<float>(cells.mPositionY[owner])
                    && link.cellBPositionX == static_cast<float>(boundaries.anchorX[boundary])
                    && link.cellBPositionY == static_cast<float>(boundaries.anchorY[boundary])
                    && link.fallbackCenterRatio == 0.5f
                    && link.renderStartOffsetX == cells.mTopology.boundaryRenderStartOffsetX(boundary)
                    && link.renderStartOffsetY == cells.mTopology.boundaryRenderStartOffsetY(boundary)
                    && link.renderEndOffsetX == 0.0f && link.renderEndOffsetY == 0.0f,
            "Boundary DTO owns the contact payload, saved anchor and original start offset without a synthetic end offset");
    return record;
}

void assertFiniteLinks(const PreparedRenderFrame& frame, int linkCount) {
    assertTrue(frame.geometryReady && frame.drawLinksCount == linkCount * 6,
            "Every captured non-degenerate boundary Link emits six vertices");
    for (int vertex = 0; vertex < frame.drawLinksCount; ++vertex) {
        assertTrue(std::isfinite(frame.linkTextureCoords[vertex]), "Active boundary pattern values are finite");
        for (int component = 0; component < 4; ++component) {
            assertTrue(std::isfinite(frame.linkTriangleCoords[4 * vertex + component]),
                    "Active boundary vertex values are finite");
        }
    }
}

void testBoundaryOnlyWithContact() {
    for (const bool preview : {false, true}) {
        BoundaryFixture fixture;
        fixture.world.mEnvironment.mIsPreviewMode = preview;
        const int owner = fixture.addCell(0.99, 0.0);
        const int boundary = fixture.addBoundary(owner, 1.0, 0.006, -0.002f, 0.004f);
        const auto revision = fixture.world.mCellData.mTopology.structuralRevision();
        const auto profile = fixture.captureWorld({0.9f, 1.1f, -0.1f, 0.1f});
        assertTrue(fixture.source.candidateLinks.size() == 1
                        && fixture.source.sourceTopologyEdgeCount == 0 && fixture.source.sourceBoundaryLinkCount == 1,
                "A boundary-only world captures its Link without normal topology edges");
        const auto& record = assertBoundaryRecord(fixture, fixture.source.candidateLinks[0], boundary, owner);
        const auto offset = record.contactChordOffset;
        assertTrue(record.contactChordCount == 1 && offset < fixture.source.activeContactChords.size()
                        && fixture.source.activeContactChords[offset].neighborCellIndex == CellData::BOUNDARY_CONNECTION_INDEX
                        && fixture.source.activeContactChordNeighborRuntimeIds[offset] == kBoundaryCellRuntimeId,
                "Boundary owner retains its actual dish contact and stable sentinel");
        assertTrue(profile.linkTopologyEdgeCount == 0 && profile.linkCandidateEdgeCount == 0
                        && profile.linkTopologyBoundaryCount == 1 && profile.linkBoundaryVisits == 1
                        && profile.linkBoundaryCandidateCount == 1,
                "Boundary counters remain separate from empty normal-edge counters");
        fixture.builder.build(fixture.source, fixture.prepared);
        assertFiniteLinks(fixture.prepared, 1);
        assertTrue(fixture.world.mCellData.mTopology.edgeCount() == 0
                        && fixture.world.mCellData.mTopology.boundaryCount() == 1
                        && fixture.world.mCellData.mTopology.structuralRevision() == revision,
                "Capturing and building a boundary Link do not modify physical topology");
    }
}

void testBoundaryOnlyWithoutContact() {
    BoundaryFixture fixture;
    const int owner = fixture.addCell(0.5, 0.25);
    const int boundary = fixture.addBoundary(owner, 0.9, 0.2, 0.001f, -0.003f);
    CellData& cells = fixture.world.mCellData;
    const std::array<double, 4> before{cells.mPositionX[owner], cells.mPositionY[owner], cells.mMass[owner], cells.mRadius[owner]};
    fixture.captureWorld({0.4f, 0.6f, 0.15f, 0.35f});
    assertTrue(fixture.source.candidateLinks.size() == 1 && fixture.source.sourceTopologyEdgeCount == 0,
            "A stretched boundary Link remains a candidate without contact or normal edges");
    const auto& record = assertBoundaryRecord(fixture, fixture.source.candidateLinks[0], boundary, owner);
    assertTrue(record.contactChordCount == 0, "Missing dish contact is represented by an owned empty contact range");
    fixture.builder.build(fixture.source, fixture.prepared);
    assertFiniteLinks(fixture.prepared, 1);
    const std::array<double, 4> after{cells.mPositionX[owner], cells.mPositionY[owner], cells.mMass[owner], cells.mRadius[owner]};
    assertTrue(before == after && cells.mTopology.validate(), "Boundary capture leaves physical cell values unchanged");
}

void testMixedOrderAndCounters() {
    BoundaryFixture fixture;
    const std::array<int, 5> cells{fixture.addCell(-0.04, 0.0), fixture.addCell(0.0, 0.0),
            fixture.addCell(0.04, 0.0), fixture.addCell(0.5, 0.0), fixture.addCell(0.0, 0.04)};
    addPairLink(fixture.world.mCellData, cells[2], cells[1], 0.0, Environment::PI, 0.2f, 0.04f);
    addPairLink(fixture.world.mCellData, cells[1], cells[0], 0.0, Environment::PI, 0.2f, 0.04f);
    fixture.addBoundary(cells[0], 0.1, 0.12);
    fixture.addBoundary(cells[3], 0.7, 0.12);
    fixture.addBoundary(cells[2], 0.12, 0.12);
    fixture.addBoundary(cells[4], 0.13, 0.12);
    fixture.addBoundary(cells[1], 0.14, 0.12);
    fixture.world.mCellData.mIsDead[cells[4]] = true;
    const RenderViewBounds view{-0.05f, 0.05f, -0.05f, 0.05f};
    const auto profile = fixture.captureWorld(view);
    assertTrue(fixture.source.sourceTopologyEdgeCount == 2 && fixture.source.sourceBoundaryLinkCount == 5
                    && fixture.source.candidateLinks.size() == 5,
            "Mixed source metadata keeps total normal and boundary counts distinct from active candidates");
    for (int edge = 0; edge < 2; ++edge) {
        assertTrue(!fixture.source.candidateLinks[edge].isBoundary() && fixture.source.candidateLinks[edge].sourceLinkIndex == edge,
                "Normal candidates remain first in ascending topology-edge order");
    }
    const std::array<int, 3> boundaryIndices{0, 2, 4};
    const std::array<int, 3> owners{cells[0], cells[2], cells[1]};
    for (std::size_t index = 0; index < boundaryIndices.size(); ++index) {
        assertBoundaryRecord(fixture, fixture.source.candidateLinks[2 + index], boundaryIndices[index], owners[index]);
    }
    assertTrue(profile.linkTopologyEdgeCount == 2 && profile.linkCandidateEdgeCount == 2
                    && profile.linkTopologyBoundaryCount == 5 && profile.linkBoundaryVisits == 5
                    && profile.linkBoundaryCandidateCount == 3 && profile.linkBoundaryCulledCount == 1
                    && profile.linkBoundaryDeadOwnerCount == 1,
            "Boundary profiling distinguishes total, accepted, culled and dead-owner records without inflating normal counts");
    const auto builderProfile = fixture.builder.profileBuild(fixture.source, fixture.prepared);
    assertFiniteLinks(fixture.prepared, 5);
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(builderProfile.linkTopologyEdgeCount == 2 && builderProfile.linkCandidateEdgeCount == 2
                    && builderProfile.linkTopologyBoundaryCount == 5 && builderProfile.linkBoundaryCandidateCount == 3,
            "The prepared-frame profiler does not count boundary records as normal edges");
#else
    (void)builderProfile;
#endif
    NativeSimulationWorldRenderer renderer(fixture.environment.maxCellCount);
    const auto wrapperProfile = renderer.profileCopyToDraw(fixture.world, view);
    assertTrue(wrapperProfile.linkTopologyEdgeCount == 2 && wrapperProfile.linkCandidateEdgeCount == 2
                    && wrapperProfile.linkTopologyBoundaryCount == 5 && wrapperProfile.linkBoundaryVisits == 5
                    && wrapperProfile.linkBoundaryCandidateCount == 3 && wrapperProfile.linkBoundaryCulledCount == 1
                    && wrapperProfile.linkBoundaryDeadOwnerCount == 1,
            "The native renderer wrapper preserves the separate capture counters");
    assertFiniteLinks(renderer.preparedFrame(), 5);
}

void testBoundaryCullingAndReuse() {
    for (int side = 0; side < 4; ++side) {
        BoundaryFixture fixture;
        const RenderViewBounds view{-0.125f, 0.125f, -0.125f, 0.125f};
        const float margin = static_cast<float>(CellData::MAX_CELL_RADIUS * 2.0);
        const float bound = side % 2 == 0 ? view.minX - margin : view.maxX + margin;
        const double outside = std::nextafter(static_cast<double>(bound),
                side % 2 == 0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
        const bool horizontal = side < 2;
        const int included = fixture.addCell(horizontal ? bound : 0.0, horizontal ? 0.0 : bound);
        const int excluded = fixture.addCell(horizontal ? outside : 0.0, horizontal ? 0.0 : outside);
        fixture.addBoundary(included, 0.4, 0.3);
        fixture.addBoundary(excluded, 0.4, 0.3);
        const auto profile = fixture.captureWorld(view);
        assertTrue(fixture.source.candidateLinks.size() == 1 && fixture.source.candidateLinks[0].sourceLinkIndex == 0
                        && profile.linkTopologyBoundaryCount == 2 && profile.linkBoundaryCulledCount == 1,
                "Expanded-view boundary culling includes the exact edge and excludes the next double outside on every side");
        CellData& cells = fixture.world.mCellData;
        std::swap(cells.mPositionX[included], cells.mPositionX[excluded]);
        std::swap(cells.mPositionY[included], cells.mPositionY[excluded]);
        fixture.captureWorld(view);
        assertTrue(fixture.source.candidateLinks.size() == 1 && fixture.source.candidateLinks[0].sourceLinkIndex == 1,
                "Reused source slots drop the previously visible boundary record");
        const std::vector<RenderSourceLink> saved = fixture.source.candidateLinks;
        fixture.captureWorld(view);
        assertTrue(fixture.source.candidateLinks.size() == saved.size()
                        && std::memcmp(saved.data(), fixture.source.candidateLinks.data(), saved.size() * sizeof(RenderSourceLink)) == 0,
                "Unchanged boundary captures preserve exact owned DTO bytes");
        cells.mTopology.removeAllLinksOfCell(included);
        cells.mTopology.removeAllLinksOfCell(excluded);
        const auto emptyProfile = fixture.captureWorld(view);
        assertTrue(fixture.source.sourceBoundaryLinkCount == 0 && fixture.source.candidateLinks.empty()
                        && emptyProfile.linkTopologyBoundaryCount == 0 && emptyProfile.linkBoundaryCandidateCount == 0,
                "Boundary metadata and profile counters reset after the last physical boundary Link is removed");
    }
}

void testInvalidBoundaryOwner() {
    BoundaryFixture fixture;
    const int owner = fixture.addCell(0.5, 0.25);
    const int boundary = fixture.addBoundary(owner, 0.9, 0.2);
    const auto boundaries = fixture.world.mCellData.mTopology.boundaryView();
    // Corrupt only this non-const fixture and restore it before asserting.
    int* const cellIndices = const_cast<int*>(boundaries.cell);
    for (const int invalid : {-1, fixture.world.mCellData.mCellCount}) {
        cellIndices[boundary] = invalid;
        bool rejected = false;
        try {
            fixture.captureWorld({0.4f, 0.6f, 0.15f, 0.35f});
        } catch (const std::logic_error& error) {
            rejected = std::string(error.what()) == "RenderSourceCapture received an invalid boundary Link owner";
        }
        cellIndices[boundary] = owner;
        assertTrue(rejected && !fixture.source.valid && fixture.source.candidateLinks.empty(),
                "Invalid boundary owner is rejected before endpoint payload or a candidate is published");
    }
    fixture.captureWorld({0.4f, 0.6f, 0.15f, 0.35f});
    assertTrue(fixture.source.candidateLinks.size() == 1 && fixture.world.mCellData.mTopology.validate(),
            "A failed boundary capture does not poison the next valid capture");
}

using TestCase = std::pair<const char*, void (*)()>;
const std::array<TestCase, 5> kCases{{
        {"boundary_only_with_contact", testBoundaryOnlyWithContact},
        {"boundary_only_without_contact", testBoundaryOnlyWithoutContact},
        {"mixed_order_and_counters", testMixedOrderAndCounters},
        {"boundary_culling_and_reuse", testBoundaryCullingAndReuse},
        {"invalid_boundary_owner", testInvalidBoundaryOwner},
}};

} // namespace

int main(int argc, char** argv) {
    assertTrue(argc <= 2, "Usage: cellularity_boundary_link_capture_tests [case]");
    bool found = false;
    for (const auto& [name, test] : kCases) {
        if (argc == 2 && std::string(argv[1]) != name) {
            continue;
        }
        found = true;
        test();
        std::cout << name << " passed\n";
    }
    assertTrue(found, "Unknown boundary capture case");
    return 0;
}
