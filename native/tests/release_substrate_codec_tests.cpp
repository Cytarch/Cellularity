#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "core/CanonicalGene.h"
#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/GeneAccess.h"
#include "formats/SubstrateFileCodec.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "formats/release/SaveContainerCodec.h"
#include "io/DecodeError.h"
#include "io/Sha256.h"

namespace {

using namespace cellularity;
using namespace cellularity::save_format::release;

#ifndef CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR
#error "CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR must be defined"
#endif

const std::filesystem::path TESTDATA_DIRECTORY =
        CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR;

[[noreturn]] void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void assertTrue(bool value, const std::string& message) {
    if (!value) {
        fail(message);
    }
}

std::vector<std::uint8_t> readHexFixture(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        fail("Cannot open release substrate golden fixture " + path.string());
    }
    std::string digits;
    char value = 0;
    while (input.get(value)) {
        if (!std::isspace(static_cast<unsigned char>(value))) {
            digits.push_back(value);
        }
    }
    if (digits.size() % 2U != 0U) {
        fail("Release substrate golden has odd hex length");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(digits.size() / 2U);
    for (std::size_t index = 0; index < digits.size(); index += 2U) {
        bytes.push_back(static_cast<std::uint8_t>(
                std::stoul(digits.substr(index, 2), nullptr, 16)));
    }
    return bytes;
}

void writeHexFixture(const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        fail("Cannot create release substrate golden fixture " + path.string());
    }
    constexpr std::size_t bytesPerLine = 32;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        output.put(digits[bytes[index] >> 4U]);
        output.put(digits[bytes[index] & 0x0FU]);
        if ((index + 1U) % bytesPerLine == 0U) {
            output.put('\n');
        }
    }
    if (bytes.size() % bytesPerLine != 0U) {
        output.put('\n');
    }
    output.close();
    if (!output) {
        fail("Could not write release substrate golden fixture " + path.string());
    }
}

class ScopedTemporaryFile final {
public:
    ScopedTemporaryFile() {
        static int nextId = 0;
        path = std::filesystem::temp_directory_path()
                / ("cellularity-release-substrate-"
                        + std::to_string(nextId++) + ".substrate");
    }

    ~ScopedTemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    [[nodiscard]] std::vector<std::uint8_t> read() const {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            fail("Could not open temporary release substrate");
        }
        const std::streamoff byteCount = input.tellg();
        if (byteCount < 0) {
            fail("Could not determine temporary release substrate size");
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount));
        input.seekg(0, std::ios::beg);
        input.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            fail("Could not read temporary release substrate");
        }
        return bytes;
    }

    void write(const std::vector<std::uint8_t>& bytes) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            fail("Could not write temporary release substrate");
        }
    }

    std::filesystem::path path;
};

template <typename Function>
void expectDecodeFailure(Function&& function, const std::string& context) {
    try {
        function();
    } catch (const DecodeError&) {
        return;
    } catch (const std::exception& error) {
        fail(context + ": unexpected exception: " + error.what());
    }
    fail(context + ": malformed substrate was accepted");
}

LogicalSection& mutableSection(LogicalSnapshot& snapshot,
        std::uint32_t sectionId) {
    for (LogicalSection& section : snapshot.sections) {
        if (section.sectionId == sectionId) {
            return section;
        }
    }
    fail("Could not find mutable release section");
}

LogicalColumn& mutableColumn(LogicalSnapshot& snapshot,
        std::uint32_t sectionId,
        std::uint32_t fieldId) {
    LogicalSection& section = mutableSection(snapshot, sectionId);
    for (LogicalColumn& column : section.columns) {
        if (column.fieldId == fieldId) {
            return column;
        }
    }
    fail("Could not find mutable release column");
}

template <typename Value>
void writeLittleEndian(LogicalColumn& column,
        std::size_t index,
        Value value) {
    using Bits = std::conditional_t<
            sizeof(Value) == 8,
            std::uint64_t,
            std::conditional_t<
                    sizeof(Value) == 4,
                    std::uint32_t,
                    std::conditional_t<
                            sizeof(Value) == 2,
                            std::uint16_t,
                            std::uint8_t>>>;
    static_assert(sizeof(Bits) == sizeof(Value));
    Bits bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::size_t offset = index * sizeof(Value);
    assertTrue(offset <= column.decodedBytes.size()
                    && column.decodedBytes.size() - offset >= sizeof(Value),
            "Mutable release column write is out of bounds");
    for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
        column.decodedBytes[offset + byte] = static_cast<std::uint8_t>(
                bits >> static_cast<unsigned>(byte * 8U));
    }
}

std::unique_ptr<SimulationWorld> makeWorld();
BodyWriteOptions options(CompressionCodec codec);

template <typename Mutation>
void expectLogicalSnapshotRejected(Mutation&& mutation,
        const std::string& context) {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    LogicalSnapshot snapshot = exportSubstrateRuntimeSoa(*world);
    mutation(snapshot);
    const std::vector<std::uint8_t> bytes = encodeContainer(
            snapshot, options(CompressionCodec::None))
                                                    .bytes;
    expectDecodeFailure(
            [&] {
                (void)decodeReleaseSubstrate(bytes, false);
            },
            context);
}

GenomeData makeGenome(std::size_t count, float colorOffset) {
    GenomeData genome(count);
    for (std::size_t index = 0; index < count; ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto materialized = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                owner,
                count,
                index == 0);
        assertTrue(materialized.has_value(), "Could not make test Gene");
        genome[index] = *materialized;
        GeneBuilder builder(genome[index]);
        builder.setColor(0, colorOffset + 0.01f * static_cast<float>(index));
        builder.setColor(1, 0.2f);
        builder.setColor(2, 0.3f);
        builder.setCellType(index % 2U == 0U
                        ? CellType::PHAGOCYTE
                        : CellType::MYOCYTE);
        builder.setInitialCompatibilityFlag(index == 0);
    }
    return genome;
}

std::unique_ptr<SimulationWorld> makeWorld() {
    Environment environment = Environment::constructDefaultEnvironment(
            8, 8, 1.0);
    environment.mRandom.setSeed(0x102030405060708LL);
    environment.mLightAngle = 0.75;
    environment.mLightAngleCos = std::cos(environment.mLightAngle);
    environment.mLightAngleSin = std::sin(environment.mLightAngle);
    environment.mMutationWeights[0] = 0.125;
    environment.mobileFood = true;
    auto world = std::make_unique<SimulationWorld>(false, environment);
    CellData& cells = world->mCellData;
    cells.resizeActiveCellCount(3);
    const OrganismId firstOrganism = cells.assignNewOrganism(0);
    cells.mOrganismIds[1] = firstOrganism;
    cells.mParentOrganismIds[1] = kUnknownOrganismId;
    const OrganismId secondOrganism =
            cells.assignNewOrganism(2, firstOrganism);
    assertTrue(
            firstOrganism != secondOrganism
                    && cells.validateOrganismIdentities(),
            "Could not initialize test Organism identities");

    const GenomeData shared = makeGenome(3, 0.05f);
    assertTrue(cells.replaceGenome(0, shared, GeneIndex(0)),
            "Could not install first shared Genome");
    assertTrue(cells.replaceGenome(1, shared, GeneIndex(1)),
            "Could not install second shared Genome");
    const GenomeData distinct = makeGenome(1, 0.25f);
    assertTrue(cells.replaceGenome(2, distinct, GeneIndex(0)),
            "Could not install distinct Genome");

    for (int index = 0; index < cells.mCellCount; ++index) {
        cells.mPositionX[index] = -0.25 + 0.25 * index;
        cells.mPositionY[index] = 0.5 - 0.125 * index;
        cells.mPositionZ[index] = 0.01 * index;
        cells.mRadius[index] = 0.012 + 0.002 * index;
        cells.refreshRadiusSq(index);
        cells.mMass[index] = CellData::MASS_PER_AREA
                * cells.mRadius[index] * cells.mRadius[index];
        cells.mNitrates[index] = 0.2 + 0.1 * index;
        cells.mAge[index] = 1.5 * index;
        cells.mLipids[index] = 0.03f * index;
        cells.mColor[index] = {0.1f * index, 0.2f, 0.3f, 1.0f};
        cells.mSignalSubstance[index][0] = 0.5f * index;
    }
    cells.mIsMirrored[1] = true;
    cells.mIsStuck[2] = true;
    const int edge = cells.mTopology.addCellLink(
            0,
            1,
            CellLinkCreationReason::GLUEOCYTE_COLLISION,
            0.1,
            0.2,
            0.3f,
            0.4f);
    assertTrue(edge == 0, "Could not add test topology edge");
    cells.mTopology.setEdgeRenderOffsets(
            edge, 0.01f, 0.02f, 0.03f, 0.04f);
    const int boundary = cells.mTopology.addBoundaryLink(
            2, 0.5, 0.6, 0.7, 0.8f, 0.9f);
    assertTrue(boundary == 0, "Could not add test topology boundary");
    cells.mTopology.setBoundaryRenderStartOffset(
            boundary, 0.11f, 0.12f);

    world->mFoodData.resizeActiveFoodCount(2);
    for (int index = 0; index < world->mFoodData.mFoodCount; ++index) {
        world->mFoodData.mPositionX[index] = 0.15f * index;
        world->mFoodData.mPositionY[index] = -0.2f * index;
        world->mFoodData.mMass[index] = 0.3f + 0.1f * index;
        world->mFoodData.mVelocityX[index] = 0.01f * index;
        world->mFoodData.mVelocityY[index] = -0.02f * index;
        world->mFoodData.mLipidLayer[index] = 0.4f * index;
    }
    assertTrue(world->mDeadSplashData.addDeadSplash(
                       0.1f,
                       -0.2f,
                       0.03f,
                       std::array<float, 4>{0.4f, 0.5f, 0.6f, 0.7f})
                    == 0,
            "Could not add test dead splash");
    world->mTime = 7200.0;
    return world;
}

BodyWriteOptions options(CompressionCodec codec) {
    BodyWriteOptions result;
    result.compressionCodec = codec;
    result.compressionLevel = codec == CompressionCodec::None ? 0 : 3;
    result.adaptiveCompression = codec != CompressionCodec::None;
    result.constantEncoding = true;
    result.decodedChunkBytes = 128;
    return result;
}

std::vector<std::uint8_t> rawGoldenBytes() {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    BodyWriteOptions raw = options(CompressionCodec::None);
    raw.adaptiveCompression = false;
    return encodeReleaseSubstrate(*world, {}, raw).bytes;
}

BootstrapHeader headerOf(const std::vector<std::uint8_t>& bytes) {
    return decodeBootstrap(
            std::vector<std::uint8_t>(
                    bytes.begin(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(kBootstrapBytes)),
            bytes.size());
}

void testSha256() {
    cellularity::io::Sha256 digest;
    static constexpr std::array<std::uint8_t, 3> input{{'a', 'b', 'c'}};
    digest.update(input.data(), input.size());
    assertTrue(
            digest.finishHex()
                    == "ba7816bf8f01cfea414140de5dae2223"
                       "b00361a396177a9cb410ff61f20015ad",
            "Production SHA-256 implementation changed");
}

void testRawGolden() {
    const std::vector<std::uint8_t> actual = rawGoldenBytes();
    const std::vector<std::uint8_t> expected = readHexFixture(
            TESTDATA_DIRECTORY / "substrate-v1-golden.hex");
    assertTrue(actual == expected,
            "Release substrate bytes differ from the frozen v1 golden");
    const std::unique_ptr<SimulationWorld> decoded = decodeReleaseSubstrate(
            expected, false);
    const std::unique_ptr<SimulationWorld> source = makeWorld();
    assertTrue(substrateSemanticDigest(*decoded)
                    == substrateSemanticDigest(*source),
            "Release substrate golden changed semantic world state");
}

void testRoundTripAndSharing() {
    const std::unique_ptr<SimulationWorld> source = makeWorld();
    const OrganismId firstOrganism = source->mCellData.organismId(0);
    const OrganismId secondOrganism = source->mCellData.organismId(2);
    constexpr std::int64_t createdAtUnixMillis = 1700000000000LL;
    source->mCreatedAtUnixMillis = createdAtUnixMillis;
    const auto diagnosticsBefore =
            source->mCellData.genomePool().diagnostics();
    const LogicalSnapshot logical = exportSubstrateRuntimeSoa(*source);
    const auto diagnosticsAfter =
            source->mCellData.genomePool().diagnostics();
    assertTrue(
            diagnosticsBefore.materializedGenomes
                    == diagnosticsAfter.materializedGenomes,
            "Release substrate writer materialized per-Cell Genomes");
    assertTrue(logical.sections.size() == 8,
            "Release substrate must contain eight sections");
    assertTrue(logical.diagnostics.uniqueGenomeCount == 2,
            "Release substrate did not deduplicate equal Genomes");
    assertTrue(logical.diagnostics.avoidedPerCellGeneDataBytes
                    == 7U * sizeof(GeneData),
            "Release writer materialization accounting changed");

    const std::string expectedDigest = substrateSemanticDigest(*source);
    for (CompressionCodec codec : {
                 CompressionCodec::None,
                 CompressionCodec::Zlib,
                 CompressionCodec::Zstd
         }) {
        const ContainerArtifact first = encodeReleaseSubstrate(
                *source, {}, options(codec));
        const ContainerArtifact second = encodeReleaseSubstrate(
                *source, {}, options(codec));
        assertTrue(first.bytes == second.bytes,
                "Release substrate output is not deterministic");
        ScopedTemporaryFile streamedFile;
        const ContainerWriteResult streamed = writeReleaseSubstratePath(
                streamedFile.path.string(), *source, {}, options(codec));
        assertTrue(
                streamedFile.read() == first.bytes
                        && streamed.header.declaredFileBytes
                                == first.bytes.size()
                        && streamed.stats.modeledPeakTemporaryBytes
                                <= 2U * kMaximumDecodedChunkBytes,
                "Streamed release substrate differs from canonical bytes");
        validateReleaseSubstratePath(streamedFile.path.string());
        const std::unique_ptr<SimulationWorld> streamedDecoded =
                decodeReleaseSubstratePath(
                        streamedFile.path.string(), false);
        assertTrue(
                substrateSemanticDigest(*streamedDecoded) == expectedDigest
                        && streamedDecoded->mCellData.genomeId(0)
                                == streamedDecoded->mCellData.genomeId(1),
                "Streamed release substrate load changed semantics or sharing");
        const ReleaseSubstrateInspection inspection =
                inspectReleaseSubstrate(first.bytes);
        assertTrue(inspection.contentSchemaMajor == 1
                        && inspection.contentSchemaMinor == 2
                        && inspection.cellCount == 3
                        && inspection.foodCount == 2
                        && inspection.genomeCount == 2
                        && inspection.uniqueGeneCount == 4
                        && inspection.topologyEdgeCount == 1
                        && inspection.deadSplashCount == 1
                        && inspection.knownOrganismCount == 2
                        && inspection.organismIdentityComplete
                        && inspection.createdAtUnixMillis
                                == createdAtUnixMillis
                        && inspection.previewDisposition
                                == PreviewDisposition::None,
                "Release substrate metadata inspection changed");
        std::unique_ptr<SimulationWorld> decoded = decodeReleaseSubstrate(
                first.bytes, false);
        assertTrue(substrateSemanticDigest(*decoded) == expectedDigest
                        && decoded->mCreatedAtUnixMillis
                                == createdAtUnixMillis,
                "Release substrate semantic digest changed on round trip");
        assertTrue(decoded->mCellData.genomeId(0)
                                == decoded->mCellData.genomeId(1)
                        && decoded->mCellData.genomeReferenceCount(0) == 2,
                "Release substrate did not preserve pooled Genome sharing");
        assertTrue(decoded->mCellData.activeGeneIndex(1) == GeneIndex(1),
                "Release substrate changed a Cell active Gene");
        assertTrue(
                decoded->mCellData.organismId(0) == firstOrganism
                        && decoded->mCellData.organismId(1)
                                == firstOrganism
                        && decoded->mCellData.organismId(2)
                                == secondOrganism
                        && decoded->mCellData.parentOrganismId(2)
                                == firstOrganism
                        && decoded->mCellData.nextOrganismId() == 3,
                "Release substrate changed Organism identities or lineage");
        assertTrue(
                decoded->mCellData.mTopology.edgeCreationReason(0)
                        == CellLinkCreationReason::GLUEOCYTE_COLLISION,
                "Release substrate changed a topology-edge creation reason");
        assertTrue(
                decoded->mCellData.genePool().diagnostics().internLookups
                        == inspection.uniqueGeneCount,
                "Release import did not intern each unique Gene row exactly once");
        const float peerColorBefore =
                decoded->mCellData.getGeneView(1, GeneIndex(0)).color(0);
        GeneData candidate =
                decoded->mCellData.materializeGenome(0).front();
        GeneBuilder(candidate).setColor(0, peerColorBefore + 0.125f);
        bool changed = false;
        assertTrue(
                decoded->mCellData.replaceGene(
                        0, GeneIndex(0), candidate, &changed)
                        && changed
                        && decoded->mCellData.genomeId(0)
                                != decoded->mCellData.genomeId(1)
                        && decoded->mCellData.genomeReferenceCount(0) == 1
                        && decoded->mCellData.genomeReferenceCount(1) == 1
                        && decoded->mCellData.getGeneView(
                                                     1, GeneIndex(0))
                                        .color(0)
                                == peerColorBefore,
                "First mutation after release load did not detach exactly one clone");
    }
}

void testEmptyWorldAndPreview() {
    Environment environment = Environment::constructDefaultEnvironment(
            4, 4, 1.0);
    SimulationWorld world(false, environment);
    EncodedPreview preview;
    preview.codec = PreviewCodec::WebpLossy;
    preview.pixelWidth = 512;
    preview.pixelHeight = 384;
    preview.bytes = {0x52, 0x49, 0x46, 0x46};
    const ContainerArtifact artifact = encodeReleaseSubstrate(
            world, preview, options(CompressionCodec::None));
    const ReleaseSubstrateInspection inspection =
            inspectReleaseSubstrate(artifact.bytes);
    assertTrue(inspection.cellCount == 0
                    && inspection.genomeCount == 0
                    && inspection.uniqueGeneCount == 0
                    && inspection.knownOrganismCount == 0
                    && inspection.organismIdentityComplete
                    && inspection.previewDisposition
                            == PreviewDisposition::Available
                    && inspection.header.previewPixelWidth == 512
                    && inspection.header.previewPixelHeight == 384,
            "Empty release substrate or preview inspection changed");
    const std::unique_ptr<SimulationWorld> decoded = decodeReleaseSubstrate(
            artifact.bytes, true);
    assertTrue(decoded->mCellData.mCellCount == 0
                    && decoded->mEnvironment.mIsPreviewMode,
            "Empty release substrate did not round trip");
}

void testRingGeometryRoundTripsAndRejectsMetadataMismatch() {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    world->mEnvironment.mInnerRadius = 0.1;
    const int innerLink = world->mCellData.mTopology.addBoundaryLink(
            1, Environment::PI, 0.097, 0.0, 0.5f, 0.0f);
    assertTrue(innerLink >= 0, "Could not create inner-wall test link");
    for (CompressionCodec codec : {CompressionCodec::None, CompressionCodec::Zlib}) {
        const auto bytes = encodeReleaseSubstrate(*world, {}, options(codec)).bytes;
        const auto inspection = inspectReleaseSubstrate(bytes);
        assertTrue(inspection.dishInnerRadius == 0.1, "Ring metadata must be readable without decoding the body");
        const auto decoded = decodeReleaseSubstrate(bytes, false);
        assertTrue(decoded->mEnvironment.mRadius == 1.0 && decoded->mEnvironment.mInnerRadius == 0.1, "Ring radii must round trip in memory");
        assertTrue(decoded->mCellData.mTopology.boundaryCount() == world->mCellData.mTopology.boundaryCount(), "Both inner and outer wall attachments must survive saving");
        const ScopedTemporaryFile temporary;
        (void)writeReleaseSubstratePath(temporary.path.string(), *world, {}, options(codec));
        const auto streamed = decodeReleaseSubstratePath(temporary.path.string(), false);
        assertTrue(streamed->mEnvironment.mInnerRadius == 0.1, "Ring radius must round trip through streaming IO");
        const auto legacyCircle = makeWorld();
        const auto circleBytes = encodeReleaseSubstrate(*legacyCircle, {}, options(codec)).bytes;
        assertTrue(decodeReleaseSubstrate(circleBytes, false)->mEnvironment.mInnerRadius == 0.0, "Existing circular files need no ring column or metadata");
    }
    auto snapshot = exportSubstrateRuntimeSoa(*world);
    for (auto& entry : snapshot.metadata) {
        if (entry.fieldId == metadata_field::kSubstrateDishInnerRadius) {
            entry = MetadataEntry::f64(entry.fieldId, "dish_inner_radius", 0.2, false);
        }
    }
    const auto mismatched = encodeContainer(snapshot, options(CompressionCodec::None)).bytes;
    expectDecodeFailure([&] {
        (void)decodeReleaseSubstrate(mismatched, false);
    },
            "Mismatched ring body and metadata must be rejected");
    expectLogicalSnapshotRejected([](LogicalSnapshot& invalid) {
        auto& environment = mutableSection(invalid, section_id::kEnvironment);
        LogicalColumn inner;
        inner.fieldId = 54;
        inner.primitiveType = PrimitiveType::F64;
        inner.canonicalName = "inner_radius";
        inner.logicalValueCount = 1;
        inner.decodedBytes.resize(sizeof(double));
        writeLittleEndian<double>(inner, 0, 0.99);
        environment.columns.push_back(std::move(inner));
    },
            "Ring narrower than one cell must be rejected");
}

void testPartialOrganismIdentityRoundTrip() {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    const OrganismId firstKnownIdentity =
            world->mCellData.organismId(1);
    const OrganismId secondKnownIdentity =
            world->mCellData.organismId(2);
    world->mCellData.mOrganismIds[0] = kUnknownOrganismId;
    world->mCellData.mParentOrganismIds[0] = kUnknownOrganismId;

    const ContainerArtifact artifact = encodeReleaseSubstrate(
            *world, {}, options(CompressionCodec::None));
    const ReleaseSubstrateInspection inspection =
            inspectReleaseSubstrate(artifact.bytes);
    assertTrue(
            inspection.knownOrganismCount == 2
                    && !inspection.organismIdentityComplete,
            "Mixed known and legacy identities expose incomplete metadata");

    const std::unique_ptr<SimulationWorld> decoded = decodeReleaseSubstrate(
            artifact.bytes, false);
    assertTrue(
            decoded->mCellData.organismId(0) == kUnknownOrganismId
                    && decoded->mCellData.parentOrganismId(0)
                            == kUnknownOrganismId
                    && decoded->mCellData.organismId(1)
                            == firstKnownIdentity
                    && decoded->mCellData.organismId(2)
                            == secondKnownIdentity
                    && decoded->mCellData.nextOrganismId() == 3,
            "Release round trip invented or discarded a partial Organism identity");
}

void testMalformedChunkCrc() {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    std::vector<std::uint8_t> bytes = encodeReleaseSubstrate(
            *world, {}, options(CompressionCodec::None))
                                              .bytes;
    const BootstrapHeader header = headerOf(bytes);
    const std::vector<std::uint8_t> directoryBytes(
            bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset + header.directoryBytes));
    const std::vector<SectionDescriptor> sections = decodeDirectory(
            directoryBytes, header.directoryCrc32, header);
    const ChunkDescriptor* target = nullptr;
    for (const SectionDescriptor& section : sections) {
        for (const ColumnDescriptor& column : section.columns) {
            if (!column.chunks.empty() && column.chunks.front().storedBytes != 0) {
                target = &column.chunks.front();
                break;
            }
        }
        if (target != nullptr) {
            break;
        }
    }
    assertTrue(target != nullptr, "Could not find release body chunk");
    bytes[static_cast<std::size_t>(target->fileOffset)] ^= 0x80U;
    expectDecodeFailure(
            [&] {
                (void)decodeReleaseSubstrate(bytes, false);
            },
            "body CRC");

    ScopedTemporaryFile file;
    file.write(bytes);
    expectDecodeFailure(
            [&] {
                validateReleaseSubstratePath(file.path.string());
            },
            "streamed body CRC validation");
    expectDecodeFailure(
            [&] {
                (void)decodeReleaseSubstratePath(
                        file.path.string(), false);
            },
            "streamed body CRC load");
}

void testLargeStreamedContainerMemoryBound() {
    constexpr std::size_t chunkBytes = 256U * 1024U;
    constexpr std::uint64_t logicalValues =
            (16ULL * 1024ULL * 1024ULL) / sizeof(double);

    const std::unique_ptr<SimulationWorld> world = makeWorld();
    const LogicalSnapshot canonical = exportSubstrateRuntimeSoa(*world);
    StreamingLogicalSnapshot streaming;
    streaming.documentKind = DocumentKind::Substrate;
    streaming.metadata = canonical.metadata;

    StreamingLogicalSection cells;
    cells.sectionId = section_id::kCells;
    cells.schemaMajor = 1;
    cells.schemaMinor = 0;
    cells.canonicalName = "cells";
    cells.logicalRowCount = logicalValues;

    StreamingLogicalColumn positions;
    positions.fieldId = 1;
    positions.primitiveType = PrimitiveType::F64;
    positions.canonicalName = "position_x";
    positions.logicalValueCount = logicalValues;
    positions.readRange = [](
                                  std::uint64_t firstValue,
                                  std::uint64_t valueCount,
                                  std::vector<std::uint8_t>& destination) {
        destination.resize(
                static_cast<std::size_t>(valueCount) * sizeof(double));
        for (std::uint64_t local = 0; local < valueCount; ++local) {
            const std::uint64_t value = firstValue + local;
            for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
                destination[static_cast<std::size_t>(local) * sizeof(value)
                        + byte] = static_cast<std::uint8_t>(value >> static_cast<unsigned>(byte * 8U));
            }
        }
    };
    cells.columns.push_back(std::move(positions));
    streaming.sections.push_back(std::move(cells));

    BodyWriteOptions raw = options(CompressionCodec::None);
    raw.decodedChunkBytes = chunkBytes;
    raw.adaptiveCompression = false;
    ScopedTemporaryFile file;
    const ContainerWriteResult written = writeContainerPath(
            file.path.string(), streaming, raw);
    assertTrue(
            written.stats.bodyDecodedBytes
                            == logicalValues * sizeof(double)
                    && written.stats.chunkCount
                            == written.stats.bodyDecodedBytes / chunkBytes
                    && written.stats.fileBytes
                            > written.stats.bodyDecodedBytes
                    && written.stats.modeledPeakTemporaryBytes
                            <= 2U * chunkBytes,
            "Large streamed writer exceeded its chunk-bounded memory model");

    ContainerFileReader reader(file.path.string());
    assertTrue(reader.decodedBodyBytes() == written.stats.bodyDecodedBytes,
            "Large streamed reader changed the decoded body size");
    reader.validateBody();
    assertTrue(reader.peakChunkTemporaryBytes() <= 2U * chunkBytes,
            "Large streamed reader retained more than one chunk pair");
}

void testMalformedLogicalState() {
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                snapshot.sections.erase(snapshot.sections.begin() + 7);
            },
            "missing required section");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint32_t>(
                        mutableColumn(
                                snapshot, section_id::kCells, 23),
                        0,
                        99U);
            },
            "Cell Genome reference");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint16_t>(
                        mutableColumn(
                                snapshot, section_id::kCells, 24),
                        0,
                        99U);
            },
            "Cell active Gene reference");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint64_t>(
                        mutableColumn(
                                snapshot, section_id::kGenomes, 1),
                        1,
                        0U);
            },
            "zero-length Genome CSR range");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint32_t>(
                        mutableColumn(
                                snapshot, section_id::kGenomes, 2),
                        0,
                        999U);
            },
            "out-of-range Gene reference");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint16_t>(
                        mutableColumn(
                                snapshot, section_id::kGenomes, 3),
                        0,
                        99U);
            },
            "out-of-range initial Gene");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint32_t>(
                        mutableColumn(
                                snapshot, section_id::kTopologyEdges, 1),
                        0,
                        99U);
            },
            "out-of-range topology Cell");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint8_t>(
                        mutableColumn(
                                snapshot, section_id::kTopologyEdges, 11),
                        0,
                        255U);
            },
            "unknown topology edge creation reason");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint8_t>(
                        mutableColumn(
                                snapshot, section_id::kCells, 25),
                        0,
                        0x80U);
            },
            "unknown Cell flags");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint64_t>(
                        mutableColumn(
                                snapshot, section_id::kCells, 39),
                        2,
                        kUnknownOrganismId);
            },
            "unknown Organism with a known parent");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint64_t>(
                        mutableColumn(
                                snapshot, section_id::kCells, 39),
                        2,
                        3U);
            },
            "Organism identity beyond the persisted high-water mark");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<double>(
                        mutableColumn(
                                snapshot, section_id::kCells, 1),
                        0,
                        std::numeric_limits<double>::quiet_NaN());
            },
            "nonfinite Cell position");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                for (MetadataEntry& entry : snapshot.metadata) {
                    if (entry.fieldId
                            == metadata_field::kSubstrateCellCount) {
                        ++entry.rawBits;
                        return;
                    }
                }
                fail("Could not find Cell-count metadata");
            },
            "metadata Cell count mismatch");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                for (MetadataEntry& entry : snapshot.metadata) {
                    if (entry.fieldId
                            == metadata_field::kSubstrateKnownOrganismCount) {
                        --entry.rawBits;
                        return;
                    }
                }
                fail("Could not find known-Organism-count metadata");
            },
            "metadata Organism count mismatch");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                for (MetadataEntry& entry : snapshot.metadata) {
                    if (entry.fieldId
                            == metadata_field::kSubstrateOrganismIdentityComplete) {
                        entry.rawBits = 0;
                        return;
                    }
                }
                fail("Could not find Organism-completeness metadata");
            },
            "metadata Organism completeness mismatch");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                for (MetadataEntry& entry : snapshot.metadata) {
                    if (entry.fieldId
                            == metadata_field::kSubstrateNextOrganismId) {
                        entry.rawBits = 2;
                        return;
                    }
                }
                fail("Could not find next-Organism metadata");
            },
            "invalid Organism high-water mark");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint32_t>(
                        mutableColumn(
                                snapshot, section_id::kCells, 23),
                        2,
                        0U);
            },
            "unreferenced Genome row");
    expectLogicalSnapshotRejected(
            [](LogicalSnapshot& snapshot) {
                writeLittleEndian<std::uint32_t>(
                        mutableColumn(
                                snapshot, section_id::kGenomes, 2),
                        3,
                        0U);
            },
            "unreferenced Gene row");

    const std::unique_ptr<SimulationWorld> invalidWorld = makeWorld();
    invalidWorld->mCellData.mParentOrganismIds[0] =
            invalidWorld->mCellData.organismId(2);
    bool inMemoryRejected = false;
    try {
        (void)encodeReleaseSubstrate(
                *invalidWorld, {}, options(CompressionCodec::None));
    } catch (const std::invalid_argument&) {
        inMemoryRejected = true;
    }
    ScopedTemporaryFile streamedFile;
    bool streamedRejected = false;
    try {
        (void)writeReleaseSubstratePath(
                streamedFile.path.string(),
                *invalidWorld,
                {},
                options(CompressionCodec::None));
    } catch (const std::invalid_argument&) {
        streamedRejected = true;
    }
    assertTrue(
            inMemoryRejected && streamedRejected,
            "Release writers accepted invalid Organism lineage state");
}

void testSharedGenomeImportRollback() {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    CellData& cells = world->mCellData;
    const GenomeId beforeFirst = cells.genomeId(0);
    const GenomeId beforeSecond = cells.genomeId(1);
    const GenomePoolDiagnostics diagnosticsBefore =
            cells.genomePool().diagnostics();
    const GeneData gene = makeGenome(1, 0.4f).front();
    const CanonicalGeneResult canonical = canonicalizeGene(
            gene, GeneIndex(0), 1);
    assertTrue(canonical.hasValue(),
            "Could not canonicalize rollback test Gene");
    const bool imported = cells.importSharedGenomes(
            {*canonical.value},
            {0, 1, 2},
            {0, 99},
            {GeneIndex(0), GeneIndex(0)},
            {0, 1, 0},
            {GeneIndex(0), GeneIndex(0), GeneIndex(0)});
    const GenomePoolDiagnostics diagnosticsAfter =
            cells.genomePool().diagnostics();
    assertTrue(!imported
                    && cells.genomeId(0) == beforeFirst
                    && cells.genomeId(1) == beforeSecond
                    && diagnosticsAfter.liveSlots
                            == diagnosticsBefore.liveSlots
                    && diagnosticsAfter.liveGeneReferenceBytes
                            == diagnosticsBefore.liveGeneReferenceBytes,
            "Failed shared-Genome import was not transactional");
}

void testFutureSchemaMetadataAndPreviewRemainInspectable() {
    const std::unique_ptr<SimulationWorld> world = makeWorld();
    LogicalSnapshot snapshot = exportSubstrateRuntimeSoa(*world);
    bool patchedSchema = false;
    for (MetadataEntry& entry : snapshot.metadata) {
        if (entry.fieldId == metadata_field::kContentSchemaMajor) {
            entry.rawBits = 2;
            patchedSchema = true;
            break;
        }
    }
    assertTrue(patchedSchema, "Could not patch future content schema");
    EncodedPreview preview;
    preview.codec = PreviewCodec::WebpLossy;
    preview.pixelWidth = 512;
    preview.pixelHeight = 512;
    preview.bytes = {
            'R', 'I', 'F', 'F', 8, 0, 0, 0,
            'W', 'E', 'B', 'P', 'V', 'P', '8', ' '
    };
    const std::vector<std::uint8_t> bytes = encodeContainer(
            snapshot, options(CompressionCodec::None), preview)
                                                    .bytes;
    ScopedTemporaryFile file;
    {
        std::ofstream output(file.path, std::ios::binary | std::ios::trunc);
        output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        output.close();
        assertTrue(static_cast<bool>(output),
                "Could not write future-schema inspection file");
    }

    const Environment fallback =
            Environment::constructDefaultEnvironment(8, 8, 1.0);
    const NativeSubstrateInspection inspection = inspectSubstratePath(
            file.path.string(), false, fallback);
    assertTrue(
            inspection.formatFamily
                            == NativeSubstrateFormatFamily::ReleaseClty
                    && inspection.familyLocalVersion == 2
                    && inspection.contentSchemaMajor == 2
                    && inspection.contentSchemaMinor == 2
                    && !inspection.loadSupported
                    && inspection.cellCount == 3
                    && inspection.previewPotentiallyUsable,
            "Future release metadata or preview became uninspectable");
    assertTrue(
            readSubstratePreviewPath(file.path.string()) == preview.bytes,
            "Future release preview became unreadable");
    expectDecodeFailure(
            [&] {
                (void)decodeReleaseSubstrate(bytes, false);
            },
            "future release body schema");
}

void testPreviewAttachmentPreservesWorld() {
    const std::unique_ptr<SimulationWorld> source = makeWorld();
    const std::string expectedDigest = substrateSemanticDigest(*source);
    ScopedTemporaryFile file;
    writeSubstratePath(file.path.string(), *source);

    const std::vector<std::uint8_t> webp{
            'R', 'I', 'F', 'F', 8, 0, 0, 0,
            'W', 'E', 'B', 'P', 'V', 'P', '8', ' '
    };
    attachSubstratePreviewPath(file.path.string(), webp);
    const std::vector<std::uint8_t> attached = file.read();
    const ReleaseSubstrateInspection inspection =
            inspectReleaseSubstrate(attached);
    assertTrue(
            inspection.previewDisposition == PreviewDisposition::Available
                    && inspection.header.previewCodec
                            == static_cast<std::uint16_t>(
                                    PreviewCodec::WebpLossy)
                    && inspection.header.previewPixelWidth
                            == kSelectedSubstratePreviewPixels
                    && inspection.header.previewPixelHeight
                            == kSelectedSubstratePreviewPixels
                    && inspection.header.previewBytes == webp.size(),
            "Attached substrate preview descriptor changed");
    assertTrue(
            std::equal(
                    webp.begin(),
                    webp.end(),
                    attached.begin() + static_cast<std::ptrdiff_t>(inspection.header.previewOffset)),
            "Attached substrate preview payload changed");
    assertTrue(
            readSubstratePreviewPath(file.path.string()) == webp,
            "Lazy substrate preview read changed its encoded payload");
    const std::unique_ptr<SimulationWorld> decoded = decodeReleaseSubstrate(
            attached, false);
    assertTrue(
            substrateSemanticDigest(*decoded) == expectedDigest,
            "Attaching a substrate preview changed the saved world");

    {
        std::fstream corrupt(
                file.path,
                std::ios::binary | std::ios::in | std::ios::out);
        assertTrue(static_cast<bool>(corrupt),
                "Could not open preview for corruption test");
        const char corruptedByte = static_cast<char>(
                attached[static_cast<std::size_t>(
                        inspection.header.previewOffset)]
                ^ 0x01U);
        corrupt.seekp(
                static_cast<std::streamoff>(inspection.header.previewOffset),
                std::ios::beg);
        corrupt.write(&corruptedByte, 1);
        corrupt.close();
        assertTrue(static_cast<bool>(corrupt),
                "Could not write preview corruption test byte");
    }
    assertTrue(
            readSubstratePreviewPath(file.path.string()).empty(),
            "Lazy substrate preview read accepted a CRC mismatch");
    const std::vector<std::uint8_t> corruptPreviewFile = file.read();
    assertTrue(
            inspectReleaseSubstrate(corruptPreviewFile).previewDisposition
                    == PreviewDisposition::Placeholder,
            "Corrupt substrate preview was not classified as corrupt");
    const std::unique_ptr<SimulationWorld> decodedWithCorruptPreview =
            decodeReleaseSubstrate(corruptPreviewFile, false);
    assertTrue(
            substrateSemanticDigest(*decodedWithCorruptPreview)
                    == expectedDigest,
            "A corrupt optional preview made the saved world unreadable");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--emit-golden") {
        writeHexFixture(argv[2], rawGoldenBytes());
        return 0;
    }
    if (argc != 1) {
        fail("Usage: cellularity_release_substrate_codec_tests "
             "[--emit-golden <path>]");
    }
    testSha256();
    testRawGolden();
    testRoundTripAndSharing();
    testEmptyWorldAndPreview();
    testRingGeometryRoundTripsAndRejectsMetadataMismatch();
    testPartialOrganismIdentityRoundTrip();
    testMalformedChunkCrc();
    testLargeStreamedContainerMemoryBound();
    testMalformedLogicalState();
    testSharedGenomeImportRollback();
    testFutureSchemaMetadataAndPreviewRemainInspectable();
    testPreviewAttachmentPreservesWorld();
    std::cout << "release substrate codec tests passed\n";
    return 0;
}
