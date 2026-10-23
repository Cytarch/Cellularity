#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
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

// Numerically specified current Link contract: sqrt(0.00018) * 0.7 / 2.
constexpr float kMinimumHalfWidth = 0.0046957427527f;
constexpr double kCoordinateTolerance = 1e-7;

void assertFiniteNear(double expected, double actual, double tolerance,
        const std::string& message) {
    assertTrue(std::isfinite(actual), message + ": non-finite output");
    cellularity::tests::assertNear(expected, actual, tolerance, message);
}

struct LinkFixture final {
    Environment environment;
    SimulationWorld world;
    RenderSourceFrame source;
    PreparedRenderFrame prepared;
    RenderSourceCapture capture;
    RenderFrameBuilder builder;

    explicit LinkFixture(int capacity = 8)
            : environment(Environment::constructDefaultEnvironment(capacity, 0, 1.0)),
              world(false, environment),
              capture(capacity, source),
              builder(capacity, prepared) {}

    int addCell(double x, double y, double radius = 0.02) {
        CellData& cells = world.mCellData;
        const int index = addAliveCell(cells, CellType::GLUEOCYTE);
        cells.mPositionX[index] = x;
        cells.mPositionY[index] = y;
        cells.mRadius[index] = radius;
        cells.refreshRadiusSq(index);
        return index;
    }

    void addNormalLink(int owner, int target, bool reversed = false,
            float startX = 0.0f, float startY = 0.0f,
            float endX = 0.0f, float endY = 0.0f) {
        CellData& cells = world.mCellData;
        addPairLink(cells, reversed ? target : owner, reversed ? owner : target,
                0.0, Environment::PI, 0.2f, 0.02f);
        const int edge = cells.mTopology.edgeCount() - 1;
        cells.mTopology.setEdgeRenderOffsets(edge,
                reversed ? endX : startX, reversed ? endY : startY,
                reversed ? startX : endX, reversed ? startY : endY);
    }

    void addBoundaryLink(int owner, double anchorX, double anchorY,
            float offsetX = 0.0f, float offsetY = 0.0f) {
        CellTopology& topology = world.mCellData.mTopology;
        const int boundary = topology.addBoundaryLink(
                owner, 0.0, anchorX, anchorY, 0.2f, 0.02f);
        assertTrue(boundary >= 0, "Boundary fixture adds a legal Link");
        topology.setBoundaryRenderStartOffset(boundary, offsetX, offsetY);
    }

    void captureWorld() {
        world.updateGrids();
        const RenderViewBounds view{-1.1f, 1.1f, -1.1f, 1.1f};
        RenderFrameCaptureMetadata metadata;
        metadata.worldEpoch = 1;
        metadata.viewport.visibleBounds = view;
        metadata.viewport.coverageBounds = view;
        capture.capture(world, view, metadata, source);
        assertTrue(source.valid, "Link fixture captures a complete source frame");
    }

    void clearChords() {
        source.activeContactChords.clear();
        source.activeContactChordNeighborRuntimeIds.clear();
        for (RenderSourceCell& cell : source.cells) {
            cell.contactChordOffset = 0;
            cell.contactChordCount = 0;
            cell.hasContactChordPayload = true;
        }
    }

    void setChords(int cellIndex, const std::vector<RenderContactChord>& chords) {
        const auto record = std::find_if(source.cells.begin(), source.cells.end(),
                [cellIndex](const RenderSourceCell& cell) {
                    return cell.sourceCellIndex == cellIndex;
                });
        assertTrue(record != source.cells.end(), "Chord fixture owns its source Cell");
        record->contactChordOffset = static_cast<std::uint32_t>(source.activeContactChords.size());
        record->contactChordCount = static_cast<std::uint8_t>(chords.size());
        for (const RenderContactChord& chord : chords) {
            source.activeContactChords.push_back(chord);
            source.activeContactChordNeighborRuntimeIds.push_back(
                    chord.neighborCellIndex == CellData::BOUNDARY_CONNECTION_INDEX
                            ? kBoundaryCellRuntimeId
                            : world.mCellData.runtimeId(chord.neighborCellIndex));
        }
    }

    void build() {
        builder.build(source, prepared);
        assertTrue(prepared.geometryReady, "Link fixture completes CPU geometry");
    }
};

RenderContactChord chordFor(int target, double x1, double y1,
        double x2, double y2, bool discarded = false) {
    RenderContactChord chord;
    chord.neighborCellIndex = target;
    chord.x1 = x1;
    chord.y1 = y1;
    chord.x2 = x2;
    chord.y2 = y2;
    chord.isDiscarded = discarded;
    return chord;
}

void assertSingleLink(const PreparedRenderFrame& frame,
        std::array<float, 2> start, std::array<float, 2> end,
        std::array<float, 4> base, std::array<float, 2> apexPatterns) {
    assertTrue(frame.drawLinksCount == 6, "Link contract requires six vertices");
    const std::array<float, 24> expected{
            start[0], start[1], 0.0f, 1.0f,
            base[0], base[1], -1.0f, 0.0f,
            base[2], base[3], 1.0f, 0.0f,
            end[0], end[1], 0.0f, 1.0f,
            base[0], base[1], -1.0f, 0.0f,
            base[2], base[3], 1.0f, 0.0f
    };
    const std::array<float, 6> patterns{
            apexPatterns[0], -1.0f, 1.0f, apexPatterns[1], -1.0f, 1.0f
    };
    assertTrue(frame.linkTriangleCoords.size() >= expected.size()
                    && frame.linkTextureCoords.size() >= patterns.size(),
            "Link contract owns complete active arrays");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        assertFiniteNear(expected[index], frame.linkTriangleCoords[index],
                kCoordinateTolerance, "Link vertex float " + std::to_string(index));
    }
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        assertFiniteNear(patterns[index], frame.linkTextureCoords[index], 5e-4,
                "Link pattern float " + std::to_string(index));
    }
}

void testContactChordPresent() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.28, -0.5);
    const int owner = fixture.addCell(0.25, -0.5);
    fixture.addNormalLink(owner, target, false, 0.002f, 0.004f, -0.003f, -0.002f);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.setChords(owner, {chordFor(target, 0.012, -0.008, 0.012, 0.008)});
    fixture.build();
    assertSingleLink(fixture.prepared, {0.252f, -0.496f}, {0.277f, -0.502f},
            {0.262f, -0.508f, 0.262f, -0.492f}, {0.5f, -0.25f});
}

void testNormalizedContactChord() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.03, 0.0);
    const int blocker = fixture.addCell(0.8, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.setChords(owner, {chordFor(target, 0.01, -0.01, 0.01, 0.01), chordFor(blocker, 0.015, 0.005, -0.015, 0.005)});
    fixture.build();
    // The intersection at (0.01, 0.005) clips the upper end of the Link base.
    assertSingleLink(fixture.prepared, {0.0f, 0.0f}, {0.03f, 0.0f},
            {0.01f, -0.01f, 0.01f, 0.005f}, {1.0f / 3.0f, 1.0f / 3.0f});
    assertTrue(fixture.source.activeContactChords[0].y2 == 0.01,
            "Link normalization leaves the captured raw chord unchanged");
}

void testShortContactChord() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.03, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.setChords(owner, {chordFor(target, 0.012, -0.0005, 0.012, 0.0005)});
    fixture.build();
    assertSingleLink(fixture.prepared, {0.0f, 0.0f}, {0.03f, 0.0f},
            {0.012f, -kMinimumHalfWidth, 0.012f, kMinimumHalfWidth}, {0.0f, 0.0f});
}

void testDiscardedContactChord() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.03, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.setChords(owner, {chordFor(target, 0.012, -0.008, 0.012, 0.008, true)});
    fixture.build();
    assertTrue(fixture.source.candidateLinks.size() == 1,
            "Discarded chord fixture retains the candidate Link");
    assertTrue(fixture.prepared.drawLinksCount == 0,
            "Discarded contact suppresses the Link without synthetic fallback");
}

void testNoContactNormalLink() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.2, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target, false, 0.002f, 0.0f, -0.003f, 0.0f);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.build();
    assertSingleLink(fixture.prepared, {0.002f, 0.0f}, {0.197f, 0.0f},
            {0.1f, kMinimumHalfWidth, 0.1f, -kMinimumHalfWidth}, {0.0f, 0.0f});
}

void testShortNormalFallback() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.001, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.build();
    assertSingleLink(fixture.prepared, {0.0f, 0.0f}, {0.001f, 0.0f},
            {0.0005f, kMinimumHalfWidth, 0.0005f, -kMinimumHalfWidth}, {0.0f, 0.0f});
}

void testUnequalRadiiWithoutRatioClamp() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.002, 0.0, 0.015);
    const int owner = fixture.addCell(0.0, 0.0, 0.03);
    fixture.addNormalLink(owner, target);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.build();
    // ratio = (0.03 + (0.002 - 0.03 - 0.015) / 2) / 0.002 = 4.25.
    assertSingleLink(fixture.prepared, {0.0f, 0.0f}, {0.002f, 0.0f},
            {-0.0065f, kMinimumHalfWidth, -0.0065f, -kMinimumHalfWidth}, {0.0f, 0.0f});
}

void testRawPatternProjection() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.03, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target, false, 0.0f, -0.032f, 0.0f, 0.032f);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.setChords(owner, {chordFor(target, 0.012, -0.008, 0.012, 0.008)});
    fixture.build();
    assertTrue(fixture.prepared.drawLinksCount == 6,
            "Pattern fixture emits the two triangles");
    // Projection on the 0.016-wide chord yields -4 and +4 at the two apexes.
    assertFiniteNear(-4.0, fixture.prepared.linkTextureCoords[0], 1e-6,
            "Negative apex pattern remains below -1");
    assertFiniteNear(4.0, fixture.prepared.linkTextureCoords[3], 1e-6,
            "Positive apex pattern remains above +1");
    assertSingleLink(fixture.prepared, {0.0f, -0.032f}, {0.03f, 0.032f},
            {0.012f, -0.008f, 0.012f, 0.008f}, {-4.0f, 4.0f});
}

void assertDeadEndpointExcluded(bool deadOwner) {
    LinkFixture fixture;
    const int target = fixture.addCell(0.03, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target);
    fixture.world.mCellData.mIsDead[deadOwner ? owner : target] = true;
    fixture.captureWorld();
    assertTrue(fixture.source.candidateLinks.empty(),
            "A dead endpoint is excluded during capture, before geometry building");
    fixture.build();
    assertTrue(fixture.prepared.drawLinksCount == 0, "Dead endpoint emits no Link");
}

void testDeadOwnerExcluded() {
    assertDeadEndpointExcluded(true);
}

void testDeadTargetExcluded() {
    assertDeadEndpointExcluded(false);
}

void testRenderOwnerIndependentOfTopologyOrder() {
    LinkFixture forward;
    LinkFixture reverse;
    const std::array<LinkFixture*, 2> fixtures{&forward, &reverse};
    std::vector<float> referenceVertices;
    std::vector<float> referencePatterns;
    for (LinkFixture* fixturePointer : fixtures) {
        LinkFixture& fixture = *fixturePointer;
        const bool reversed = fixturePointer == &reverse;
        const int target = fixture.addCell(0.03, 0.0);
        const int owner = fixture.addCell(0.0, 0.0);
        fixture.addNormalLink(owner, target, reversed, 0.002f, 0.004f, -0.003f, -0.002f);
        fixture.captureWorld();
        assertTrue(fixture.source.candidateLinks.size() == 1,
                "Render-owner fixture captures one normal Link");
        const RenderSourceLink& link = fixture.source.candidateLinks[0];
        assertTrue(link.cellARuntimeId == fixture.world.mCellData.runtimeId(owner)
                        && link.cellBRuntimeId == fixture.world.mCellData.runtimeId(target),
                "Render-owner is the higher Cell index for either topology order");
        assertTrue(link.renderStartOffsetX == 0.002f && link.renderStartOffsetY == 0.004f
                        && link.renderEndOffsetX == -0.003f && link.renderEndOffsetY == -0.002f,
                "Owner selection swaps both attachment offsets together");
    }
    for (LinkFixture* fixturePointer : fixtures) {
        LinkFixture& fixture = *fixturePointer;
        constexpr int target = 0;
        constexpr int owner = 1;
        fixture.clearChords();
        fixture.setChords(owner, {chordFor(target, 0.012, -0.008, 0.012, 0.008)});
        fixture.setChords(target, {chordFor(owner, -0.018, 0.01, -0.018, -0.01)});
        fixture.build();
        assertSingleLink(fixture.prepared, {0.002f, 0.004f}, {0.027f, -0.002f},
                {0.012f, -0.008f, 0.012f, 0.008f}, {0.5f, -0.25f});
        if (fixturePointer == &forward) {
            referenceVertices.assign(fixture.prepared.linkTriangleCoords.begin(),
                    fixture.prepared.linkTriangleCoords.begin() + 24);
            referencePatterns.assign(fixture.prepared.linkTextureCoords.begin(),
                    fixture.prepared.linkTextureCoords.begin() + 6);
        } else {
            assertTrue(std::equal(referenceVertices.begin(), referenceVertices.end(),
                               fixture.prepared.linkTriangleCoords.begin())
                            && std::equal(referencePatterns.begin(), referencePatterns.end(),
                                    fixture.prepared.linkTextureCoords.begin()),
                    "Topology endpoint order preserves exact prepared Link bytes");
        }
    }
}

void testBoundaryContactChord() {
    LinkFixture fixture;
    const int owner = fixture.addCell(0.99, 0.0);
    fixture.addBoundaryLink(owner, 1.0, 0.006, -0.002f, 0.004f);
    fixture.captureWorld();
    assertTrue(fixture.source.candidateLinks.size() == 1,
            "Boundary-only topology captures one drawable Link");
    fixture.clearChords();
    fixture.setChords(owner, {chordFor(CellData::BOUNDARY_CONNECTION_INDEX, 0.01, -0.007, 0.01, 0.007)});
    fixture.build();
    assertSingleLink(fixture.prepared, {0.988f, 0.004f}, {1.0f, 0.006f},
            {1.0f, -0.007f, 1.0f, 0.007f}, {4.0f / 7.0f, 6.0f / 7.0f});
}

void testBoundaryRadialFallback() {
    LinkFixture fixture;
    const int owner = fixture.addCell(0.5, 0.25);
    fixture.addBoundaryLink(owner, 0.9, 0.2, 0.001f, -0.003f);
    fixture.captureWorld();
    assertTrue(fixture.source.candidateLinks.size() == 1,
            "Boundary without a collision captures one Link");
    fixture.clearChords();
    fixture.build();
    // The radial normal is (0.0021, -0.0042), not normal to the anchor axis.
    assertSingleLink(fixture.prepared, {0.501f, 0.247f}, {0.9f, 0.2f},
            {0.7021f, 0.2208f, 0.6979f, 0.2292f}, {162.0f / 7.0f, -500.0f / 21.0f});
}

void assertDegenerateCenterUsesVisualAxis(double centerDistance) {
    LinkFixture fixture;
    const int target = fixture.addCell(centerDistance, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target, false, -0.01f, 0.0f, 0.01f, 0.0f);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.build();
    assertSingleLink(fixture.prepared, {-0.01f, 0.0f}, {0.01f, 0.0f},
            {0.0f, kMinimumHalfWidth, 0.0f, -kMinimumHalfWidth}, {0.0f, 0.0f});
}

void testZeroCenterAxisUsesVisualAxis() {
    assertDegenerateCenterUsesVisualAxis(0.0);
}

void testNearZeroCenterAxisUsesVisualAxis() {
    assertDegenerateCenterUsesVisualAxis(1e-30);
}

void testFullyDegenerateLinkIsSkipped() {
    LinkFixture fixture;
    const int target = fixture.addCell(0.0, 0.0);
    const int owner = fixture.addCell(0.0, 0.0);
    fixture.addNormalLink(owner, target);
    fixture.captureWorld();
    fixture.clearChords();
    fixture.build();
    assertTrue(fixture.source.candidateLinks.size() == 1,
            "Fully degenerate fixture retains a legal candidate");
    assertTrue(fixture.prepared.drawLinksCount == 0,
            "Coincident centers and attachments are explicitly skipped");
}

void testMixedNormalBoundaryCapacity() {
    constexpr int cellCount = 22;
    LinkFixture fixture(cellCount);
    for (int index = 0; index < cellCount; ++index) {
        fixture.addCell(-0.08 + 0.03 * (index % 6), -0.05 + 0.03 * (index / 6));
    }
    for (int lower = 0; lower < cellCount; ++lower) {
        for (int higher = lower + 1; higher < cellCount; ++higher) {
            // Complete graph minus a ring: 19 normal slots and one wall slot.
            if (higher == lower + 1 || (lower == 0 && higher == cellCount - 1)) {
                continue;
            }
            fixture.addNormalLink(higher, lower);
        }
    }
    for (int cell = 0; cell < cellCount; ++cell) {
        fixture.addBoundaryLink(cell, 0.9, 0.2);
        assertTrue(fixture.world.mCellData.mTopology.linkCount(cell) == 20,
                "Mixed capacity fixture fills every legal adjacency slot");
    }
    assertTrue(fixture.world.mCellData.mTopology.validate()
                    && fixture.world.mCellData.mTopology.edgeCount() == 209
                    && fixture.world.mCellData.mTopology.boundaryCount() == 22,
            "Mixed capacity fixture contains 209 normal and 22 legal boundary Links");
    fixture.captureWorld();
    assertTrue(fixture.source.candidateLinks.size() == 231,
            "Mixed capture retains all 231 Links beyond the normal-only bound of 220");
    fixture.clearChords();
    fixture.build();
    assertTrue(fixture.prepared.drawLinksCount == 1386,
            "Mixed geometry emits all 231 Links without capacity truncation");
    for (int index = 0; index < fixture.prepared.drawLinksCount; ++index) {
        assertTrue(std::isfinite(fixture.prepared.linkTextureCoords[index]),
                "Every mixed-capacity pattern value is finite");
        for (int component = 0; component < 4; ++component) {
            assertTrue(std::isfinite(fixture.prepared.linkTriangleCoords[4 * index + component]),
                    "Every mixed-capacity vertex component is finite");
        }
    }
}

using TestCase = std::pair<const char*, void (*)()>;
const std::array<TestCase, 17> kCases{{
        {"contact_chord_present", testContactChordPresent},
        {"normalized_contact_chord", testNormalizedContactChord},
        {"short_contact_chord", testShortContactChord},
        {"discarded_contact_chord", testDiscardedContactChord},
        {"no_contact_normal_link", testNoContactNormalLink},
        {"short_normal_fallback", testShortNormalFallback},
        {"unequal_radii_without_ratio_clamp", testUnequalRadiiWithoutRatioClamp},
        {"raw_pattern_projection", testRawPatternProjection},
        {"dead_owner_excluded", testDeadOwnerExcluded},
        {"dead_target_excluded", testDeadTargetExcluded},
        {"render_owner_topology_order", testRenderOwnerIndependentOfTopologyOrder},
        {"boundary_contact_chord", testBoundaryContactChord},
        {"boundary_radial_fallback", testBoundaryRadialFallback},
        {"zero_center_visual_axis", testZeroCenterAxisUsesVisualAxis},
        {"near_zero_center_visual_axis", testNearZeroCenterAxisUsesVisualAxis},
        {"fully_degenerate_link", testFullyDegenerateLinkIsSkipped},
        {"mixed_normal_boundary_capacity", testMixedNormalBoundaryCapacity},
}};

} // namespace

int main(int argc, char** argv) {
    assertTrue(argc <= 2, "Usage: cellularity_link_render_contract_tests [case]");
    bool found = false;
    for (const auto& [name, test] : kCases) {
        if (argc == 2 && std::string(argv[1]) != name) {
            continue;
        }
        found = true;
        test();
        std::cout << name << " passed\n";
    }
    assertTrue(found, "Unknown Link contract case");
    return 0;
}
