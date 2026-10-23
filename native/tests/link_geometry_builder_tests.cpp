#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/CellDataConstants.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderContactLimits.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderLinkGeometryBuilder.h"
#include "render/frame/RenderSourceFrame.h"
#include "render/geometry/RenderContactChordNormalizer.h"
#include "tests/support/TestAssertions.h"

namespace {

using namespace cellularity::render;
using cellularity::tests::assertNear;
using cellularity::tests::assertTrue;

struct Fixture final {
    RenderSourceFrame source;
    PreparedRenderFrame frame;
    RenderContactChordResolver resolver;
    RenderLinkGeometryBuilder builder;
    RenderLinkGeometryBuilder::Profile profile;
    RenderContactChordResolver::Profile resolverProfile;
    RenderContactChordNormalizationProfile normalizationProfile;

    Fixture() {
        source.valid = true;
        source.sourceCellCount = 1;
        source.sourceTopologyEdgeCount = 1;
        RenderSourceCell owner;
        owner.runtimeId = 1;
        owner.sourceCellIndex = 0;
        owner.radius = 0.02f;
        owner.hasContactChordPayload = true;
        source.cells.push_back(owner);
        RenderSourceLink link;
        link.runtimeId = 1;
        link.cellARuntimeId = 1;
        link.cellBRuntimeId = 2;
        link.sourceCellARecordIndex = 0;
        link.sourceCellBIndex = 1;
        link.cellBPositionX = 0.03f;
        source.candidateLinks.push_back(link);
        resolver.reset(1, 1, 1, kMaximumRenderContactChordsPerCell);
        builder.reset(6);
    }

    RenderSourceLink& link() {
        return source.candidateLinks[0];
    }

    void setChords(std::vector<RenderContactChord> chords) {
        source.activeContactChords = std::move(chords);
        source.activeContactChordNeighborRuntimeIds.clear();
        for (const auto& chord : source.activeContactChords) {
            source.activeContactChordNeighborRuntimeIds.push_back(chord.neighborCellIndex == CellDataConstants::BOUNDARY_CONNECTION_INDEX ? kBoundaryCellRuntimeId : CellRuntimeId{2});
        }
        source.cells[0].contactChordCount = static_cast<std::uint8_t>(source.activeContactChords.size());
    }

    void makeBoundary() {
        source.sourceTopologyEdgeCount = 0;
        source.sourceBoundaryLinkCount = 1;
        link().runtimeId = kInvalidTopologyEdgeRuntimeId;
        link().cellBRuntimeId = kBoundaryCellRuntimeId;
        link().sourceCellBIndex = CellDataConstants::BOUNDARY_CONNECTION_INDEX;
    }

    void build() {
        frame.drawLinksCount = 0;
        frame.diagnostics = {};
        resolver.beginFrame();
        resolverProfile = {};
        normalizationProfile = {};
        builder.build(source, frame, resolver, &resolverProfile, &normalizationProfile, &profile);
        for (int vertex = 0; vertex < frame.drawLinksCount; ++vertex) {
            assertTrue(std::isfinite(frame.linkTextureCoords[vertex]), "All active pattern values are finite");
            for (int component = 0; component < 4; ++component) {
                assertTrue(std::isfinite(frame.linkTriangleCoords[4 * vertex + component]), "All active vertex values are finite");
            }
        }
    }
};

RenderContactChord contact(double x1, double y1, double x2, double y2, bool discarded = false) {
    RenderContactChord chord;
    chord.neighborCellIndex = 1;
    chord.x1 = x1;
    chord.y1 = y1;
    chord.x2 = x2;
    chord.y2 = y2;
    chord.isDiscarded = discarded;
    return chord;
}

void testUnusableContactFallback() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const bool boundary : {false, true}) {
        for (RenderContactChord chord : {contact(0.012, 0.0, 0.012, 0.0),
                     contact(nan, -0.008, 0.012, 0.008), contact(0.012, -0.008, infinity, 0.008),
                     contact(1e100, -0.008, 1e100, 0.008)}) {
            Fixture fixture;
            if (boundary) {
                fixture.makeBoundary();
                fixture.link().cellAPositionX = 0.5f;
                fixture.link().cellBPositionX = 0.9f;
                chord.neighborCellIndex = CellDataConstants::BOUNDARY_CONNECTION_INDEX;
            }
            fixture.setChords({chord});
            fixture.build();
            assertTrue(fixture.frame.drawLinksCount == 6 && fixture.frame.diagnostics.degenerateLinkCount == 0,
                    "Unusable non-discarded contact falls back instead of losing a legal Link");
            assertNear(boundary ? 0.7 : 0.015, fixture.frame.linkTriangleCoords[4], 1e-7, "Fallback base uses the correct midpoint");
#ifdef CELLULARITY_RENDER_PROFILE
            assertTrue(fixture.profile.normalFallbackCount == (boundary ? 0 : 1)
                            && fixture.profile.boundaryFallbackCount == (boundary ? 1 : 0)
                            && fixture.profile.contactCrossSectionCount == 0,
                    "Unusable contact is counted as the appropriate fallback");
#endif
            fixture.source.activeContactChords[0].isDiscarded = true;
            fixture.build();
            assertTrue(fixture.frame.drawLinksCount == 0 && fixture.frame.diagnostics.degenerateLinkCount == 0,
                    "Even an unusable discarded contact hides the Link, never falling back");
#ifdef CELLULARITY_RENDER_PROFILE
            assertTrue(fixture.profile.discardedContactCount == 1 && fixture.profile.normalFallbackCount == 0 && fixture.profile.boundaryFallbackCount == 0,
                    "Reused profile counts only the discarded contact");
#endif
        }
    }
}

void testFallbackAxisPolicy() {
    const float nominalWidth = static_cast<float>(CellDataConstants::MIN_CELL_RADIUS * 0.7);
    Fixture fixture;
    fixture.link().cellBPositionX = 0.0f;
    fixture.link().renderStartOffsetX = -0.001f;
    fixture.link().renderEndOffsetX = 0.001f;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 6, "Undefined center axis uses separated visual attachments");
    assertNear(0.002, fixture.frame.linkTriangleCoords[5] - fixture.frame.linkTriangleCoords[9], 1e-8,
            "Only the undefined-axis branch bounds width by visual distance");
    fixture.link().cellBPositionX = std::nextafter(1e-9f, std::numeric_limits<float>::infinity());
    fixture.link().renderStartOffsetX = 0.0f;
    fixture.link().renderEndOffsetX = 0.0f;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 6, "A center axis above the absolute threshold remains drawable");
    assertNear(nominalWidth, fixture.frame.linkTriangleCoords[5] - fixture.frame.linkTriangleCoords[9], 1e-8,
            "A defined short center axis retains the full nominal width");

    fixture.makeBoundary();
    fixture.link().cellBPositionX = 0.03f;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 6, "A zero radial axis also uses the visual fallback");
    assertNear(0.015, fixture.frame.linkTriangleCoords[4], 1e-7, "Undefined radial base uses the visual midpoint");
    fixture.link().cellBPositionX = 0.0f;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 0 && fixture.frame.diagnostics.degenerateLinkCount == 1,
            "Both undefined axes skip the Link with production diagnostics");
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(fixture.profile.degenerateLinkCount == 1 && fixture.profile.boundaryFallbackCount == 0,
            "Degenerate skips have a separate counter, not an emitted fallback counter");
#endif
}

void testFiniteOutputAndAtomicAppend() {
    Fixture fixture;
    fixture.setChords({contact(0.012, -0.008, 0.012, 0.008)});
    fixture.link().fallbackCenterRatio = std::numeric_limits<float>::quiet_NaN();
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 6, "Valid contact does not depend on an unused fallback ratio");
    const auto vertices = fixture.frame.linkTriangleCoords;
    const auto patterns = fixture.frame.linkTextureCoords;
    for (const float invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        fixture.link().renderStartOffsetX = invalid;
        fixture.build();
        assertTrue(fixture.frame.drawLinksCount == 0 && fixture.frame.diagnostics.degenerateLinkCount == 1
                        && fixture.frame.linkTriangleCoords == vertices && fixture.frame.linkTextureCoords == patterns,
                "Non-finite attachments are rejected before any partial triangle write");
    }
    fixture.link().renderStartOffsetX = 0.0f;
    fixture.link().renderStartOffsetY = std::numeric_limits<float>::max();
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 0 && fixture.frame.diagnostics.degenerateLinkCount == 1
                    && fixture.frame.linkTriangleCoords == vertices && fixture.frame.linkTextureCoords == patterns,
            "Unrepresentable raw projection is rejected before any partial write, without clamping it");
    fixture.link().renderStartOffsetY = 0.0f;
    fixture.setChords({});
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 0 && fixture.frame.diagnostics.degenerateLinkCount == 1,
            "Non-finite used fallback ratio never reaches active buffers");

    fixture.link().fallbackCenterRatio = 0.5f;
    fixture.link().cellBPositionX = 0.0f;
    fixture.link().renderStartOffsetX = -2e38f;
    fixture.link().renderEndOffsetX = 2e38f;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 0 && fixture.frame.diagnostics.degenerateLinkCount == 1,
            "Overflowing visual axes never reach active buffers");
    fixture.link().cellBPositionX = 0.03f;
    fixture.link().renderStartOffsetX = 0.0f;
    fixture.link().renderEndOffsetX = 0.0f;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 6 && fixture.frame.diagnostics.degenerateLinkCount == 0,
            "The next valid frame resets degenerate diagnostics and recovers geometry");
    const auto completeVertices = fixture.frame.linkTriangleCoords;
    const auto completePatterns = fixture.frame.linkTextureCoords;
    fixture.builder.reset(5);
    bool rejected = false;
    try {
        fixture.build();
    } catch (const std::length_error&) {
        rejected = true;
    }
    assertTrue(rejected && fixture.frame.drawLinksCount == 0
                    && fixture.frame.linkTriangleCoords == completeVertices && fixture.frame.linkTextureCoords == completePatterns,
            "An insufficient logical bound rejects both triangles before any partial write");
}

void testProfileAndImmutableResolver() {
    Fixture fixture;
    auto blocker = contact(0.015, 0.005, -0.015, 0.005);
    blocker.neighborCellIndex = 2;
    fixture.setChords({contact(0.01, -0.01, 0.01, 0.01), blocker});
    const auto raw = fixture.source.activeContactChords;
    fixture.build();
    assertTrue(fixture.frame.drawLinksCount == 6, "Link-only resolver normalizes an offscreen owner");
    const auto vertices = fixture.frame.linkTriangleCoords;
    const auto patterns = fixture.frame.linkTextureCoords;
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(fixture.profile.contactCrossSectionCount == 1 && fixture.profile.normalFallbackCount == 0
                    && fixture.resolverProfile.linkOnlyCacheMisses == 1 && fixture.normalizationProfile.calls == 1,
            "Contact path forwards resolver and normalization profiling");
#endif
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const auto& before = raw[index];
        const auto& after = fixture.source.activeContactChords[index];
        assertTrue(before.x1 == after.x1 && before.y1 == after.y1 && before.x2 == after.x2 && before.y2 == after.y2
                        && before.isDiscarded == after.isDiscarded && before.joinsNextChord == after.joinsNextChord,
                "The Link resolver never mutates raw immutable source chords");
    }
    fixture.frame.drawLinksCount = 0;
    fixture.builder.build(fixture.source, fixture.frame, fixture.resolver, &fixture.resolverProfile,
            &fixture.normalizationProfile, &fixture.profile);
    assertTrue(fixture.frame.linkTriangleCoords == vertices && fixture.frame.linkTextureCoords == patterns,
            "Cached normalization produces byte-identical Link geometry");
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(fixture.resolverProfile.cacheHits == 1 && fixture.normalizationProfile.calls == 1,
            "Repeated Link lookup reuses the normalized cache without a second normalization");
#endif
    fixture.setChords({});
    fixture.build();
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(fixture.profile.normalFallbackCount == 1 && fixture.profile.contactCrossSectionCount == 0,
            "Profile resets when an owner loses its contact");
#endif
    fixture.makeBoundary();
    fixture.link().cellAPositionX = 0.5f;
    fixture.link().cellBPositionX = 0.9f;
    fixture.build();
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(fixture.profile.topologyEdgeCount == 0 && fixture.profile.topologyBoundaryCount == 1
                    && fixture.profile.candidateBoundaryCount == 1 && fixture.profile.candidateEdgeCount == 0
                    && fixture.profile.boundaryFallbackCount == 1 && fixture.profile.normalFallbackCount == 0,
            "Boundary fallback and candidate counters remain distinct from normal edges");
#endif
}

using TestCase = std::pair<const char*, void (*)()>;
const std::array<TestCase, 4> kCases{{
        {"unusable_contact_fallback", testUnusableContactFallback},
        {"fallback_axis_policy", testFallbackAxisPolicy},
        {"finite_output_and_atomic_append", testFiniteOutputAndAtomicAppend},
        {"profile_and_immutable_resolver", testProfileAndImmutableResolver},
}};

} // namespace

int main(int argc, char** argv) {
    assertTrue(argc <= 2, "Usage: cellularity_link_geometry_builder_tests [case]");
    bool found = false;
    for (const auto& [name, test] : kCases) {
        if (argc == 2 && std::string(argv[1]) != name) {
            continue;
        }
        found = true;
        test();
        std::cout << name << " passed\n";
    }
    assertTrue(found, "Unknown Link geometry builder case");
}
