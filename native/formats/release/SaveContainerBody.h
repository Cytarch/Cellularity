#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/release/LogicalSaveSnapshot.h"

namespace cellularity::save_format::release {

constexpr std::size_t kSelectedNormalChunkBytes = 1U * 1024U * 1024U;
constexpr CompressionCodec kSelectedBodyCompressionCodec =
        CompressionCodec::Zstd;
constexpr int kSelectedBodyCompressionLevel = 3;
constexpr PreviewCodec kSelectedSubstratePreviewCodec =
        PreviewCodec::WebpLossy;
constexpr std::uint32_t kSelectedSubstratePreviewPixels = 512;
constexpr int kSelectedSubstratePreviewQuality = 85;
constexpr PreviewCodec kSelectedGenomePreviewCodec = PreviewCodec::Png;
constexpr std::uint32_t kSelectedGenomePreviewPixels = 256;
constexpr std::size_t kMinimumCompressionSavingBytes = 64;
constexpr std::uint32_t kMinimumCompressionSavingPermille = 20; // 2%

struct EncodedPreview final {
    PreviewCodec codec = PreviewCodec::None;
    std::uint32_t pixelWidth = 0;
    std::uint32_t pixelHeight = 0;
    std::vector<std::uint8_t> bytes;
};

struct BodyWriteOptions final {
    std::size_t decodedChunkBytes = kSelectedNormalChunkBytes;
    CompressionCodec compressionCodec = kSelectedBodyCompressionCodec;
    int compressionLevel = kSelectedBodyCompressionLevel;
    bool adaptiveCompression = true;
    bool constantEncoding = true;
};

struct SectionStorageStats final {
    std::uint32_t sectionId = 0;
    std::uint64_t decodedBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint64_t chunkCount = 0;
};

struct ContainerStats final {
    std::uint64_t fileBytes = 0;
    std::uint64_t metadataBytes = 0;
    std::uint64_t previewBytes = 0;
    std::uint64_t bodyDecodedBytes = 0;
    std::uint64_t bodyStoredBytes = 0;
    std::uint64_t directoryBytes = 0;
    std::uint64_t paddingBytes = 0;
    std::uint64_t chunkCount = 0;
    std::uint64_t rawChunkCount = 0;
    std::uint64_t compressedChunkCount = 0;
    std::uint64_t constantChunkCount = 0;
    std::uint64_t modeledPeakTemporaryBytes = 0;
    std::vector<SectionStorageStats> sections;
};

struct ContainerArtifact final {
    BootstrapHeader header;
    ContainerStats stats;
    std::vector<std::uint8_t> bytes;
};

struct DecodedContainer final {
    BootstrapHeader header;
    PreviewDisposition previewDisposition = PreviewDisposition::None;
    LogicalSnapshot snapshot;
};

/**
 * A logical column whose canonical little-endian bytes are produced on
 * demand. The callback must replace `destination` with exactly
 * `valueCount * primitiveWidth` bytes for the requested contiguous range.
 * It is deliberately range based so path writers never need to materialize a
 * complete SoA column.
 */
struct StreamingLogicalColumn final {
    std::uint32_t fieldId = 0;
    PrimitiveType primitiveType = PrimitiveType::U8;
    std::string canonicalName;
    std::uint64_t logicalValueCount = 0;
    std::function<void(std::uint64_t firstValue,
            std::uint64_t valueCount,
            std::vector<std::uint8_t>& destination)>
            readRange;
};

struct StreamingLogicalSection final {
    std::uint32_t sectionId = 0;
    std::uint16_t schemaMajor = 1;
    std::uint16_t schemaMinor = 0;
    std::string canonicalName;
    std::uint64_t logicalRowCount = 0;
    std::vector<StreamingLogicalColumn> columns;
};

struct StreamingLogicalSnapshot final {
    DocumentKind documentKind = DocumentKind::Substrate;
    std::vector<MetadataEntry> metadata;
    std::vector<StreamingLogicalSection> sections;
};

struct ContainerWriteResult final {
    BootstrapHeader header;
    ContainerStats stats;
};

/** Writes canonical chunks directly to a seekable file and patches its header. */
[[nodiscard]] ContainerWriteResult writeContainerPath(
        const std::string& path,
        const StreamingLogicalSnapshot& snapshot,
        const BodyWriteOptions& options = {},
        const EncodedPreview& preview = {});

using DecodedChunkConsumer = std::function<void(
        std::uint64_t firstLogicalValue,
        std::uint64_t logicalValueCount,
        const std::uint8_t* decodedBytes,
        std::size_t decodedByteCount)>;

/**
 * Seekable container index. Construction reads only the bounded bootstrap,
 * metadata, optional preview, and trailing directory. Body chunks are read
 * and decoded only when requested.
 */
class ContainerFileReader final {
public:
    explicit ContainerFileReader(const std::string& path);
    ~ContainerFileReader();

    ContainerFileReader(const ContainerFileReader&) = delete;
    ContainerFileReader& operator=(const ContainerFileReader&) = delete;
    ContainerFileReader(ContainerFileReader&&) noexcept;
    ContainerFileReader& operator=(ContainerFileReader&&) noexcept;

    [[nodiscard]] const BootstrapHeader& header() const noexcept;
    [[nodiscard]] PreviewDisposition previewDisposition() const noexcept;
    [[nodiscard]] const std::vector<MetadataEntry>& metadata() const noexcept;
    [[nodiscard]] const std::vector<SectionDescriptor>& sections() const noexcept;
    [[nodiscard]] std::uint64_t decodedBodyBytes() const noexcept;
    [[nodiscard]] std::uint64_t peakChunkTemporaryBytes() const noexcept;

    /** Decodes and CRC-checks one descriptor, one bounded chunk at a time. */
    void decodeColumn(const ColumnDescriptor& column,
            const DecodedChunkConsumer& consumer);

    /** CRC-checks every understood body chunk without constructing a world. */
    void validateBody();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/** Builds a complete release file, including body chunks. */
[[nodiscard]] ContainerArtifact encodeContainer(
        const LogicalSnapshot& snapshot,
        const BodyWriteOptions& options = {},
        const EncodedPreview& preview = {});

/** Validates and decodes all required body chunks into canonical column bytes. */
[[nodiscard]] DecodedContainer decodeContainer(
        const std::vector<std::uint8_t>& bytes);

} // namespace cellularity::save_format::release
