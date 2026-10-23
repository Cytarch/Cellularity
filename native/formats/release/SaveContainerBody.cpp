#include "formats/release/SaveContainerBody.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <zlib.h>

#include <zstd.h>

#include "io/DecodeError.h"

namespace cellularity::save_format::release {
namespace {

constexpr std::size_t kAlignment = 8;

[[noreturn]] void fail(DecodeErrorCode code, const std::string& message) {
    throw DecodeError(code, message);
}

std::size_t primitiveWidth(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::U8:
        case PrimitiveType::I8:
            return 1;
        case PrimitiveType::U16:
            return 2;
        case PrimitiveType::U32:
        case PrimitiveType::I32:
        case PrimitiveType::F32:
            return 4;
        case PrimitiveType::U64:
        case PrimitiveType::I64:
        case PrimitiveType::F64:
            return 8;
    }
    fail(DecodeErrorCode::UnsupportedFeature,
            "Release body uses an unsupported primitive type");
}

std::size_t alignUp(std::size_t value) {
    if (value > std::numeric_limits<std::size_t>::max() - (kAlignment - 1U)) {
        throw std::length_error("Release alignment overflow");
    }
    return (value + kAlignment - 1U) & ~(kAlignment - 1U);
}

void padToAlignment(std::vector<std::uint8_t>& bytes,
        ContainerStats& stats) {
    const std::size_t aligned = alignUp(bytes.size());
    stats.paddingBytes += aligned - bytes.size();
    bytes.resize(aligned, 0);
}

std::vector<std::uint8_t> slice(const std::vector<std::uint8_t>& bytes,
        std::uint64_t offset,
        std::uint64_t count,
        const char* label) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        fail(DecodeErrorCode::TruncatedInput,
                std::string("Release ") + label + " range is truncated");
    }
    return std::vector<std::uint8_t>(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
}

bool isConstantRange(const std::vector<std::uint8_t>& bytes,
        std::size_t width) {
    if (bytes.empty()) {
        return false;
    }
    for (std::size_t offset = width; offset < bytes.size(); offset += width) {
        if (!std::equal(bytes.begin(), bytes.begin() + width,
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset))) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> compressZlib(const std::uint8_t* bytes,
        std::size_t size,
        int level) {
    if (size > std::numeric_limits<uLong>::max()) {
        throw std::length_error("Release zlib input exceeds uLong");
    }
    uLongf destinationBytes = compressBound(static_cast<uLong>(size));
    std::vector<std::uint8_t> destination(destinationBytes);
    const int result = compress2(
            destination.data(),
            &destinationBytes,
            bytes,
            static_cast<uLong>(size),
            level);
    if (result != Z_OK) {
        fail(DecodeErrorCode::InvalidCompression,
                "Release zlib compression failed");
    }
    destination.resize(destinationBytes);
    return destination;
}

std::vector<std::uint8_t> decompressZlibExact(
        const std::vector<std::uint8_t>& stored,
        std::size_t decodedBytes) {
    if (stored.size() > std::numeric_limits<uInt>::max()
            || decodedBytes > std::numeric_limits<uInt>::max()) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release zlib chunk exceeds the implementation limit");
    }
    std::vector<std::uint8_t> decoded(decodedBytes);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(stored.data());
    stream.avail_in = static_cast<uInt>(stored.size());
    stream.next_out = decoded.data();
    stream.avail_out = static_cast<uInt>(decoded.size());
    if (inflateInit(&stream) != Z_OK) {
        fail(DecodeErrorCode::InvalidCompression,
                "Release zlib decoder initialization failed");
    }
    const int result = inflate(&stream, Z_FINISH);
    const bool exact = result == Z_STREAM_END
            && stream.total_in == stored.size()
            && stream.total_out == decoded.size()
            && stream.avail_in == 0;
    inflateEnd(&stream);
    if (!exact) {
        fail(DecodeErrorCode::InvalidCompression,
                "Release zlib chunk has an invalid frame or decoded length");
    }
    return decoded;
}

std::vector<std::uint8_t> compressZstd(const std::uint8_t* bytes,
        std::size_t size,
        int level) {
    std::vector<std::uint8_t> destination(ZSTD_compressBound(size));
    const std::size_t result = ZSTD_compress(
            destination.data(), destination.size(), bytes, size, level);
    if (ZSTD_isError(result)) {
        fail(DecodeErrorCode::InvalidCompression,
                std::string("Release zstd compression failed: ")
                        + ZSTD_getErrorName(result));
    }
    destination.resize(result);
    return destination;
}

std::vector<std::uint8_t> decompressZstdExact(
        const std::vector<std::uint8_t>& stored,
        std::size_t decodedBytes) {
    const std::size_t frameBytes = ZSTD_findFrameCompressedSize(
            stored.data(), stored.size());
    if (ZSTD_isError(frameBytes) || frameBytes != stored.size()) {
        fail(DecodeErrorCode::InvalidCompression,
                "Release zstd chunk has trailing or malformed frame data");
    }
    std::vector<std::uint8_t> decoded(decodedBytes);
    const std::size_t result = ZSTD_decompress(
            decoded.data(), decoded.size(), stored.data(), stored.size());
    if (ZSTD_isError(result) || result != decoded.size()) {
        fail(DecodeErrorCode::InvalidCompression,
                "Release zstd chunk has an invalid decoded length");
    }
    return decoded;
}

std::vector<std::uint8_t> compressBytes(const std::uint8_t* bytes,
        std::size_t size,
        CompressionCodec codec,
        int level) {
    switch (codec) {
        case CompressionCodec::None:
            return std::vector<std::uint8_t>(bytes, bytes + size);
        case CompressionCodec::Zlib:
            return compressZlib(bytes, size, level);
        case CompressionCodec::Zstd:
            return compressZstd(bytes, size, level);
    }
    fail(DecodeErrorCode::UnsupportedFeature,
            "Release body requested an unknown compression codec");
}

std::vector<std::uint8_t> decompressBytes(
        const std::vector<std::uint8_t>& stored,
        std::size_t decodedBytes,
        std::uint16_t codec) {
    switch (static_cast<CompressionCodec>(codec)) {
        case CompressionCodec::None:
            if (stored.size() != decodedBytes) {
                fail(DecodeErrorCode::InvalidCompression,
                        "Release raw chunk has the wrong length");
            }
            return stored;
        case CompressionCodec::Zlib:
            return decompressZlibExact(stored, decodedBytes);
        case CompressionCodec::Zstd:
            return decompressZstdExact(stored, decodedBytes);
    }
    fail(DecodeErrorCode::UnsupportedFeature,
            "Release body requires an unknown compression codec");
}

bool compressionWins(std::size_t rawBytes,
        std::size_t storedBytes) {
    if (storedBytes >= rawBytes) {
        return false;
    }
    const std::size_t saving = rawBytes - storedBytes;
    const std::size_t relative =
            (rawBytes * kMinimumCompressionSavingPermille + 999U) / 1000U;
    return saving >= std::max(kMinimumCompressionSavingBytes, relative);
}

struct PendingChunk final {
    ChunkDescriptor descriptor;
    std::vector<std::uint8_t> stored;
};

struct PendingColumn final {
    ColumnDescriptor descriptor;
    std::vector<PendingChunk> chunks;
};

struct PendingSection final {
    SectionDescriptor descriptor;
    std::vector<PendingColumn> columns;
};

std::vector<PendingSection> encodeBodyChunks(
        const LogicalSnapshot& snapshot,
        const BodyWriteOptions& options,
        ContainerStats& stats) {
    if (options.decodedChunkBytes == 0
            || options.decodedChunkBytes > kMaximumDecodedChunkBytes) {
        throw std::invalid_argument(
                "Release decoded chunk size must be within 1..4 MiB");
    }
    std::vector<PendingSection> pendingSections;
    pendingSections.reserve(snapshot.sections.size());
    for (const LogicalSection& logicalSection : snapshot.sections) {
        PendingSection pendingSection;
        pendingSection.descriptor.sectionId = logicalSection.sectionId;
        pendingSection.descriptor.schemaMajor = logicalSection.schemaMajor;
        pendingSection.descriptor.schemaMinor = logicalSection.schemaMinor;
        pendingSection.descriptor.sectionFlags = kDescriptorCriticalFlag;
        pendingSection.descriptor.logicalRowCount = logicalSection.logicalRowCount;
        pendingSection.descriptor.canonicalName = logicalSection.canonicalName;
        SectionStorageStats sectionStats;
        sectionStats.sectionId = logicalSection.sectionId;
        pendingSection.columns.reserve(logicalSection.columns.size());

        for (const LogicalColumn& logicalColumn : logicalSection.columns) {
            const std::size_t width = primitiveWidth(logicalColumn.primitiveType);
            if (logicalColumn.logicalValueCount
                            > std::numeric_limits<std::size_t>::max() / width
                    || logicalColumn.logicalValueCount * width
                            != logicalColumn.decodedBytes.size()) {
                throw std::invalid_argument(
                        "Release logical column byte count is inconsistent");
            }
            PendingColumn pendingColumn;
            pendingColumn.descriptor.fieldId = logicalColumn.fieldId;
            pendingColumn.descriptor.primitiveType =
                    static_cast<std::uint8_t>(logicalColumn.primitiveType);
            pendingColumn.descriptor.fieldFlags = kDescriptorCriticalFlag;
            pendingColumn.descriptor.logicalValueCount =
                    logicalColumn.logicalValueCount;
            pendingColumn.descriptor.canonicalName = logicalColumn.canonicalName;
            const bool constant = options.constantEncoding
                    && logicalColumn.logicalValueCount > 0
                    && isConstantRange(logicalColumn.decodedBytes, width);
            pendingColumn.descriptor.physicalEncoding = static_cast<std::uint8_t>(
                    constant ? PhysicalEncoding::Constant
                             : PhysicalEncoding::Plain);

            const std::size_t valuesPerChunk = std::max<std::size_t>(
                    1, options.decodedChunkBytes / width);
            std::uint64_t firstValue = 0;
            while (firstValue < logicalColumn.logicalValueCount) {
                const std::uint64_t remaining =
                        logicalColumn.logicalValueCount - firstValue;
                const std::uint64_t valueCount = std::min<std::uint64_t>(
                        remaining, valuesPerChunk);
                const std::size_t byteOffset =
                        static_cast<std::size_t>(firstValue) * width;
                const std::size_t decodedBytes =
                        static_cast<std::size_t>(valueCount) * width;
                PendingChunk pendingChunk;
                pendingChunk.descriptor.firstLogicalValue = firstValue;
                pendingChunk.descriptor.logicalValueCount = valueCount;
                pendingChunk.descriptor.decodedBytes = decodedBytes;
                pendingChunk.descriptor.decodedCrc32 = crc32(
                        logicalColumn.decodedBytes.data() + byteOffset,
                        decodedBytes);

                if (constant) {
                    pendingChunk.stored.assign(
                            logicalColumn.decodedBytes.begin(),
                            logicalColumn.decodedBytes.begin()
                                    + static_cast<std::ptrdiff_t>(width));
                    pendingChunk.descriptor.compressionCodec =
                            static_cast<std::uint16_t>(CompressionCodec::None);
                    ++stats.constantChunkCount;
                } else {
                    pendingChunk.stored = compressBytes(
                            logicalColumn.decodedBytes.data() + byteOffset,
                            decodedBytes,
                            options.compressionCodec,
                            options.compressionLevel);
                    CompressionCodec retainedCodec = options.compressionCodec;
                    if (options.compressionCodec != CompressionCodec::None
                            && options.adaptiveCompression
                            && !compressionWins(decodedBytes,
                                    pendingChunk.stored.size())) {
                        pendingChunk.stored.assign(
                                logicalColumn.decodedBytes.begin()
                                        + static_cast<std::ptrdiff_t>(byteOffset),
                                logicalColumn.decodedBytes.begin()
                                        + static_cast<std::ptrdiff_t>(
                                                byteOffset + decodedBytes));
                        retainedCodec = CompressionCodec::None;
                    }
                    pendingChunk.descriptor.compressionCodec =
                            static_cast<std::uint16_t>(retainedCodec);
                    pendingChunk.descriptor.codecParameter =
                            retainedCodec == CompressionCodec::None
                            ? 0U
                            : static_cast<std::uint32_t>(options.compressionLevel);
                    if (retainedCodec == CompressionCodec::None) {
                        ++stats.rawChunkCount;
                    } else {
                        ++stats.compressedChunkCount;
                    }
                }
                pendingChunk.descriptor.storedBytes = pendingChunk.stored.size();
                stats.modeledPeakTemporaryBytes = std::max<std::uint64_t>(
                        stats.modeledPeakTemporaryBytes,
                        decodedBytes + pendingChunk.stored.size());
                sectionStats.decodedBytes += decodedBytes;
                sectionStats.storedBytes += pendingChunk.stored.size();
                ++sectionStats.chunkCount;
                ++stats.chunkCount;
                pendingColumn.chunks.push_back(std::move(pendingChunk));
                firstValue += valueCount;
            }
            pendingSection.columns.push_back(std::move(pendingColumn));
        }
        stats.bodyDecodedBytes += sectionStats.decodedBytes;
        stats.bodyStoredBytes += sectionStats.storedBytes;
        stats.sections.push_back(sectionStats);
        pendingSections.push_back(std::move(pendingSection));
    }
    return pendingSections;
}

void appendStoredChunks(std::vector<std::uint8_t>& file,
        std::vector<PendingSection>& pending,
        ContainerStats& stats) {
    for (PendingSection& section : pending) {
        section.descriptor.columns.reserve(section.columns.size());
        for (PendingColumn& column : section.columns) {
            column.descriptor.chunks.reserve(column.chunks.size());
            for (PendingChunk& chunk : column.chunks) {
                padToAlignment(file, stats);
                chunk.descriptor.fileOffset = file.size();
                file.insert(file.end(), chunk.stored.begin(), chunk.stored.end());
                column.descriptor.chunks.push_back(chunk.descriptor);
            }
            section.descriptor.columns.push_back(std::move(column.descriptor));
        }
    }
}

std::vector<SectionDescriptor> takeDescriptors(
        std::vector<PendingSection>& pending) {
    std::vector<SectionDescriptor> descriptors;
    descriptors.reserve(pending.size());
    for (PendingSection& section : pending) {
        descriptors.push_back(std::move(section.descriptor));
    }
    return descriptors;
}

std::vector<std::uint8_t> expandConstant(
        const std::vector<std::uint8_t>& stored,
        std::size_t width,
        std::size_t decodedBytes) {
    if (stored.size() != width || decodedBytes % width != 0) {
        fail(DecodeErrorCode::InvalidCompression,
                "Release constant chunk shape is invalid");
    }
    std::vector<std::uint8_t> decoded(decodedBytes);
    for (std::size_t offset = 0; offset < decodedBytes; offset += width) {
        std::copy(stored.begin(), stored.end(), decoded.begin() + offset);
    }
    return decoded;
}

void validatePreviewForWrite(const EncodedPreview& preview) {
    if (preview.codec == PreviewCodec::None) {
        if (!preview.bytes.empty() || preview.pixelWidth != 0
                || preview.pixelHeight != 0) {
            throw std::invalid_argument(
                    "Release NONE preview must have no payload or dimensions");
        }
        return;
    }
    if (preview.bytes.empty() || preview.pixelWidth == 0
            || preview.pixelHeight == 0) {
        throw std::invalid_argument(
                "Release encoded preview requires bytes and dimensions");
    }
    if (preview.bytes.size() > kMaximumPreviewBytes
            || preview.pixelWidth > kMaximumPreviewDimension
            || preview.pixelHeight > kMaximumPreviewDimension
            || static_cast<std::uint64_t>(preview.pixelWidth)
                            * preview.pixelHeight
                    > kMaximumPreviewPixels) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release encoded preview exceeds its structural limit");
    }
}

void requireChunkOptions(const BodyWriteOptions& options) {
    if (options.decodedChunkBytes == 0
            || options.decodedChunkBytes > kMaximumDecodedChunkBytes) {
        throw std::invalid_argument(
                "Release decoded chunk size must be within 1..4 MiB");
    }
}

void requireAddressableStreamRange(std::uint64_t offset,
        std::uint64_t count,
        const char* operation) {
    if (offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())
            || count > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamsize>::max())
            || count > kMaximumFileBytes
            || offset > kMaximumFileBytes - count) {
        fail(DecodeErrorCode::LimitExceeded,
                std::string("Release ") + operation
                        + " range is not seekable");
    }
}

void writeExact(std::ostream& output,
        const std::uint8_t* bytes,
        std::size_t count,
        const char* label) {
    if (count == 0) {
        return;
    }
    if (count > static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
        fail(DecodeErrorCode::LimitExceeded,
                std::string("Release ") + label + " is too large to write");
    }
    output.write(reinterpret_cast<const char*>(bytes),
            static_cast<std::streamsize>(count));
    if (!output) {
        fail(DecodeErrorCode::IoFailure,
                std::string("Could not write release ") + label);
    }
}

void readExact(std::istream& input,
        std::uint64_t offset,
        std::uint8_t* bytes,
        std::size_t count,
        const char* label) {
    requireAddressableStreamRange(offset, count, label);
    if (count == 0) {
        return;
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        fail(DecodeErrorCode::IoFailure,
                std::string("Could not seek to release ") + label);
    }
    input.read(reinterpret_cast<char*>(bytes),
            static_cast<std::streamsize>(count));
    if (!input) {
        fail(DecodeErrorCode::IoFailure,
                std::string("Could not read release ") + label);
    }
}

std::vector<std::uint8_t> readExact(
        std::istream& input,
        std::uint64_t offset,
        std::uint64_t count,
        const char* label) {
    if (count > std::numeric_limits<std::size_t>::max()) {
        fail(DecodeErrorCode::LimitExceeded,
                std::string("Release ") + label + " exceeds addressable memory");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));
    readExact(input, offset, bytes.data(), bytes.size(), label);
    return bytes;
}

void appendToStream(std::fstream& output,
        std::uint64_t& position,
        const std::uint8_t* bytes,
        std::size_t count,
        const char* label) {
    requireAddressableStreamRange(position, count, label);
    output.clear();
    output.seekp(static_cast<std::streamoff>(position), std::ios::beg);
    if (!output) {
        fail(DecodeErrorCode::IoFailure,
                std::string("Could not seek release output for ") + label);
    }
    writeExact(output, bytes, count, label);
    position += count;
}

void padStreamToAlignment(std::fstream& output,
        std::uint64_t& position,
        ContainerStats& stats) {
    const std::uint64_t aligned = (position + (kAlignment - 1U))
            & ~static_cast<std::uint64_t>(kAlignment - 1U);
    const std::size_t count = static_cast<std::size_t>(aligned - position);
    static constexpr std::array<std::uint8_t, kAlignment> zeros{};
    appendToStream(output, position, zeros.data(), count, "alignment padding");
    stats.paddingBytes += count;
}

void readStreamingRange(const StreamingLogicalColumn& column,
        std::uint64_t firstValue,
        std::uint64_t valueCount,
        std::size_t width,
        std::vector<std::uint8_t>& destination) {
    if (!column.readRange) {
        throw std::invalid_argument(
                "Release streaming column has no range reader");
    }
    column.readRange(firstValue, valueCount, destination);
    if (valueCount > std::numeric_limits<std::size_t>::max() / width
            || destination.size()
                    != static_cast<std::size_t>(valueCount) * width) {
        throw std::invalid_argument(
                "Release streaming column returned an inconsistent byte count");
    }
}

bool isStreamingColumnConstant(const StreamingLogicalColumn& column,
        const BodyWriteOptions& options,
        std::size_t width,
        std::vector<std::uint8_t>& scratch) {
    if (!options.constantEncoding || column.logicalValueCount == 0) {
        return false;
    }
    const std::uint64_t valuesPerChunk = std::max<std::uint64_t>(
            1, options.decodedChunkBytes / width);
    std::array<std::uint8_t, 8> first{};
    bool haveFirst = false;
    for (std::uint64_t begin = 0; begin < column.logicalValueCount;) {
        const std::uint64_t count = std::min(
                valuesPerChunk, column.logicalValueCount - begin);
        readStreamingRange(column, begin, count, width, scratch);
        if (!haveFirst) {
            std::copy_n(scratch.begin(), width, first.begin());
            haveFirst = true;
        }
        for (std::size_t offset = 0; offset < scratch.size(); offset += width) {
            if (!std::equal(first.begin(), first.begin() + width,
                        scratch.begin() + static_cast<std::ptrdiff_t>(offset))) {
                return false;
            }
        }
        begin += count;
    }
    return true;
}

bool knownPhysicalEncoding(std::uint8_t encoding) {
    return encoding == static_cast<std::uint8_t>(PhysicalEncoding::Plain)
            || encoding == static_cast<std::uint8_t>(PhysicalEncoding::Constant);
}

bool knownPrimitiveType(std::uint8_t type) {
    return type >= static_cast<std::uint8_t>(PrimitiveType::U8)
            && type <= static_cast<std::uint8_t>(PrimitiveType::I8);
}

bool knownCompressionCodec(std::uint16_t codec) {
    return codec == static_cast<std::uint16_t>(CompressionCodec::None)
            || codec == static_cast<std::uint16_t>(CompressionCodec::Zlib)
            || codec == static_cast<std::uint16_t>(CompressionCodec::Zstd);
}

} // namespace

ContainerArtifact encodeContainer(
        const LogicalSnapshot& snapshot,
        const BodyWriteOptions& options,
        const EncodedPreview& preview) {
    validatePreviewForWrite(preview);

    ContainerArtifact artifact;
    const std::vector<std::uint8_t> metadata = encodeMetadata(
            snapshot.documentKind, snapshot.metadata);
    artifact.stats.metadataBytes = metadata.size();
    artifact.stats.previewBytes = preview.bytes.size();
    std::vector<PendingSection> pending = encodeBodyChunks(
            snapshot, options, artifact.stats);

    artifact.bytes.resize(kBootstrapBytes, 0);
    artifact.header.documentKind = snapshot.documentKind;
    artifact.header.metadataOffset = artifact.bytes.size();
    artifact.header.metadataBytes = metadata.size();
    artifact.header.metadataCrc32 = crc32(metadata);
    artifact.bytes.insert(artifact.bytes.end(), metadata.begin(), metadata.end());

    if (preview.codec != PreviewCodec::None) {
        padToAlignment(artifact.bytes, artifact.stats);
        artifact.header.previewCodec = static_cast<std::uint16_t>(preview.codec);
        artifact.header.previewPixelWidth = preview.pixelWidth;
        artifact.header.previewPixelHeight = preview.pixelHeight;
        artifact.header.previewOffset = artifact.bytes.size();
        artifact.header.previewBytes = preview.bytes.size();
        artifact.header.previewCrc32 = crc32(preview.bytes);
        artifact.bytes.insert(
                artifact.bytes.end(), preview.bytes.begin(), preview.bytes.end());
    }

    appendStoredChunks(artifact.bytes, pending, artifact.stats);
    std::vector<SectionDescriptor> descriptors = takeDescriptors(pending);
    const std::vector<std::uint8_t> directory = encodeDirectory(descriptors);
    padToAlignment(artifact.bytes, artifact.stats);
    artifact.header.directoryOffset = artifact.bytes.size();
    artifact.header.directoryBytes = directory.size();
    artifact.header.directoryCrc32 = crc32(directory);
    artifact.stats.directoryBytes = directory.size();
    artifact.bytes.insert(
            artifact.bytes.end(), directory.begin(), directory.end());
    if (artifact.bytes.size() > kMaximumFileBytes) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release encoded file exceeds the 2 GiB limit");
    }
    artifact.header.declaredFileBytes = artifact.bytes.size();
    const std::vector<std::uint8_t> bootstrap = encodeBootstrap(artifact.header);
    std::copy(bootstrap.begin(), bootstrap.end(), artifact.bytes.begin());
    artifact.stats.fileBytes = artifact.bytes.size();
    return artifact;
}

DecodedContainer decodeContainer(
        const std::vector<std::uint8_t>& bytes) {
    const std::vector<std::uint8_t> bootstrap = slice(
            bytes, 0, std::min<std::size_t>(bytes.size(), kBootstrapBytes),
            "bootstrap");
    DecodedContainer result;
    result.header = decodeBootstrap(bootstrap, bytes.size());
    const std::vector<std::uint8_t> metadata = slice(
            bytes,
            result.header.metadataOffset,
            result.header.metadataBytes,
            "metadata");
    result.snapshot.documentKind = result.header.documentKind;
    result.snapshot.metadata = decodeMetadata(
            result.header.documentKind,
            metadata,
            result.header.metadataCrc32);

    std::vector<std::uint8_t> preview;
    if (result.header.previewBytes != 0) {
        preview = slice(bytes,
                result.header.previewOffset,
                result.header.previewBytes,
                "preview");
    }
    result.previewDisposition = classifyPreview(result.header, preview);
    const std::vector<std::uint8_t> directory = slice(
            bytes,
            result.header.directoryOffset,
            result.header.directoryBytes,
            "directory");
    const std::vector<SectionDescriptor> sections = decodeDirectory(
            directory, result.header.directoryCrc32, result.header);
    result.snapshot.sections.reserve(sections.size());

    std::uint64_t totalDecodedBytes = 0;
    for (const SectionDescriptor& descriptor : sections) {
        if ((descriptor.sectionFlags & kDescriptorCriticalFlag) == 0) {
            // This decoder has no optional sections in its schema. Their
            // ranges were validated by decodeDirectory(), so an older reader
            // can safely ignore them without understanding their codecs.
            continue;
        }
        LogicalSection section;
        section.sectionId = descriptor.sectionId;
        section.schemaMajor = descriptor.schemaMajor;
        section.schemaMinor = descriptor.schemaMinor;
        section.canonicalName = descriptor.canonicalName;
        section.logicalRowCount = descriptor.logicalRowCount;
        section.columns.reserve(descriptor.columns.size());
        for (const ColumnDescriptor& columnDescriptor : descriptor.columns) {
            if ((columnDescriptor.fieldFlags & kDescriptorCriticalFlag) == 0) {
                // The initial schema contains only required columns. Future
                // optional columns remain skippable even when their physical
                // encoding or compression codec is unknown to this build.
                continue;
            }
            LogicalColumn column;
            column.fieldId = columnDescriptor.fieldId;
            column.primitiveType = static_cast<PrimitiveType>(
                    columnDescriptor.primitiveType);
            column.canonicalName = columnDescriptor.canonicalName;
            column.logicalValueCount = columnDescriptor.logicalValueCount;
            const std::size_t width = primitiveWidth(column.primitiveType);
            if (column.logicalValueCount
                    > std::numeric_limits<std::size_t>::max() / width) {
                fail(DecodeErrorCode::LimitExceeded,
                        "Release decoded column exceeds addressable memory");
            }
            column.decodedBytes.reserve(
                    static_cast<std::size_t>(column.logicalValueCount) * width);
            for (const ChunkDescriptor& chunk : columnDescriptor.chunks) {
                const std::vector<std::uint8_t> stored = slice(
                        bytes, chunk.fileOffset, chunk.storedBytes, "body chunk");
                std::vector<std::uint8_t> decoded;
                if (columnDescriptor.physicalEncoding
                        == static_cast<std::uint8_t>(PhysicalEncoding::Constant)) {
                    decoded = expandConstant(
                            stored, width,
                            static_cast<std::size_t>(chunk.decodedBytes));
                } else {
                    decoded = decompressBytes(
                            stored,
                            static_cast<std::size_t>(chunk.decodedBytes),
                            chunk.compressionCodec);
                }
                if (crc32(decoded) != chunk.decodedCrc32) {
                    fail(DecodeErrorCode::InvalidValue,
                            "Release decoded body chunk CRC does not match");
                }
                if (decoded.size()
                        > std::numeric_limits<std::uint64_t>::max()
                                - totalDecodedBytes) {
                    fail(DecodeErrorCode::LimitExceeded,
                            "Release total decoded body size overflowed");
                }
                totalDecodedBytes += decoded.size();
                if (totalDecodedBytes > kMaximumFileBytes) {
                    fail(DecodeErrorCode::LimitExceeded,
                            "Release decoded body exceeds the 2 GiB limit");
                }
                column.decodedBytes.insert(
                        column.decodedBytes.end(), decoded.begin(), decoded.end());
            }
            section.columns.push_back(std::move(column));
        }
        result.snapshot.sections.push_back(std::move(section));
    }
    result.snapshot.diagnostics.canonicalDecodedBodyBytes = totalDecodedBytes;
    const LogicalSection* cells = findLogicalSection(
            result.snapshot, section_id::kCells);
    const LogicalSection* genomes = findLogicalSection(
            result.snapshot, section_id::kGenomes);
    const LogicalSection* genes = findLogicalSection(
            result.snapshot, section_id::kGenes);
    result.snapshot.diagnostics.cellCount =
            cells == nullptr ? 0 : cells->logicalRowCount;
    result.snapshot.diagnostics.uniqueGenomeCount =
            genomes == nullptr ? 0 : genomes->logicalRowCount;
    result.snapshot.diagnostics.uniqueGeneCount =
            genes == nullptr ? 0 : genes->logicalRowCount;
    if (genomes != nullptr) {
        const LogicalColumn* references = findLogicalColumn(*genomes, 2);
        result.snapshot.diagnostics.totalGeneReferences =
                references == nullptr ? 0 : references->logicalValueCount;
    }
    return result;
}

ContainerWriteResult writeContainerPath(
        const std::string& path,
        const StreamingLogicalSnapshot& snapshot,
        const BodyWriteOptions& options,
        const EncodedPreview& preview) {
    if (path.empty()) {
        throw std::invalid_argument("Release output path is empty");
    }
    requireChunkOptions(options);
    validatePreviewForWrite(preview);

    ContainerWriteResult result;
    const std::vector<std::uint8_t> metadata = encodeMetadata(
            snapshot.documentKind, snapshot.metadata);
    result.stats.metadataBytes = metadata.size();
    result.stats.previewBytes = preview.bytes.size();

    std::fstream output(
            path,
            std::ios::binary | std::ios::in | std::ios::out
                    | std::ios::trunc);
    if (!output) {
        fail(DecodeErrorCode::IoFailure,
                "Could not open release output file");
    }

    std::uint64_t position = 0;
    const std::vector<std::uint8_t> bootstrapPlaceholder(kBootstrapBytes, 0);
    appendToStream(output, position,
            bootstrapPlaceholder.data(), bootstrapPlaceholder.size(),
            "bootstrap placeholder");
    result.header.documentKind = snapshot.documentKind;
    result.header.metadataOffset = position;
    result.header.metadataBytes = metadata.size();
    result.header.metadataCrc32 = crc32(metadata);
    appendToStream(output, position, metadata.data(), metadata.size(),
            "metadata");

    if (preview.codec != PreviewCodec::None) {
        padStreamToAlignment(output, position, result.stats);
        result.header.previewCodec = static_cast<std::uint16_t>(preview.codec);
        result.header.previewPixelWidth = preview.pixelWidth;
        result.header.previewPixelHeight = preview.pixelHeight;
        result.header.previewOffset = position;
        result.header.previewBytes = preview.bytes.size();
        result.header.previewCrc32 = crc32(preview.bytes);
        appendToStream(output, position,
                preview.bytes.data(), preview.bytes.size(), "preview");
    }

    std::vector<SectionDescriptor> descriptors;
    descriptors.reserve(snapshot.sections.size());
    std::vector<std::uint8_t> decoded;
    for (const StreamingLogicalSection& sourceSection : snapshot.sections) {
        SectionDescriptor section;
        section.sectionId = sourceSection.sectionId;
        section.schemaMajor = sourceSection.schemaMajor;
        section.schemaMinor = sourceSection.schemaMinor;
        section.sectionFlags = kDescriptorCriticalFlag;
        section.logicalRowCount = sourceSection.logicalRowCount;
        section.canonicalName = sourceSection.canonicalName;
        section.columns.reserve(sourceSection.columns.size());
        SectionStorageStats sectionStats;
        sectionStats.sectionId = sourceSection.sectionId;

        for (const StreamingLogicalColumn& sourceColumn : sourceSection.columns) {
            const std::size_t width = primitiveWidth(sourceColumn.primitiveType);
            if (sourceColumn.logicalValueCount
                    > std::numeric_limits<std::size_t>::max() / width) {
                fail(DecodeErrorCode::LimitExceeded,
                        "Release streaming column exceeds addressable memory");
            }

            ColumnDescriptor column;
            column.fieldId = sourceColumn.fieldId;
            column.primitiveType = static_cast<std::uint8_t>(
                    sourceColumn.primitiveType);
            column.fieldFlags = kDescriptorCriticalFlag;
            column.logicalValueCount = sourceColumn.logicalValueCount;
            column.canonicalName = sourceColumn.canonicalName;
            const bool constant = isStreamingColumnConstant(
                    sourceColumn, options, width, decoded);
            column.physicalEncoding = static_cast<std::uint8_t>(
                    constant ? PhysicalEncoding::Constant
                             : PhysicalEncoding::Plain);

            const std::uint64_t valuesPerChunk = std::max<std::uint64_t>(
                    1, options.decodedChunkBytes / width);
            for (std::uint64_t firstValue = 0;
                    firstValue < sourceColumn.logicalValueCount;) {
                const std::uint64_t valueCount = std::min(
                        valuesPerChunk,
                        sourceColumn.logicalValueCount - firstValue);
                readStreamingRange(
                        sourceColumn, firstValue, valueCount, width, decoded);

                ChunkDescriptor chunk;
                chunk.firstLogicalValue = firstValue;
                chunk.logicalValueCount = valueCount;
                chunk.decodedBytes = decoded.size();
                chunk.decodedCrc32 = crc32(decoded);
                std::vector<std::uint8_t> stored;
                if (constant) {
                    stored.assign(decoded.begin(), decoded.begin() + width);
                    chunk.compressionCodec = static_cast<std::uint16_t>(
                            CompressionCodec::None);
                    ++result.stats.constantChunkCount;
                } else {
                    stored = compressBytes(
                            decoded.data(), decoded.size(),
                            options.compressionCodec,
                            options.compressionLevel);
                    CompressionCodec retained = options.compressionCodec;
                    if (retained != CompressionCodec::None
                            && options.adaptiveCompression
                            && !compressionWins(decoded.size(), stored.size())) {
                        stored = decoded;
                        retained = CompressionCodec::None;
                    }
                    chunk.compressionCodec = static_cast<std::uint16_t>(retained);
                    chunk.codecParameter = retained == CompressionCodec::None
                            ? 0U
                            : static_cast<std::uint32_t>(
                                      options.compressionLevel);
                    if (retained == CompressionCodec::None) {
                        ++result.stats.rawChunkCount;
                    } else {
                        ++result.stats.compressedChunkCount;
                    }
                }
                chunk.storedBytes = stored.size();
                padStreamToAlignment(output, position, result.stats);
                chunk.fileOffset = position;
                appendToStream(output, position,
                        stored.data(), stored.size(), "body chunk");
                result.stats.modeledPeakTemporaryBytes = std::max(
                        result.stats.modeledPeakTemporaryBytes,
                        static_cast<std::uint64_t>(
                                decoded.size() + stored.size()));
                sectionStats.decodedBytes += decoded.size();
                sectionStats.storedBytes += stored.size();
                ++sectionStats.chunkCount;
                ++result.stats.chunkCount;
                column.chunks.push_back(chunk);
                firstValue += valueCount;
            }
            section.columns.push_back(std::move(column));
        }
        result.stats.bodyDecodedBytes += sectionStats.decodedBytes;
        result.stats.bodyStoredBytes += sectionStats.storedBytes;
        result.stats.sections.push_back(sectionStats);
        descriptors.push_back(std::move(section));
    }

    const std::vector<std::uint8_t> directory = encodeDirectory(descriptors);
    padStreamToAlignment(output, position, result.stats);
    result.header.directoryOffset = position;
    result.header.directoryBytes = directory.size();
    result.header.directoryCrc32 = crc32(directory);
    result.stats.directoryBytes = directory.size();
    appendToStream(output, position,
            directory.data(), directory.size(), "directory");
    if (position > kMaximumFileBytes) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release encoded file exceeds the 2 GiB limit");
    }
    result.header.declaredFileBytes = position;
    const std::vector<std::uint8_t> bootstrap = encodeBootstrap(result.header);
    output.clear();
    output.seekp(0, std::ios::beg);
    writeExact(output, bootstrap.data(), bootstrap.size(), "bootstrap");
    output.flush();
    if (!output) {
        fail(DecodeErrorCode::IoFailure,
                "Could not flush release output file");
    }
    output.close();
    if (!output) {
        fail(DecodeErrorCode::IoFailure,
                "Could not close release output file");
    }
    result.stats.fileBytes = position;
    return result;
}

class ContainerFileReader::Impl final {
public:
    explicit Impl(const std::string& path)
            : input(path, std::ios::binary | std::ios::ate) {
        if (!input) {
            fail(DecodeErrorCode::IoFailure,
                    "Could not open release input file");
        }
        const std::streampos end = input.tellg();
        if (end < 0) {
            fail(DecodeErrorCode::IoFailure,
                    "Could not determine release input size");
        }
        const std::uint64_t fileBytes = static_cast<std::uint64_t>(end);
        if (fileBytes > kMaximumFileBytes) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Release input exceeds the 2 GiB limit");
        }
        const std::vector<std::uint8_t> bootstrap = readExact(
                input, 0, std::min<std::uint64_t>(fileBytes, kBootstrapBytes),
                "bootstrap");
        header = decodeBootstrap(bootstrap, fileBytes);
        const std::vector<std::uint8_t> metadataBytes = readExact(
                input, header.metadataOffset, header.metadataBytes, "metadata");
        metadata = decodeMetadata(
                header.documentKind, metadataBytes, header.metadataCrc32);

        std::vector<std::uint8_t> preview;
        if (header.previewBytes != 0) {
            preview = readExact(
                    input, header.previewOffset, header.previewBytes, "preview");
        }
        previewState = classifyPreview(header, preview);

        const std::vector<std::uint8_t> directory = readExact(
                input, header.directoryOffset, header.directoryBytes,
                "directory");
        sections = decodeDirectory(
                directory, header.directoryCrc32, header);

        for (const SectionDescriptor& section : sections) {
            for (const ColumnDescriptor& column : section.columns) {
                for (const ChunkDescriptor& chunk : column.chunks) {
                    if (chunk.decodedBytes
                            > kMaximumFileBytes - totalDecodedBytes) {
                        fail(DecodeErrorCode::LimitExceeded,
                                "Release decoded body exceeds the 2 GiB limit");
                    }
                    totalDecodedBytes += chunk.decodedBytes;
                }
            }
        }
    }

    void decode(const ColumnDescriptor& column,
            const DecodedChunkConsumer& consumer) {
        const std::size_t width = primitiveWidth(
                static_cast<PrimitiveType>(column.primitiveType));
        for (const ChunkDescriptor& chunk : column.chunks) {
            const std::vector<std::uint8_t> stored = readExact(
                    input, chunk.fileOffset, chunk.storedBytes, "body chunk");
            std::vector<std::uint8_t> decoded;
            if (column.physicalEncoding
                    == static_cast<std::uint8_t>(PhysicalEncoding::Constant)) {
                decoded = expandConstant(
                        stored, width,
                        static_cast<std::size_t>(chunk.decodedBytes));
            } else {
                decoded = decompressBytes(
                        stored,
                        static_cast<std::size_t>(chunk.decodedBytes),
                        chunk.compressionCodec);
            }
            if (crc32(decoded) != chunk.decodedCrc32) {
                fail(DecodeErrorCode::InvalidValue,
                        "Release decoded body chunk CRC does not match");
            }
            peakTemporaryBytes = std::max(
                    peakTemporaryBytes,
                    static_cast<std::uint64_t>(
                            stored.size() + decoded.size()));
            consumer(chunk.firstLogicalValue,
                    chunk.logicalValueCount,
                    decoded.data(), decoded.size());
        }
    }

    std::ifstream input;
    BootstrapHeader header;
    PreviewDisposition previewState = PreviewDisposition::None;
    std::vector<MetadataEntry> metadata;
    std::vector<SectionDescriptor> sections;
    std::uint64_t totalDecodedBytes = 0;
    std::uint64_t peakTemporaryBytes = 0;
};

ContainerFileReader::ContainerFileReader(const std::string& path)
        : impl_(std::make_unique<Impl>(path)) {
}

ContainerFileReader::~ContainerFileReader() = default;
ContainerFileReader::ContainerFileReader(ContainerFileReader&&) noexcept = default;
ContainerFileReader& ContainerFileReader::operator=(
        ContainerFileReader&&) noexcept = default;

const BootstrapHeader& ContainerFileReader::header() const noexcept {
    return impl_->header;
}

PreviewDisposition ContainerFileReader::previewDisposition() const noexcept {
    return impl_->previewState;
}

const std::vector<MetadataEntry>& ContainerFileReader::metadata() const noexcept {
    return impl_->metadata;
}

const std::vector<SectionDescriptor>& ContainerFileReader::sections() const noexcept {
    return impl_->sections;
}

std::uint64_t ContainerFileReader::decodedBodyBytes() const noexcept {
    return impl_->totalDecodedBytes;
}

std::uint64_t ContainerFileReader::peakChunkTemporaryBytes() const noexcept {
    return impl_->peakTemporaryBytes;
}

void ContainerFileReader::decodeColumn(
        const ColumnDescriptor& column,
        const DecodedChunkConsumer& consumer) {
    impl_->decode(column, consumer);
}

void ContainerFileReader::validateBody() {
    const DecodedChunkConsumer discard = [](
                                                 std::uint64_t, std::uint64_t, const std::uint8_t*, std::size_t) {};
    for (const SectionDescriptor& section : impl_->sections) {
        const bool sectionCritical =
                (section.sectionFlags & kDescriptorCriticalFlag) != 0;
        for (const ColumnDescriptor& column : section.columns) {
            const bool columnCritical =
                    (column.fieldFlags & kDescriptorCriticalFlag) != 0;
            const bool understood = knownPrimitiveType(column.primitiveType)
                    && knownPhysicalEncoding(column.physicalEncoding)
                    && std::all_of(
                            column.chunks.begin(), column.chunks.end(),
                            [](const ChunkDescriptor& chunk) {
                                return knownCompressionCodec(
                                        chunk.compressionCodec);
                            });
            if (!understood) {
                if (sectionCritical && columnCritical) {
                    fail(DecodeErrorCode::UnsupportedFeature,
                            "Release critical body column is unsupported");
                }
                continue;
            }
            impl_->decode(column, discard);
        }
    }
}

} // namespace cellularity::save_format::release
