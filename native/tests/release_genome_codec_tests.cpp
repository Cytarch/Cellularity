#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/CanonicalGene.h"
#include "core/CellType.h"
#include "core/GeneAccess.h"
#include "formats/GenomeDraftCodec.h"
#include "formats/release/ReleaseGenomeCodec.h"
#include "formats/release/SaveContainerBody.h"
#include "io/DecodeError.h"

namespace {

using namespace cellularity;
using namespace cellularity::save_format::release;

#ifndef CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR
#error "CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR must be defined"
#endif
#ifndef CELLULARITY_RELEASE_FORMAT_REGISTRY_PATH
#error "CELLULARITY_RELEASE_FORMAT_REGISTRY_PATH must be defined"
#endif

const std::filesystem::path TESTDATA_DIRECTORY =
        CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR;
const std::filesystem::path REGISTRY_PATH =
        CELLULARITY_RELEASE_FORMAT_REGISTRY_PATH;

[[noreturn]] void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void assertTrue(bool value, const std::string& message) {
    if (!value)
        fail(message);
}

template <typename Function>
void expectCode(Function&& function,
        DecodeErrorCode expected,
        const std::string& context) {
    try {
        function();
    } catch (const DecodeError& error) {
        if (error.code() != expected) {
            fail(context + ": expected " + decodeErrorCodeName(expected)
                    + ", got " + decodeErrorCodeName(error.code())
                    + " (" + error.what() + ")");
        }
        return;
    } catch (const std::exception& error) {
        fail(context + ": unexpected exception: " + error.what());
    }
    fail(context + ": malformed release genome was accepted");
}

template <typename Function>
void expectOneOf(Function&& function,
        std::initializer_list<DecodeErrorCode> expected,
        const std::string& context) {
    try {
        function();
    } catch (const DecodeError& error) {
        if (std::find(expected.begin(), expected.end(), error.code())
                == expected.end()) {
            fail(context + ": unexpected " + decodeErrorCodeName(error.code())
                    + " (" + error.what() + ")");
        }
        return;
    }
    fail(context + ": malformed release genome was accepted");
}

GenomeData makeGenome(std::size_t geneCount, std::size_t initialIndex = 0) {
    GenomeData genome(geneCount);
    for (std::size_t index = 0; index < geneCount; ++index) {
        const auto materialized = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                GeneIndex(static_cast<GeneIndex::Value>(index)),
                geneCount,
                index == initialIndex);
        assertTrue(materialized.has_value(), "Could not build release test gene");
        genome[index] = *materialized;
        GeneBuilder builder(genome[index]);
        builder.setColor(0, static_cast<float>((index % 11U) + 1U) / 12.0f);
        builder.setColor(1, static_cast<float>((index % 7U) + 1U) / 8.0f);
        builder.setColor(2, static_cast<float>((index % 5U) + 1U) / 6.0f);
        builder.setCellType(index % 2U == 0U
                        ? CellType::PHAGOCYTE
                        : CellType::CILIOCYTE);
    }
    validateGenomeDraft(genome);
    return genome;
}

GenomeData makeDuplicateSelfGenome(std::size_t geneCount) {
    GenomeData genome(geneCount);
    for (std::size_t index = 0; index < geneCount; ++index) {
        const auto materialized = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                GeneIndex(static_cast<GeneIndex::Value>(index)),
                geneCount,
                index == geneCount / 2U);
        assertTrue(materialized.has_value(), "Could not build duplicate test gene");
        genome[index] = *materialized;
    }
    validateGenomeDraft(genome);
    return genome;
}

GenomeData makeGoldenGenome() {
    GenomeData genome = makeGenome(2, 1);
    GeneBuilder first(genome[0]);
    first.setSplitMass(0.1f);
    first.setSplitRatio(0.4f);
    first.setSplitAngle(0.25f);
    first.setChild1Angle(0.5f);
    first.setChild2Angle(0.75f);
    first.setMassFlowPriority(2.0f);
    first.setLinkStrength(3.0f);
    first.setChild1GeneIndex(GeneIndex(0));
    first.setChild2GeneIndex(GeneIndex(1));
    first.setMakesLink(true);
    first.setChild1KeepsLinks(false);
    first.setChild2KeepsLinks(true);
    first.setStaysAlive(true);
    first.setChild1Mirrored(true);
    first.setChild2Mirrored(false);
    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; ++index) {
        first.setParametricVariableA(index, 0.01f * static_cast<float>(index));
        first.setParametricVariableB(
                index,
                index == 0 ? 0.0f : -0.01f * static_cast<float>(index));
        first.setParametricVariableC(index, 0.02f * static_cast<float>(index));
        first.setParametricSignalChannel(index, index % 8);
        first.setParametricFormulaType(index, index % 4);
    }
    first.setVirusFromGeneIndex(GeneIndex(0));
    first.setVirusToGeneIndex(GeneIndex(1));
    first.setIntegerValue(2, 1);
    first.setIntegerValue(3, 2);
    first.setIntegerValue(4, 3);
    first.setIntegerValue(5, 1);
    first.setIntegerValue(6, 2);
    first.setIntegerValue(7, 3);
    first.setIntegerValue(8, 3);
    first.setIntegerValue(9, 5);
    first.setIntegerValue(10, 2);
    first.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 0.5f);
    first.setFloatValue(Gene::FloatType::SMELL_RED, 0.2f);
    first.setFloatValue(Gene::FloatType::SMELL_GREEN, 0.3f);
    first.setFloatValue(Gene::FloatType::SMELL_BLUE, 0.4f);
    first.setFloatValue(Gene::FloatType::SMELL_THRESHOLD, 0.6f);
    first.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.005f);
    first.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.5f);
    validateGenomeDraft(genome);
    return genome;
}

BodyWriteOptions rawOptions() {
    BodyWriteOptions options;
    options.compressionCodec = CompressionCodec::None;
    options.compressionLevel = 0;
    options.adaptiveCompression = false;
    options.constantEncoding = true;
    return options;
}

std::vector<std::uint8_t> readHexFixture(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        fail("Cannot open release golden fixture " + path.string());
    std::string digits;
    char value = 0;
    while (input.get(value)) {
        if (!std::isspace(static_cast<unsigned char>(value)))
            digits.push_back(value);
    }
    if (digits.size() % 2U != 0U)
        fail("Release golden has odd hex length");
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
    if (!output)
        fail("Cannot create release golden fixture " + path.string());
    constexpr std::size_t bytesPerLine = 32;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        output.put(digits[bytes[index] >> 4U]);
        output.put(digits[bytes[index] & 0x0FU]);
        if ((index + 1U) % bytesPerLine == 0U)
            output.put('\n');
    }
    if (bytes.size() % bytesPerLine != 0U)
        output.put('\n');
    output.close();
    if (!output)
        fail("Could not write release golden fixture " + path.string());
}

BootstrapHeader decodeHeader(const std::vector<std::uint8_t>& bytes) {
    return decodeBootstrap(
            std::vector<std::uint8_t>(
                    bytes.begin(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(kBootstrapBytes)),
            bytes.size());
}

void patchHeader(std::vector<std::uint8_t>& bytes,
        const BootstrapHeader& header) {
    const std::vector<std::uint8_t> encoded = encodeBootstrap(header);
    std::copy(encoded.begin(), encoded.end(), bytes.begin());
}

std::vector<MetadataEntry> metadataOf(const std::vector<std::uint8_t>& bytes,
        const BootstrapHeader& header) {
    const std::vector<std::uint8_t> metadata(
            bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset + header.metadataBytes));
    return decodeMetadata(header.documentKind, metadata, header.metadataCrc32);
}

std::vector<SectionDescriptor> directoryOf(
        const std::vector<std::uint8_t>& bytes,
        const BootstrapHeader& header) {
    const std::vector<std::uint8_t> directory(
            bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset + header.directoryBytes));
    return decodeDirectory(directory, header.directoryCrc32, header);
}

void replaceMetadata(std::vector<std::uint8_t>& bytes,
        BootstrapHeader& header,
        const std::vector<MetadataEntry>& metadata) {
    const std::vector<std::uint8_t> encoded = encodeMetadata(
            DocumentKind::Genome, metadata);
    assertTrue(encoded.size() == header.metadataBytes,
            "Malformed test metadata unexpectedly changed byte size");
    std::copy(encoded.begin(), encoded.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset));
    header.metadataCrc32 = crc32(encoded);
    patchHeader(bytes, header);
}

void replaceDirectory(std::vector<std::uint8_t>& bytes,
        BootstrapHeader& header,
        const std::vector<SectionDescriptor>& sections) {
    const std::vector<std::uint8_t> encoded = encodeDirectory(sections);
    assertTrue(encoded.size() == header.directoryBytes,
            "Malformed test directory unexpectedly changed byte size");
    std::copy(encoded.begin(), encoded.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset));
    header.directoryCrc32 = crc32(encoded);
    patchHeader(bytes, header);
}

std::uint64_t readU64(const std::vector<std::uint8_t>& bytes,
        std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        fail("Release test U64 read is out of bounds");
    }
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte])
                << (byte * 8U);
    }
    return value;
}

void writeU16(std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::uint16_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        fail("Release test U16 write is out of bounds");
    }
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

std::size_t findChunkStart(const std::vector<std::uint8_t>& directory,
        const ChunkDescriptor& chunk) {
    for (std::size_t offset = 0; offset + 56U <= directory.size(); ++offset) {
        if (readU64(directory, offset) == chunk.firstLogicalValue
                && readU64(directory, offset + 8U) == chunk.logicalValueCount
                && readU64(directory, offset + 24U) == chunk.fileOffset
                && readU64(directory, offset + 32U) == chunk.storedBytes
                && readU64(directory, offset + 40U) == chunk.decodedBytes) {
            return offset;
        }
    }
    fail("Release test could not locate encoded chunk descriptor");
}

ChunkDescriptor& geneReferenceChunk(std::vector<SectionDescriptor>& sections) {
    for (SectionDescriptor& section : sections) {
        if (section.sectionId != section_id::kGenomes)
            continue;
        for (ColumnDescriptor& column : section.columns) {
            if (column.fieldId == 2)
                return column.chunks.front();
        }
    }
    fail("Release test could not locate gene_refs chunk");
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = line.find('\t', begin);
        fields.push_back(line.substr(begin, end - begin));
        if (end == std::string::npos)
            return fields;
        begin = end + 1;
    }
}

struct RegistryField final {
    std::uint32_t id = 0;
    std::string name;
    std::string type;
};

std::string primitiveName(std::uint8_t wireType) {
    switch (static_cast<PrimitiveType>(wireType)) {
        case PrimitiveType::U8:
            return "u8";
        case PrimitiveType::U16:
            return "u16";
        case PrimitiveType::U32:
            return "u32";
        case PrimitiveType::U64:
            return "u64";
        case PrimitiveType::I32:
            return "i32";
        case PrimitiveType::I64:
            return "i64";
        case PrimitiveType::F32:
            return "f32";
        case PrimitiveType::F64:
            return "f64";
        case PrimitiveType::I8:
            return "i8";
    }
    fail("Release test encountered an unknown primitive type");
}

void testStandaloneColumnsMatchFrozenRegistry() {
    std::ifstream input(REGISTRY_PATH);
    assertTrue(static_cast<bool>(input),
            "Release format registry could not be opened");
    std::map<std::string, std::vector<RegistryField>> expected;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#')
            continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() != 6 || fields[0] != "field"
                || (fields[1] != "genes" && fields[1] != "genomes")) {
            continue;
        }
        expected[fields[1]].push_back(RegistryField{
                static_cast<std::uint32_t>(std::stoul(fields[2])),
                fields[3], fields[4]
        });
    }

    const std::vector<std::uint8_t> bytes = encodeReleaseGenome(
            makeGoldenGenome(), {}, rawOptions());
    const BootstrapHeader header = decodeHeader(bytes);
    const std::vector<SectionDescriptor> sections = directoryOf(bytes, header);
    std::size_t compared = 0;
    for (const SectionDescriptor& section : sections) {
        const auto registered = expected.find(section.canonicalName);
        assertTrue(registered != expected.end(),
                "Standalone release section is absent from the registry");
        assertTrue(registered->second.size() == section.columns.size(),
                "Standalone release column count differs from the registry");
        for (const RegistryField& field : registered->second) {
            const auto column = std::find_if(
                    section.columns.begin(), section.columns.end(),
                    [&](const ColumnDescriptor& candidate) {
                        return candidate.fieldId == field.id;
                    });
            assertTrue(column != section.columns.end()
                            && column->canonicalName == field.name
                            && primitiveName(column->primitiveType) == field.type,
                    "Standalone release column differs from the registry: "
                            + section.canonicalName + "." + field.name);
            ++compared;
        }
    }
    assertTrue(compared == 90,
            "Standalone release codec did not cover all 90 registered columns");
}

void testExactCountsAndCodecs() {
    for (std::size_t count : {1U, 2U, 40U, 150U, 256U}) {
        GenomeData source = makeGenome(count, count / 2U);
        for (CompressionCodec codec : {
                     CompressionCodec::None,
                     CompressionCodec::Zlib,
                     CompressionCodec::Zstd
             }) {
            BodyWriteOptions options;
            options.compressionCodec = codec;
            options.compressionLevel = codec == CompressionCodec::Zstd ? 3 : 6;
            options.adaptiveCompression = codec != CompressionCodec::None;
            const std::vector<std::uint8_t> bytes = encodeReleaseGenome(
                    source, {}, options);
            const GenomeData decoded = decodeReleaseGenome(bytes);
            assertTrue(writeGenomeDraft(decoded) == writeGenomeDraft(source),
                    "Release round trip changed a " + std::to_string(count)
                            + "-gene genome");
            const ReleaseGenomeInspection inspection = inspectReleaseGenome(bytes);
            assertTrue(inspection.geneCount == count
                            && inspection.initialGeneIndex == count / 2U,
                    "Release genome card count changed");
        }
    }
}

void testUniqueRowsAndPreview() {
    const GenomeData source = makeDuplicateSelfGenome(40);
    EncodedPreview preview;
    preview.codec = PreviewCodec::Png;
    preview.pixelWidth = 256;
    preview.pixelHeight = 256;
    preview.bytes = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> bytes = encodeReleaseGenome(
            source, preview, rawOptions());
    const ReleaseGenomeInspection inspection = inspectReleaseGenome(bytes);
    assertTrue(inspection.uniqueGeneCount == 1,
            "Canonical SELF genes did not collapse to one row");
    assertTrue(inspection.header.previewPixelWidth == 256
                    && inspection.header.previewPixelHeight == 256
                    && inspection.previewDisposition
                            == PreviewDisposition::Available,
            "Genome preview descriptor did not round trip");
    assertTrue(writeGenomeDraft(decodeReleaseGenome(bytes))
                    == writeGenomeDraft(source),
            "Deduplicated SELF genes did not materialize by owner");

    bytes[static_cast<std::size_t>(inspection.header.previewOffset)] ^= 1U;
    const ReleaseGenomeInspection damaged = inspectReleaseGenome(bytes);
    assertTrue(damaged.previewDisposition == PreviewDisposition::Placeholder,
            "Corrupt ancillary preview did not fall back to placeholder");
    (void)decodeReleaseGenome(bytes);
}

void testCrossLanguageRawGolden() {
    const std::vector<std::uint8_t> actual = encodeReleaseGenome(
            makeGoldenGenome(), {}, rawOptions());
    const std::vector<std::uint8_t> expected = readHexFixture(
            TESTDATA_DIRECTORY / "genome-v1-golden.hex");
    assertTrue(actual == expected,
            "C++ release genome bytes differ from shared raw golden");
    assertTrue(writeGenomeDraft(decodeReleaseGenome(expected))
                    == writeGenomeDraft(makeGoldenGenome()),
            "Release raw golden changed semantic genome values");
}

void testMalformedCrcAndTruncation() {
    const std::vector<std::uint8_t> canonical = encodeReleaseGenome(
            makeGenome(2, 1), {}, rawOptions());
    expectCode([&] {
        (void)decodeReleaseGenome(std::vector<std::uint8_t>(canonical.begin(),
                canonical.begin() + 100));
    },
            DecodeErrorCode::TruncatedInput, "truncated bootstrap");

    std::vector<std::uint8_t> bootstrap = canonical;
    bootstrap[20] ^= 1U;
    expectCode([&] {
        (void)decodeReleaseGenome(bootstrap);
    },
            DecodeErrorCode::InvalidValue, "bootstrap CRC");

    const BootstrapHeader header = decodeHeader(canonical);
    std::vector<std::uint8_t> metadata = canonical;
    metadata[static_cast<std::size_t>(header.metadataOffset) + 1U] ^= 1U;
    expectCode([&] {
        (void)decodeReleaseGenome(metadata);
    },
            DecodeErrorCode::InvalidValue, "metadata CRC");

    std::vector<std::uint8_t> directory = canonical;
    directory[static_cast<std::size_t>(header.directoryOffset) + 1U] ^= 1U;
    expectCode([&] {
        (void)decodeReleaseGenome(directory);
    },
            DecodeErrorCode::InvalidValue, "directory CRC");

    std::vector<SectionDescriptor> sections = directoryOf(canonical, header);
    const ChunkDescriptor reference = geneReferenceChunk(sections);
    std::vector<std::uint8_t> body = canonical;
    body[static_cast<std::size_t>(reference.fileOffset)] ^= 1U;
    expectCode([&] {
        (void)decodeReleaseGenome(body);
    },
            DecodeErrorCode::InvalidValue, "body CRC");
}

void testMalformedCountsReferencesAndCodec() {
    const std::vector<std::uint8_t> canonical = encodeReleaseGenome(
            makeGenome(2, 1), {}, rawOptions());

    for (std::uint32_t invalidCount : {0U, 257U}) {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        std::vector<MetadataEntry> metadata = metadataOf(bytes, header);
        for (MetadataEntry& entry : metadata) {
            if (entry.fieldId == metadata_field::kGenomeGeneCount) {
                entry.rawBits = invalidCount;
            }
        }
        replaceMetadata(bytes, header, metadata);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                DecodeErrorCode::InvalidCount, "invalid genome count");
    }

    for (const auto& [invalidMajor, expectedCode] : {
                 std::pair<std::uint32_t, DecodeErrorCode>{
                         0U, DecodeErrorCode::InvalidVersion
                 },
                 std::pair<std::uint32_t, DecodeErrorCode>{
                         2U, DecodeErrorCode::NewerSnapshotVersion
                 }
         }) {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        std::vector<MetadataEntry> metadata = metadataOf(bytes, header);
        for (MetadataEntry& entry : metadata) {
            if (entry.fieldId == metadata_field::kContentSchemaMajor) {
                entry.rawBits = invalidMajor;
            }
        }
        replaceMetadata(bytes, header, metadata);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                expectedCode, "invalid content schema major");
    }

    {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        std::vector<MetadataEntry> metadata = metadataOf(bytes, header);
        for (MetadataEntry& entry : metadata) {
            if (entry.fieldId == metadata_field::kGenomeInitialGeneIndex) {
                entry.rawBits = 2;
            }
        }
        replaceMetadata(bytes, header, metadata);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                DecodeErrorCode::InvalidCount, "invalid initial gene");
    }

    {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        std::vector<SectionDescriptor> sections = directoryOf(bytes, header);
        ChunkDescriptor& chunk = geneReferenceChunk(sections);
        bytes[static_cast<std::size_t>(chunk.fileOffset)] = 99;
        const std::vector<std::uint8_t> decoded(
                bytes.begin() + static_cast<std::ptrdiff_t>(chunk.fileOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(chunk.fileOffset + chunk.decodedBytes));
        chunk.decodedCrc32 = crc32(decoded);
        replaceDirectory(bytes, header, sections);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                DecodeErrorCode::InvalidValue, "out-of-range file gene ref");
    }

    {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        std::vector<SectionDescriptor> sections = directoryOf(bytes, header);
        const ChunkDescriptor chunk = geneReferenceChunk(sections);
        std::vector<std::uint8_t> directory(
                bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset + header.directoryBytes));
        writeU16(directory, findChunkStart(directory, chunk) + 16U, 99);
        std::copy(directory.begin(), directory.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset));
        header.directoryCrc32 = crc32(directory);
        patchHeader(bytes, header);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                DecodeErrorCode::UnsupportedFeature, "unknown body codec");
    }

    {
        const BootstrapHeader header = decodeHeader(canonical);
        std::vector<SectionDescriptor> sections = directoryOf(
                canonical, header);
        geneReferenceChunk(sections).storedBytes =
                kMaximumStoredChunkBytes + 1U;
        expectCode([&] {
            (void)encodeDirectory(sections);
        },
                DecodeErrorCode::LimitExceeded, "oversized stored body chunk");
    }
}

void testMalformedNamedMetadataAndDirectory() {
    const std::vector<std::uint8_t> canonical = encodeReleaseGenome(
            makeGenome(2, 1), {}, rawOptions());

    {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        const std::string name = "initial_gene_index";
        const auto metadataEnd = bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset + header.metadataBytes);
        auto found = std::search(
                bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset),
                metadataEnd,
                name.begin(), name.end());
        assertTrue(found != metadataEnd,
                "Malformed test could not locate metadata name");
        *found = static_cast<std::uint8_t>('x');
        const std::vector<std::uint8_t> metadata(
                bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset + header.metadataBytes));
        header.metadataCrc32 = crc32(metadata);
        patchHeader(bytes, header);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                DecodeErrorCode::InvalidValue, "metadata ID/name mismatch with valid CRC");
    }

    {
        std::vector<std::uint8_t> bytes = canonical;
        BootstrapHeader header = decodeHeader(bytes);
        const std::string name = "genes";
        const auto directoryEnd = bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset + header.directoryBytes);
        auto found = std::search(
                bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset),
                directoryEnd,
                name.begin(), name.end());
        assertTrue(found != directoryEnd,
                "Malformed test could not locate section name");
        *found = static_cast<std::uint8_t>('x');
        const std::vector<std::uint8_t> directory(
                bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(header.directoryOffset + header.directoryBytes));
        header.directoryCrc32 = crc32(directory);
        patchHeader(bytes, header);
        expectCode([&] {
            (void)decodeReleaseGenome(bytes);
        },
                DecodeErrorCode::InvalidValue, "directory ID/name mismatch with valid CRC");
    }
}

void testAdditiveSchemaMinorRemainsReadable() {
    const GenomeData source = makeGoldenGenome();
    std::vector<std::uint8_t> bytes = encodeReleaseGenome(
            source, {}, rawOptions());
    BootstrapHeader header = decodeHeader(bytes);
    std::vector<MetadataEntry> metadata = metadataOf(bytes, header);
    for (MetadataEntry& entry : metadata) {
        if (entry.fieldId == metadata_field::kContentSchemaMinor) {
            entry.rawBits = 7;
        }
    }
    replaceMetadata(bytes, header, metadata);
    std::vector<SectionDescriptor> sections = directoryOf(bytes, header);
    for (SectionDescriptor& section : sections) {
        section.schemaMinor = 7;
    }
    replaceDirectory(bytes, header, sections);

    const GenomeData decoded = decodeReleaseGenome(bytes);
    assertTrue(writeGenomeDraft(decoded) == writeGenomeDraft(source),
            "Additive release schema minor changed the genome");
    const ReleaseGenomeInspection inspection = inspectReleaseGenome(bytes);
    assertTrue(inspection.geneCount == source.size(),
            "Additive release schema minor broke metadata inspection");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--emit-golden") {
        writeHexFixture(
                argv[2],
                encodeReleaseGenome(makeGoldenGenome(), {}, rawOptions()));
        return 0;
    }
    if (argc != 1) {
        fail("Usage: cellularity_release_genome_codec_tests "
             "[--emit-golden <path>]");
    }
    testStandaloneColumnsMatchFrozenRegistry();
    testExactCountsAndCodecs();
    testUniqueRowsAndPreview();
    testCrossLanguageRawGolden();
    testMalformedCrcAndTruncation();
    testMalformedCountsReferencesAndCodec();
    testMalformedNamedMetadataAndDirectory();
    testAdditiveSchemaMinorRemainsReadable();
    std::cout << "release genome codec tests passed\n";
    return 0;
}
