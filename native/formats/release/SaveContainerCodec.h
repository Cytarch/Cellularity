#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/FormatLimits.h"

namespace cellularity::save_format::release {

// Frozen release-container contract. Public compatibility begins with the
// first published stable content schema.

constexpr std::size_t kBootstrapBytes = 112;
constexpr std::uint16_t kContainerMajor = 1;
constexpr std::uint16_t kContainerMinor = 0;
constexpr std::uint32_t kDirectoryFormatVersion = 1;

constexpr std::uint64_t kMaximumFileBytes =
        format_limits::kMaximumSaveFileBytes;
constexpr std::uint64_t kMaximumMetadataBytes = 64ULL * 1024ULL;
constexpr std::uint32_t kMaximumMetadataEntries = 128;
constexpr std::uint64_t kMaximumPreviewBytes = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumPreviewDimension = 4096;
constexpr std::uint64_t kMaximumPreviewPixels = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumDirectoryBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumSections = 64;
constexpr std::uint32_t kMaximumColumnsPerSection = 512;
constexpr std::uint32_t kMaximumChunks = 65536;
constexpr std::uint64_t kMaximumDecodedChunkBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumStoredChunkBytes = 5ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumNameBytes = 63;

constexpr std::uint8_t kCriticalFlag = 1U;
constexpr std::uint16_t kDescriptorCriticalFlag = 1U;

enum class DocumentKind : std::uint16_t {
    Substrate = 1,
    Genome = 2,
};

enum class MetadataValueType : std::uint8_t {
    U32 = 1,
    U64 = 2,
    I64 = 3,
    F64 = 4,
};

enum class PreviewCodec : std::uint16_t {
    None = 0,
    Png = 1,
    Jpeg = 2,
    WebpLossy = 3,
    WebpLossless = 4,
    Avif = 5,
};

enum class PreviewDisposition : std::uint8_t {
    None,
    Available,
    Placeholder,
};

enum class CompressionCodec : std::uint16_t {
    None = 0,
    Zlib = 1,
    Zstd = 2,
};

enum class PrimitiveType : std::uint8_t {
    U8 = 1,
    U16 = 2,
    U32 = 3,
    U64 = 4,
    I32 = 5,
    I64 = 6,
    F32 = 7,
    F64 = 8,
    I8 = 9,
};

enum class PhysicalEncoding : std::uint8_t {
    Plain = 0,
    Constant = 1,
};

namespace metadata_field {
constexpr std::uint32_t kContentSchemaMajor = 1;
constexpr std::uint32_t kContentSchemaMinor = 2;
constexpr std::uint32_t kSavedAtUnixMillis = 3;

constexpr std::uint32_t kSubstrateSimulationTimeHours = 0x1001;
constexpr std::uint32_t kSubstrateCellCount = 0x1002;
constexpr std::uint32_t kSubstrateFoodCount = 0x1003;
constexpr std::uint32_t kSubstrateFoodMass = 0x1004;
constexpr std::uint32_t kSubstrateDishRadius = 0x1005;
constexpr std::uint32_t kSubstrateMaximumCellCount = 0x1006;
constexpr std::uint32_t kSubstrateMaximumFoodCount = 0x1007;
constexpr std::uint32_t kSubstrateGenomeCount = 0x1008;
constexpr std::uint32_t kSubstrateUniqueGeneCount = 0x1009;
constexpr std::uint32_t kSubstrateTopologyEdgeCount = 0x100A;
constexpr std::uint32_t kSubstrateDeadSplashCount = 0x100B;
constexpr std::uint32_t kSubstrateCreatedAtUnixMillis = 0x100C;
constexpr std::uint32_t kSubstrateKnownOrganismCount = 0x100D;
constexpr std::uint32_t kSubstrateOrganismIdentityComplete = 0x100E;
constexpr std::uint32_t kSubstrateNextOrganismId = 0x100F;
constexpr std::uint32_t kSubstrateDishInnerRadius = 0x1010;

constexpr std::uint32_t kGenomeGeneCount = 0x2001;
constexpr std::uint32_t kGenomeInitialGeneIndex = 0x2002;
constexpr std::uint32_t kGenomeUniqueGeneCount = 0x2003;
constexpr std::uint32_t kGenomeCellTypeMask = 0x2004;
} // namespace metadata_field

namespace section_id {
constexpr std::uint32_t kEnvironment = 1;
constexpr std::uint32_t kGenes = 2;
constexpr std::uint32_t kGenomes = 3;
constexpr std::uint32_t kCells = 4;
constexpr std::uint32_t kTopologyEdges = 5;
constexpr std::uint32_t kTopologyBoundaries = 6;
constexpr std::uint32_t kFood = 7;
constexpr std::uint32_t kDeadSplashes = 8;
} // namespace section_id

struct BootstrapHeader final {
    std::uint16_t containerMajor = kContainerMajor;
    std::uint16_t containerMinor = kContainerMinor;
    DocumentKind documentKind = DocumentKind::Substrate;
    std::uint16_t headerBytes = static_cast<std::uint16_t>(kBootstrapBytes);
    std::uint32_t requiredFeatureBits = 0;
    std::uint32_t optionalFeatureBits = 0;
    std::uint32_t headerFlags = 0;
    std::uint64_t declaredFileBytes = 0;
    std::uint64_t metadataOffset = 0;
    std::uint64_t metadataBytes = 0;
    std::uint32_t metadataCrc32 = 0;
    std::uint16_t previewCodec = static_cast<std::uint16_t>(PreviewCodec::None);
    std::uint16_t previewFlags = 0;
    std::uint32_t previewPixelWidth = 0;
    std::uint32_t previewPixelHeight = 0;
    std::uint64_t previewOffset = 0;
    std::uint64_t previewBytes = 0;
    std::uint32_t previewCrc32 = 0;
    std::uint32_t reserved0 = 0;
    std::uint64_t directoryOffset = 0;
    std::uint64_t directoryBytes = 0;
    std::uint32_t directoryCrc32 = 0;
};

struct MetadataEntry final {
    std::uint32_t fieldId = 0;
    MetadataValueType valueType = MetadataValueType::U32;
    bool critical = false;
    std::string canonicalName;
    std::uint64_t rawBits = 0;

    static MetadataEntry u32(std::uint32_t fieldId,
            std::string canonicalName,
            std::uint32_t value,
            bool critical);
    static MetadataEntry u64(std::uint32_t fieldId,
            std::string canonicalName,
            std::uint64_t value,
            bool critical);
    static MetadataEntry i64(std::uint32_t fieldId,
            std::string canonicalName,
            std::int64_t value,
            bool critical);
    static MetadataEntry f64(std::uint32_t fieldId,
            std::string canonicalName,
            double value,
            bool critical);

    [[nodiscard]] std::uint32_t asU32() const;
    [[nodiscard]] std::uint64_t asU64() const;
    [[nodiscard]] std::int64_t asI64() const;
    [[nodiscard]] double asF64() const;
};

struct ChunkDescriptor final {
    std::uint64_t firstLogicalValue = 0;
    std::uint64_t logicalValueCount = 0;
    std::uint16_t compressionCodec =
            static_cast<std::uint16_t>(CompressionCodec::None);
    std::uint16_t compressionFlags = 0;
    std::uint32_t codecParameter = 0;
    std::uint64_t fileOffset = 0;
    std::uint64_t storedBytes = 0;
    std::uint64_t decodedBytes = 0;
    std::uint32_t decodedCrc32 = 0;
    std::uint32_t reserved = 0;
};

struct ColumnDescriptor final {
    std::uint32_t fieldId = 0;
    std::uint8_t primitiveType = static_cast<std::uint8_t>(PrimitiveType::U8);
    std::uint8_t physicalEncoding =
            static_cast<std::uint8_t>(PhysicalEncoding::Plain);
    std::uint16_t fieldFlags = kDescriptorCriticalFlag;
    std::uint64_t logicalValueCount = 0;
    std::string canonicalName;
    std::vector<ChunkDescriptor> chunks;
};

struct SectionDescriptor final {
    std::uint32_t sectionId = 0;
    std::uint16_t schemaMajor = 1;
    std::uint16_t schemaMinor = 0;
    std::uint32_t sectionFlags = kDescriptorCriticalFlag;
    std::uint64_t logicalRowCount = 0;
    std::string canonicalName;
    std::vector<ColumnDescriptor> columns;
};

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] std::uint32_t crc32(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] std::vector<std::uint8_t> encodeBootstrap(
        const BootstrapHeader& header);
[[nodiscard]] BootstrapHeader decodeBootstrap(
        const std::vector<std::uint8_t>& bootstrapBytes,
        std::uint64_t actualFileBytes);
[[nodiscard]] PreviewDisposition classifyPreview(
        const BootstrapHeader& header,
        const std::vector<std::uint8_t>& storedPreviewBytes) noexcept;

[[nodiscard]] std::vector<std::uint8_t> encodeMetadata(
        DocumentKind documentKind,
        const std::vector<MetadataEntry>& entries);
[[nodiscard]] std::vector<MetadataEntry> decodeMetadata(
        DocumentKind documentKind,
        const std::vector<std::uint8_t>& bytes,
        std::uint32_t expectedCrc32);

[[nodiscard]] std::vector<std::uint8_t> encodeDirectory(
        const std::vector<SectionDescriptor>& sections);
[[nodiscard]] std::vector<SectionDescriptor> decodeDirectory(
        const std::vector<std::uint8_t>& bytes,
        std::uint32_t expectedCrc32,
        const BootstrapHeader& header);

} // namespace cellularity::save_format::release
