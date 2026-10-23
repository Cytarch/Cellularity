#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/RendererMemoryModel.h"
#include "core/SimulationMemoryPolicy.h"
#include "io/DecodeError.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace cellularity::tests {

struct NativeSimulationWorldRendererTestAccess {
    static std::size_t candidateWordCount(const render::RenderSourceCapture& capture) {
        return capture.mLinkCandidateBits.size();
    }
};

} // namespace cellularity::tests

namespace {

using namespace cellularity::render;
using namespace cellularity::memory;
using cellularity::tests::assertTrue;

static_assert(sizeof(int) == 4 && sizeof(float) == 4);
constexpr int kMaximumUploadedCellCapacity = 397093;
constexpr int kMaximumUploadedFoodCapacity = 134217727;
constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;

template <typename Exception, typename Action>
void requireException(Action action, const std::string& message) {
    try {
        action();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        cellularity::tests::fail(message + ": wrong exception: " + error.what());
    }
    cellularity::tests::fail(message + ": no exception");
}

template <typename T>
void appendValue(std::vector<std::byte>& bytes, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(T));
}

template <typename T>
void appendVector(std::vector<std::byte>& bytes, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    appendValue(bytes, values.size());
    appendValue(bytes, values.capacity());
    if (!values.empty()) {
        const auto* first = reinterpret_cast<const std::byte*>(values.data());
        bytes.insert(bytes.end(), first, first + values.size() * sizeof(T));
    }
}

std::vector<std::byte> preparedSnapshot(const PreparedRenderFrame& frame) {
    std::vector<std::byte> bytes;
    appendValue(bytes, frame.valid);
    appendValue(bytes, frame.geometryReady);
    appendValue(bytes, frame.identity);
    appendValue(bytes, frame.environment);
    appendValue(bytes, frame.viewport);
    appendValue(bytes, frame.resolvedInput);
    appendValue(bytes, frame.timeline);
    appendValue(bytes, frame.layout);
    appendValue(bytes, frame.interpolation);
    appendValue(bytes, frame.diagnostics);
    appendVector(bytes, frame.markers);
    appendVector(bytes, frame.cellTriangleCoords);
    appendVector(bytes, frame.cellTextureCoords);
    appendVector(bytes, frame.cellCenterCoords);
    appendVector(bytes, frame.cellColors);
    appendVector(bytes, frame.cellIndices);
    appendVector(bytes, frame.foodInstanceAttributes);
    appendVector(bytes, frame.foodVertexAttributes);
    appendVector(bytes, frame.deadCellInstanceAttributes);
    appendVector(bytes, frame.deadCellVertexAttributes);
    appendVector(bytes, frame.linkTriangleCoords);
    appendVector(bytes, frame.linkTextureCoords);
    appendValue(bytes, frame.cellVertexCountBefore);
    appendValue(bytes, frame.cellVertexCount);
    appendValue(bytes, frame.cellIndexCountBefore);
    appendValue(bytes, frame.cellIndexCount);
    appendValue(bytes, frame.bunchStart);
    appendValue(bytes, frame.bunchSize);
    appendValue(bytes, frame.bunchIndexStart);
    appendValue(bytes, frame.bunchIndexSize);
    appendValue(bytes, frame.bunches);
    appendValue(bytes, frame.cellDrawVertexCount);
    appendValue(bytes, frame.cellDrawIndexCount);
    appendValue(bytes, frame.cellCenterFloatCount);
    appendValue(bytes, frame.foodInstanceCount);
    appendValue(bytes, frame.deadCellInstanceCount);
    appendValue(bytes, frame.drawLinksCount);
    return bytes;
}

std::vector<std::byte> sourceSnapshot(const RenderSourceFrame& frame) {
    // valid must become false on failure; all payload and metadata stay intact.
    std::vector<std::byte> bytes;
    appendValue(bytes, frame.layoutVersion);
    appendValue(bytes, frame.captureSequence);
    appendValue(bytes, frame.capture);
    appendValue(bytes, frame.environment);
    appendValue(bytes, frame.sourceCellCount);
    appendValue(bytes, frame.sourceFoodCount);
    appendValue(bytes, frame.sourceDeadSplashCount);
    appendValue(bytes, frame.sourceTopologyEdgeCount);
    appendValue(bytes, frame.sourceBoundaryLinkCount);
    appendValue(bytes, frame.sourceFoodCapacity);
    appendValue(bytes, frame.sourceDeadSplashCapacity);
    appendValue(bytes, frame.visibleCellCount);
    for (const auto& bucket : frame.orderedCellRecordIndices) {
        appendVector(bytes, bucket);
    }
    appendVector(bytes, frame.cells);
    appendVector(bytes, frame.activeContactChords);
    appendVector(bytes, frame.activeContactChordNeighborRuntimeIds);
    appendVector(bytes, frame.candidateLinks);
    appendVector(bytes, frame.foods);
    appendVector(bytes, frame.deadSplashes);
    appendVector(bytes, frame.markers);
    return bytes;
}

struct Fixture final {
    Environment environment;
    SimulationWorld world;
    RenderSourceFrame source;
    PreparedRenderFrame prepared;
    RenderSourceCapture capture;
    RenderFrameBuilder builder;

    explicit Fixture(int capacity)
            : environment(Environment::constructDefaultEnvironment(capacity, 0, 1.0)),
              world(false, environment),
              capture(capacity, source),
              builder(capacity, prepared) {}

    void addCells(int count) {
        for (int index = 0; index < count; ++index) {
            const int cell = cellularity::tests::addAliveCell(world.mCellData, CellType::GLUEOCYTE);
            world.mCellData.mPositionX[cell] = -0.3 + 0.08 * index;
            world.mCellData.mPositionY[cell] = 0.0;
            world.mCellData.mRadius[cell] = 0.02;
            world.mCellData.refreshRadiusSq(cell);
        }
    }

    void fillBoundarySlots() {
        CellTopology& topology = world.mCellData.mTopology;
        for (int cell = 0; cell < world.mCellData.mCellCount; ++cell) {
            for (int slot = 0; slot < CellData::MAX_NEIGHBORS; ++slot) {
                assertTrue(topology.addBoundaryLink(cell, 0.0, 0.8, 0.2 + 0.001 * slot, 0.2f, 0.02f) >= 0,
                        "Every existing adjacency slot supports a boundary Link");
            }
        }
        assertTrue(topology.validate(), "Boundary-only fixture has legal topology");
    }

    void captureWorld(RenderSourceCapture* overrideCapture = nullptr, RenderSourceFrame* overrideSource = nullptr) {
        world.updateGrids();
        const RenderViewBounds view{-1.1f, 1.1f, -1.1f, 1.1f};
        RenderFrameCaptureMetadata metadata;
        metadata.worldEpoch = 1;
        metadata.viewport.visibleBounds = view;
        metadata.viewport.coverageBounds = view;
        (overrideCapture != nullptr ? *overrideCapture : capture).capture(world, view, metadata, overrideSource != nullptr ? *overrideSource : source);
    }
};

void testLogicalBoundsAndInitialSizes() {
    for (const int capacity : {1, 2, 3, 7, 22, 1000}) {
        RendererInitialBufferLayout layout;
        assertTrue(tryBuildRendererInitialBufferLayout(capacity, layout), "Small logical layout is supported");
        const auto cells = static_cast<std::uint64_t>(capacity);
        assertTrue(layout.maximumNormalEdgeCount == 10U * cells
                        && layout.maximumDrawableLinkCount == 20U * cells
                        && layout.linkCandidateWordEntries == (10U * cells + 63U) / 64U
                        && layout.maximumLinkVertexCount == 120U * cells,
                "Normal-only bitset and drawable bounds use different exact maxima");
        assertTrue(layout.cellTriangleFloatEntries == 224U * cells
                        && layout.cellTextureFloatEntries == 448U * cells
                        && layout.cellCenterFloatEntries == 224U * cells
                        && layout.cellColorFloatEntries == 448U * cells
                        && layout.cellIndexEntries == 168U * cells
                        && layout.linkTriangleFloatEntries == 84U * cells
                        && layout.linkTextureFloatEntries == 42U * cells
                        && layout.foodInstanceFloatEntries == 4U * cells
                        && layout.foodVertexFloatEntries == 4U * cells
                        && layout.deadInstanceFloatEntries == ((cells + 3U) / 4U) * 4U
                        && layout.deadVertexFloatEntries == layout.deadInstanceFloatEntries,
                "Initial allocation still uses the existing average-contact sizing");
    }
    RendererInitialBufferLayout output;
    assertTrue(tryBuildRendererInitialBufferLayout(7, output), "Initialize layout sentinel");
    const auto before = output;
    for (const int invalid : {0, -1, kMaximumUploadedCellCapacity + 1, INT_MAX}) {
        assertTrue(!tryBuildRendererInitialBufferLayout(invalid, output), "Unsupported layout is rejected");
        assertTrue(std::memcmp(&before, &output, sizeof(output)) == 0, "Rejected layout does not partially overwrite output");
    }
}

void testUploadByteBoundaries() {
    RendererInitialBufferLayout layout;
    assertTrue(tryBuildRendererInitialBufferLayout(kMaximumUploadedCellCapacity, layout), "Last int-byte-safe Cell capacity is supported");
    assertTrue(layout.maximumCellVertexCount == 338ULL * kMaximumUploadedCellCapacity
                    && layout.maximumCellVertexCount * 4U * sizeof(float) <= INT_MAX
                    && 338ULL * (kMaximumUploadedCellCapacity + 1) * 4U * sizeof(float) > INT_MAX,
            "Cell attribute upload determines the independently calculated boundary");
    assertTrue(layout.maximumLinkVertexCount * 4U * sizeof(float) <= INT_MAX
                    && layout.maximumLinkVertexCount * sizeof(float) <= INT_MAX
                    && layout.maximumCellIndexCount * sizeof(unsigned short) <= INT_MAX,
            "Link coordinates, patterns and Cell indices fit int-byte uploads");
    for (const unsigned int bits : {32U, 64U}) {
        RendererMemoryEstimate output;
        assertTrue(tryEstimateRendererMemory(kMaximumUploadedCellCapacity, 0, bits, output), "Large layout estimate needs no allocation");
        assertTrue(tryEstimateRendererMemory(1, kMaximumUploadedFoodCapacity, bits, output), "Last int-byte-safe Food capacity is supported");
        const auto before = output;
        for (const auto invalid : {std::pair{kMaximumUploadedCellCapacity + 1, 0}, std::pair{1, kMaximumUploadedFoodCapacity + 1}, std::pair{INT_MAX, INT_MAX}, std::pair{1, -1}}) {
            assertTrue(!tryEstimateRendererMemory(invalid.first, invalid.second, bits, output), "Unsupported upload range is rejected");
            assertTrue(std::memcmp(&before, &output, sizeof(output)) == 0, "Rejected estimate does not partially overwrite output");
        }
        for (const unsigned int invalidBits : {0U, 65U}) {
            assertTrue(!tryEstimateRendererMemory(1, 0, invalidBits, output), "Invalid address width is rejected");
            assertTrue(std::memcmp(&before, &output, sizeof(output)) == 0, "Invalid address width preserves estimate output");
        }
    }
}

void testFileAndAdmissionPolicy() {
    const RuntimeLimits limits32 = runtimeLimitsForAddressBits(32);
    const RuntimeLimits limits64 = runtimeLimitsForAddressBits(64);
    assertTrue(limits32.absoluteSubstrateFileBytes == 2048U * kMiB
                    && limits64.absoluteSubstrateFileBytes == 2048U * kMiB
                    && limits32.operationalSubstrateFileBytes == 512U * kMiB
                    && limits64.operationalSubstrateFileBytes == 2048U * kMiB
                    && limits32.worldAllocationBudgetBytes == 384U * kMiB
                    && limits64.worldAllocationBudgetBytes == 1024U * kMiB,
            "File limits and RAM budgets remain separate unchanged policies");
    for (const RuntimeLimits limits : {limits32, limits64}) {
        validateSubstrateFileSize(limits.operationalSubstrateFileBytes, limits);
        requireException<DecodeError>([&] {
            validateSubstrateFileSize(limits.operationalSubstrateFileBytes + 1, limits);
        },
                "File operational boundary rejects one extra byte");
        requireException<DecodeError>([&] {
            validateSubstrateFileSize(2048U * kMiB + 1, limits);
        },
                "Absolute file boundary rejects one extra byte");
        const auto deviceLimits = runtimeLimitsForDevice(limits.addressBits, 512U * kMiB);
        assertTrue(deviceLimits.worldAllocationBudgetBytes == 128U * kMiB, "Device RAM applies the existing quarter-memory budget");
        const auto environment = Environment::constructDefaultEnvironment(10000, 0, 1.0);
        const auto world = estimateWorldAllocation(environment, limits.addressBits);
        RendererMemoryEstimate renderer;
        assertTrue(tryEstimateRendererMemory(10000, 0, limits.addressBits, renderer), "Compute explicit renderer budget");
        assertTrue(world.rendererBudgetedBytes == renderer.budgetedBytes
                        && renderer.budgetedBytes == 2U * (renderer.maximumReusablePayloadBytes + renderer.maximumGhostSnapshotPayloadBytes)
                        && world.bytes > world.budgetBytes,
                "Admission includes renderer growth reserve and rejects an over-budget world");
    }
    const auto environment = Environment::constructDefaultEnvironment(1000, 0, 1.0);
    const auto estimate32 = estimateWorldAllocation(environment, 32);
    const auto estimate64 = estimateWorldAllocation(environment, 64);
    assertTrue(estimate64.bytes - estimate32.bytes == estimate64.rendererBudgetedBytes - estimate32.rendererBudgetedBytes,
            "World headroom does not multiply the separately budgeted renderer again");
}

void testBoundaryOnlyFullCapacity() {
    Fixture fixture(7);
    assertTrue(fixture.prepared.linkTriangleCoords.size() == 588
                    && fixture.prepared.linkTextureCoords.size() == 294
                    && fixture.source.candidateLinks.empty()
                    && fixture.source.candidateLinks.capacity() == 0
                    && cellularity::tests::NativeSimulationWorldRendererTestAccess::candidateWordCount(fixture.capture) == 2,
            "Reset does not preallocate the theoretical drawable maximum");
    fixture.addCells(7);
    fixture.fillBoundarySlots();
    assertTrue(fixture.world.mCellData.mTopology.addBoundaryLink(0, 0.0, 0.8, 0.2, 0.2f, 0.02f) == -1,
            "Only the existing twentieth adjacency slot is the topology limit");
    fixture.captureWorld();
    assertTrue(fixture.source.candidateLinks.size() == 140
                    && fixture.source.sourceTopologyEdgeCount == 0
                    && fixture.source.sourceBoundaryLinkCount == 140,
            "Boundary-only capture reaches twice the old normal-only record maximum");
    fixture.builder.build(fixture.source, fixture.prepared);
    assertTrue(fixture.prepared.drawLinksCount == 840, "Every boundary Link builds six vertices without false overflow");
    for (int vertex = 0; vertex < fixture.prepared.drawLinksCount; ++vertex) {
        assertTrue(std::isfinite(fixture.prepared.linkTextureCoords[vertex]), "Boundary patterns are finite");
        for (int component = 0; component < 4; ++component) {
            assertTrue(std::isfinite(fixture.prepared.linkTriangleCoords[4 * vertex + component]), "Boundary coordinates are finite");
        }
    }
    const auto* records = fixture.source.candidateLinks.data();
    const auto* coordinates = fixture.prepared.linkTriangleCoords.data();
    const auto* patterns = fixture.prepared.linkTextureCoords.data();
    fixture.captureWorld();
    fixture.builder.profileBuild(fixture.source, fixture.prepared);
    assertTrue(fixture.source.candidateLinks.data() == records
                    && fixture.prepared.linkTriangleCoords.data() == coordinates
                    && fixture.prepared.linkTextureCoords.data() == patterns
                    && fixture.prepared.drawLinksCount == 840,
            "Repeated legal capture/build reuses high-water Link storage");
    RendererMemoryEstimate estimate;
    assertTrue(tryEstimateRendererMemory(7, 0, sizeof(void*) * CHAR_BIT, estimate)
                    && fixture.source.retainedStorageBytes() + fixture.prepared.retainedStorageBytes() <= estimate.budgetedBytes,
            "Actual maximal Link payload fits the conservative renderer budget");
}

void testSourceCapacityPreflight() {
    for (const int topologyCase : {0, 1, 2}) {
        Fixture fixture(3);
        fixture.addCells(3);
        if (topologyCase == 1) {
            fixture.fillBoundarySlots();
        } else if (topologyCase == 2) {
            auto& topology = fixture.world.mCellData.mTopology;
            for (int index = 0; index < 10; ++index) {
                assertTrue(topology.addCellLink(0, 1, CellLinkCreationReason::UNSPECIFIED, 0.0, Environment::PI, 0.2f, 0.02f) >= 0, "Build mixed capacity fixture");
                assertTrue(topology.addBoundaryLink(0, 0.0, 0.8, 0.2, 0.2f, 0.02f) >= 0, "Build mixed capacity fixture");
                assertTrue(topology.addBoundaryLink(1, 0.0, 0.8, 0.2, 0.2f, 0.02f) >= 0, "Build mixed capacity fixture");
            }
            assertTrue(topology.addBoundaryLink(2, 0.0, 0.8, 0.2, 0.2f, 0.02f) >= 0
                            && topology.validate(),
                    "Mixed fixture has only 31 records but uses 41 adjacency slots");
        } else {
            auto& topology = fixture.world.mCellData.mTopology;
            for (int index = 0; index < 10; ++index) {
                assertTrue(topology.addCellLink(0, 1, CellLinkCreationReason::UNSPECIFIED, 0.0, Environment::PI, 0.2f, 0.02f) >= 0, "Build normal capacity fixture");
                assertTrue(topology.addCellLink(0, 2, CellLinkCreationReason::UNSPECIFIED, 0.0, Environment::PI, 0.2f, 0.02f) >= 0, "Build normal capacity fixture");
            }
            assertTrue(topology.addCellLink(1, 2, CellLinkCreationReason::UNSPECIFIED, 0.0, Environment::PI, 0.2f, 0.02f) >= 0
                            && topology.validate(),
                    "Fixture has 21 legal normal edges before intentional capacity mismatch");
        }
        fixture.captureWorld();
        RenderSourceCapture smallerCapture(2, fixture.source);
        fixture.source.valid = true;
        fixture.source.captureSequence = 77;
        fixture.source.cells.emplace_back();
        fixture.source.cells.back().positionX = 42.0f;
        fixture.source.candidateLinks.emplace_back();
        const auto before = sourceSnapshot(fixture.source);
        // Keep topology intact but intentionally mismatch active/configured capacity.
        fixture.world.mCellData.mCellCount = 2;
        requireException<std::length_error>([&] {
            fixture.captureWorld(&smallerCapture);
        },
                "Invalid topology capacity is rejected before capture writes");
        assertTrue(!fixture.source.valid && sourceSnapshot(fixture.source) == before,
                "Failed preflight invalidates publication without partially overwriting source payload");
    }
    Fixture fixture(3);
    fixture.addCells(1);
    fixture.captureWorld();
    const auto before = sourceSnapshot(fixture.source);
    fixture.world.mFoodData.mFoodCount = 1;
    requireException<std::length_error>([&] {
        fixture.captureWorld();
    },
            "Invalid world auxiliary range is rejected before capture writes");
    assertTrue(!fixture.source.valid && sourceSnapshot(fixture.source) == before,
            "Auxiliary capture preflight preserves old metadata and payload while invalidating publication");
}

void requirePreparedPreflight(Fixture& fixture) {
    const auto before = preparedSnapshot(fixture.prepared);
    requireException<std::length_error>([&] {
        fixture.builder.build(fixture.source, fixture.prepared);
    },
            "Build rejects invalid capacity before writing any geometry");
    assertTrue(preparedSnapshot(fixture.prepared) == before, "Build capacity failure preserves all reusable prepared payload and metadata");
    requireException<std::length_error>([&] {
        fixture.builder.profileBuild(fixture.source, fixture.prepared);
    },
            "Profile build rejects the same capacity before writing any geometry");
    assertTrue(preparedSnapshot(fixture.prepared) == before, "Profile capacity failure preserves all reusable prepared payload and metadata");
}

void testPreparedCandidatePreflight() {
    Fixture fixture(7);
    fixture.addCells(7);
    fixture.fillBoundarySlots();
    fixture.captureWorld();
    fixture.builder.build(fixture.source, fixture.prepared);
    fixture.source.candidateLinks.push_back(fixture.source.candidateLinks.front());
    fixture.source.candidateLinks.front().cellBPositionX = 0.6f;
    fixture.source.cells.front().red = 0.7f;
    ++fixture.source.captureSequence;
    requirePreparedPreflight(fixture);
}

void testAuxiliaryCapacityPreflight() {
    Fixture fixture(7);
    fixture.addCells(1);
    fixture.captureWorld();
    fixture.builder.build(fixture.source, fixture.prepared);
    ++fixture.source.captureSequence;
    fixture.source.cells.front().red = 0.7f;
    fixture.source.sourceFoodCapacity = 0;
    fixture.source.foods.push_back(RenderSourceFood{1});
    requirePreparedPreflight(fixture);
    fixture.source.foods.clear();
    fixture.source.sourceDeadSplashCapacity = 0;
    fixture.source.deadSplashes.push_back(RenderSourceDeadSplash{1});
    requirePreparedPreflight(fixture);
    fixture.source.deadSplashes.clear();
    fixture.source.sourceFoodCapacity = kMaximumUploadedFoodCapacity + 1ULL;
    requirePreparedPreflight(fixture);
    fixture.source.sourceFoodCapacity = 0;
    fixture.source.sourceDeadSplashCapacity = std::numeric_limits<std::uint64_t>::max();
    requirePreparedPreflight(fixture);
}

void testUnsupportedConstructorAndReset() {
    Fixture fixture(7);
    fixture.addCells(1);
    fixture.captureWorld();
    fixture.builder.build(fixture.source, fixture.prepared);
    const auto beforeSource = sourceSnapshot(fixture.source);
    const auto beforePrepared = preparedSnapshot(fixture.prepared);
    requireException<std::length_error>([&] {
        RenderSourceCapture unsupported(kMaximumUploadedCellCapacity + 1, fixture.source);
    },
            "Capture constructor validates before allocating scratch");
    requireException<std::length_error>([&] {
        RenderFrameBuilder unsupported(kMaximumUploadedCellCapacity + 1, fixture.prepared);
    },
            "Builder constructor validates before allocating output");
    requireException<std::length_error>([&] {
        fixture.capture.reset(INT_MAX, fixture.source);
    },
            "Capture reset rejects unsupported layout");
    requireException<std::length_error>([&] {
        fixture.builder.reset(INT_MAX, fixture.prepared);
    },
            "Builder reset rejects unsupported layout");
    assertTrue(fixture.source.valid && sourceSnapshot(fixture.source) == beforeSource
                    && preparedSnapshot(fixture.prepared) == beforePrepared
                    && fixture.capture.cellCapacity() == 7,
            "Invalid construction/reset leaves existing supported payload untouched");
    fixture.captureWorld();
    fixture.builder.build(fixture.source, fixture.prepared);
    assertTrue(fixture.prepared.geometryReady, "Rejected reset leaves the previous capture/builder usable");
}

using TestCase = std::pair<const char*, void (*)()>;
const std::array<TestCase, 8> kCases{{
        {"logical_bounds_and_initial_sizes", testLogicalBoundsAndInitialSizes},
        {"upload_byte_boundaries", testUploadByteBoundaries},
        {"file_and_admission_policy", testFileAndAdmissionPolicy},
        {"boundary_only_full_capacity", testBoundaryOnlyFullCapacity},
        {"source_capacity_preflight", testSourceCapacityPreflight},
        {"prepared_candidate_preflight", testPreparedCandidatePreflight},
        {"auxiliary_capacity_preflight", testAuxiliaryCapacityPreflight},
        {"unsupported_constructor_and_reset", testUnsupportedConstructorAndReset},
}};

} // namespace

int main(int argc, char** argv) {
    const std::string selection = argc > 1 ? argv[1] : "all";
    bool matched = false;
    for (const auto& test : kCases) {
        if (selection != "all" && selection != test.first) {
            continue;
        }
        matched = true;
        test.second();
        std::cout << test.first << " PASS\n";
    }
    assertTrue(matched, "Requested renderer bounds test exists");
    return 0;
}
