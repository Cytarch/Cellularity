#include "formats/SubstrateFileCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/SimulationWorld.h"
#include "core/SimulationMemoryPolicy.h"
#include "formats/FormatLimits.h"
#include "formats/InternalWireFormatConstants.h"
#include "formats/LegacySubstrateCodec.h"
#include "formats/SnapshotCodecSupport.h"
#include "formats/SnapshotFileIO.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "io/NativeByteStream.h"

namespace {

bool hasMagic(const std::vector<std::uint8_t>& bytes,
        const std::array<std::uint8_t, 4>& magic) {
    return bytes.size() >= magic.size()
            && std::equal(magic.begin(), magic.end(), bytes.begin());
}

bool isReleaseFile(const std::vector<std::uint8_t>& bytes) {
    return hasMagic(bytes, {'C', 'L', 'T', 'Y'});
}

bool isLegacyJavaFile(const std::vector<std::uint8_t>& bytes) {
    return hasMagic(bytes, {0xAC, 0xED, 0x00, 0x05});
}

bool isDiscardedPreReleaseFile(const std::vector<std::uint8_t>& bytes) {
    return hasMagic(bytes, {'C', 'L', 'S', 'B'})
            || hasMagic(bytes, {'C', 'L', 'G', 'N'})
            || hasMagic(bytes, {'C', 'L', 'E', 'N'})
            || hasMagic(bytes, {'C', 'G', 'D', 'W'});
}

constexpr std::uint64_t kReleaseAlignment = 8;
constexpr std::size_t kPreviewAttachCopyBufferBytes = 1024U * 1024U;

std::uint64_t alignReleaseOffset(std::uint64_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max()
                    - (kReleaseAlignment - 1U)) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                "Release substrate alignment overflowed");
    }
    return (value + kReleaseAlignment - 1U)
            & ~(kReleaseAlignment - 1U);
}

void requireWebpPayload(const std::vector<std::uint8_t>& bytes) {
    using namespace cellularity::save_format::release;
    const bool signatureMatches = bytes.size() >= 12
            && std::memcmp(bytes.data(), "RIFF", 4) == 0
            && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
    if (!signatureMatches || bytes.size() > kMaximumPreviewBytes) {
        cellularity::snapshot_codec::failDecode(
                signatureMatches ? DecodeErrorCode::LimitExceeded
                                 : DecodeErrorCode::InvalidValue,
                signatureMatches
                        ? "Substrate WebP preview exceeds its size limit"
                        : "Substrate preview is not an encoded WebP image");
    }
}

std::uint64_t streamSize(std::fstream& stream) {
    stream.clear();
    stream.seekg(0, std::ios::end);
    const std::streampos end = stream.tellg();
    if (end < 0) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::IoFailure,
                "Could not determine release substrate file size");
    }
    return static_cast<std::uint64_t>(end);
}

void readRangeInto(std::istream& stream,
        std::uint64_t offset,
        std::uint8_t* bytes,
        std::size_t count,
        const char* label) {
    if (offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())
            || count > static_cast<std::size_t>(
                       std::numeric_limits<std::streamsize>::max())) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                std::string("Release substrate ") + label
                        + " range is not addressable");
    }
    if (count == 0) {
        return;
    }
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    stream.read(
            reinterpret_cast<char*>(bytes),
            static_cast<std::streamsize>(count));
    if (!stream) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::IoFailure,
                std::string("Could not read release substrate ") + label);
    }
}

std::vector<std::uint8_t> readRange(std::istream& stream,
        std::uint64_t offset,
        std::uint64_t count,
        const char* label) {
    if (count > std::numeric_limits<std::size_t>::max()
            || offset > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamoff>::max())
            || count > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamsize>::max())) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                std::string("Release substrate ") + label
                        + " range is not addressable");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));
    if (bytes.empty()) {
        return bytes;
    }
    readRangeInto(stream, offset, bytes.data(), bytes.size(), label);
    return bytes;
}

void writeRange(std::fstream& stream,
        std::uint64_t offset,
        const std::uint8_t* bytes,
        std::size_t count,
        const char* label) {
    if (offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())
            || count > static_cast<std::size_t>(
                       std::numeric_limits<std::streamsize>::max())) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                std::string("Release substrate ") + label
                        + " range is not addressable");
    }
    if (count == 0) {
        return;
    }
    stream.clear();
    stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    stream.write(
            reinterpret_cast<const char*>(bytes),
            static_cast<std::streamsize>(count));
    if (!stream) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::IoFailure,
                std::string("Could not write release substrate ") + label);
    }
}

void writeReleaseSubstrateArtifact(const std::string& path,
        const SimulationWorld& world) {
    if (path.empty()) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidValue,
                "Substrate output path is empty");
    }
    const auto artifact =
            cellularity::save_format::release::writeReleaseSubstratePath(
                    path, world);
    cellularity::memory::validateSubstrateFileSize(
            artifact.stats.fileBytes,
            cellularity::memory::currentRuntimeLimits());
}

NativeSubstrateInspection releaseInspectionToNative(
        const cellularity::save_format::release::ReleaseSubstrateInspection& source,
        bool previewMode) {
    using namespace cellularity::save_format::release;
    if (source.maximumCellCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || source.maximumFoodCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || source.cellCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || source.foodCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || source.knownOrganismCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || source.contentSchemaMajor
                    > static_cast<std::uint32_t>(
                            std::numeric_limits<int>::max())
            || source.contentSchemaMinor
                    > static_cast<std::uint32_t>(
                            std::numeric_limits<int>::max())) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                "Release substrate metadata exceeds runtime integers");
    }
    const double timeSeconds = source.simulationTimeHours * 3600.0;
    cellularity::snapshot_codec::requireFinite(
            timeSeconds, "Release substrate simulation time");
    Environment environment = Environment::constructDefaultEnvironment(
            static_cast<int>(source.maximumCellCount),
            static_cast<int>(source.maximumFoodCount),
            source.dishRadius);
    environment.mInnerRadius = source.dishInnerRadius;
    environment.mIsPreviewMode = previewMode;
    NativeSubstrateInspection result;
    result.familyLocalVersion = static_cast<int>(source.contentSchemaMajor);
    result.time = timeSeconds;
    result.cellCount = static_cast<int>(source.cellCount);
    result.foodCount = static_cast<int>(source.foodCount);
    result.foodMass = source.foodMass;
    result.knownOrganismCount =
            static_cast<int>(source.knownOrganismCount);
    result.organismIdentityComplete = source.organismIdentityComplete;
    result.createdAtUnixMillis = source.createdAtUnixMillis;
    result.environment = std::move(environment);
    result.previewCodec = source.header.previewCodec;
    result.previewPixelWidth = source.header.previewPixelWidth;
    result.previewPixelHeight = source.header.previewPixelHeight;
    result.previewBytes = source.header.previewBytes;
    result.previewCrc32 = source.header.previewCrc32;
    const bool supportedCodec =
            source.header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::Png)
            || source.header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::Jpeg)
            || source.header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::WebpLossy)
            || source.header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::WebpLossless);
    result.previewPotentiallyUsable = supportedCodec
            && source.header.previewFlags == 0
            && source.header.previewBytes != 0;
    result.formatFamily = NativeSubstrateFormatFamily::ReleaseClty;
    result.contentSchemaMajor = source.contentSchemaMajor;
    result.contentSchemaMinor = source.contentSchemaMinor;
    result.loadSupported =
            source.contentSchemaMajor == kSubstrateContentSchemaMajor
            && source.contentSchemaMinor == kSubstrateContentSchemaMinor;
    return result;
}

} // namespace

NativeSubstrateInspection inspectSubstrateHeader(
        std::vector<std::uint8_t> prefixBytes,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    if (prefixBytes.size()
            > cellularity::format_limits::
                    kMaximumSubstrateInspectionPrefixBytes) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                "Substrate inspection prefix exceeds the safety limit");
    }
    if (isReleaseFile(prefixBytes)) {
        return releaseInspectionToNative(
                cellularity::save_format::release::inspectReleaseSubstrate(
                        prefixBytes),
                previewMode);
    }
    if (isLegacyJavaFile(prefixBytes)) {
        return cellularity::snapshot_codec::inspectLegacySubstrateHeader(
                std::move(prefixBytes), previewMode, fallbackEnvironment);
    }
    if (isDiscardedPreReleaseFile(prefixBytes)) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::UnsupportedFeature,
                "Discarded pre-release Cellularity format is not supported");
    }
    cellularity::snapshot_codec::failDecode(
            DecodeErrorCode::InvalidHeader,
            "Unsupported substrate file header");
}

NativeSubstrateInspection inspectSubstratePath(
        const std::string& path,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    cellularity::snapshot_codec::SnapshotInputFile source =
            cellularity::snapshot_codec::openSnapshotFile(path);
    const std::size_t initialSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                    source.sizeBytes,
                    cellularity::save_format::release::kBootstrapBytes));
    const std::vector<std::uint8_t> initial = readRange(
            source.stream, 0, initialSize, "inspection bootstrap");
    if (isReleaseFile(initial)) {
        using namespace cellularity::save_format::release;
        const BootstrapHeader header = decodeBootstrap(initial, source.sizeBytes);
        const std::uint64_t metadataEnd = header.metadataOffset
                + header.metadataBytes;
        if (metadataEnd > std::numeric_limits<std::size_t>::max()) {
            cellularity::snapshot_codec::failDecode(
                    DecodeErrorCode::LimitExceeded,
                    "Release substrate metadata is not addressable");
        }
        std::vector<std::uint8_t> prefix(
                static_cast<std::size_t>(metadataEnd), 0);
        std::copy(initial.begin(), initial.end(), prefix.begin());
        const std::vector<std::uint8_t> metadata = readRange(
                source.stream,
                header.metadataOffset,
                header.metadataBytes,
                "inspection metadata");
        std::copy(
                metadata.begin(),
                metadata.end(),
                prefix.begin() + static_cast<std::ptrdiff_t>(header.metadataOffset));
        return releaseInspectionToNative(
                inspectReleaseSubstratePrefix(prefix, source.sizeBytes),
                previewMode);
    }

    const std::size_t prefixSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                    source.sizeBytes,
                    cellularity::format_limits::
                            kMaximumSubstrateInspectionPrefixBytes));
    std::vector<std::uint8_t> prefix = readRange(
            source.stream, 0, prefixSize, "inspection prefix");
    return inspectSubstrateHeader(
            std::move(prefix), previewMode, fallbackEnvironment);
}

void writeSubstratePath(const std::string& path, const SimulationWorld& world) {
    writeReleaseSubstrateArtifact(path, world);
}

void attachSubstratePreviewPath(
        const std::string& path,
        const std::vector<std::uint8_t>& webpBytes) {
    using namespace cellularity::save_format::release;
    if (path.empty()) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidValue,
                "Substrate preview path is empty");
    }
    requireWebpPayload(webpBytes);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::IoFailure,
                "Could not open substrate file for preview attachment");
    }
    const std::uint64_t oldFileBytes = streamSize(file);
    cellularity::memory::validateSubstrateFileSize(
            oldFileBytes, cellularity::memory::currentRuntimeLimits());
    const BootstrapHeader originalHeader = decodeBootstrap(
            readRange(file, 0, kBootstrapBytes, "bootstrap"),
            oldFileBytes);
    if (originalHeader.documentKind != DocumentKind::Substrate) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidHeader,
                "Preview target is not a release substrate");
    }
    if (originalHeader.previewCodec
            != static_cast<std::uint16_t>(PreviewCodec::None)) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidValue,
                "Release substrate already contains a preview");
    }
    const std::vector<std::uint8_t> metadata = readRange(
            file,
            originalHeader.metadataOffset,
            originalHeader.metadataBytes,
            "metadata");
    (void)decodeMetadata(
            DocumentKind::Substrate,
            metadata,
            originalHeader.metadataCrc32);
    const std::vector<std::uint8_t> oldDirectory = readRange(
            file,
            originalHeader.directoryOffset,
            originalHeader.directoryBytes,
            "directory");
    std::vector<SectionDescriptor> descriptors = decodeDirectory(
            oldDirectory,
            originalHeader.directoryCrc32,
            originalHeader);
    if (originalHeader.directoryOffset + originalHeader.directoryBytes
            != oldFileBytes) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidLength,
                "Release substrate directory is not the final file region");
    }

    const std::uint64_t metadataEnd = originalHeader.metadataOffset
            + originalHeader.metadataBytes;
    const std::uint64_t oldContentStart = alignReleaseOffset(metadataEnd);
    std::uint64_t firstLiveRegion = originalHeader.directoryOffset;
    for (const SectionDescriptor& section : descriptors) {
        for (const ColumnDescriptor& column : section.columns) {
            for (const ChunkDescriptor& chunk : column.chunks) {
                firstLiveRegion = std::min(firstLiveRegion, chunk.fileOffset);
            }
        }
    }
    if (firstLiveRegion != oldContentStart) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidLength,
                "Release substrate does not use the canonical preview insertion point");
    }
    const std::vector<std::uint8_t> alignmentPadding = readRange(
            file,
            metadataEnd,
            oldContentStart - metadataEnd,
            "metadata alignment padding");
    if (!std::all_of(
                alignmentPadding.begin(),
                alignmentPadding.end(),
                [](std::uint8_t byte) {
                    return byte == 0;
                })) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidValue,
                "Release substrate metadata padding is nonzero");
    }

    const std::uint64_t newContentStart = alignReleaseOffset(
            oldContentStart + webpBytes.size());
    const std::uint64_t shift = newContentStart - oldContentStart;
    if (shift > kMaximumFileBytes - oldFileBytes) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                "Release substrate with preview exceeds the 2 GiB limit");
    }
    const std::uint64_t newFileBytes = oldFileBytes + shift;
    cellularity::memory::validateSubstrateFileSize(
            newFileBytes, cellularity::memory::currentRuntimeLimits());
    if (newFileBytes == 0
            || newFileBytes - 1U > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamoff>::max())) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::LimitExceeded,
                "Release substrate output is not seekable");
    }

    const std::uint8_t zero = 0;
    writeRange(file, newFileBytes - 1U, &zero, 1, "extension");
    std::vector<std::uint8_t> copyBuffer(kPreviewAttachCopyBufferBytes);
    std::uint64_t remaining = oldFileBytes - oldContentStart;
    while (remaining != 0) {
        const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, copyBuffer.size()));
        const std::uint64_t sourceOffset =
                oldContentStart + remaining - count;
        readRangeInto(
                file,
                sourceOffset,
                copyBuffer.data(),
                count,
                "body shift source");
        writeRange(
                file,
                sourceOffset + shift,
                copyBuffer.data(),
                count,
                "body shift destination");
        remaining -= count;
    }
    writeRange(
            file,
            oldContentStart,
            webpBytes.data(),
            webpBytes.size(),
            "preview");
    const std::vector<std::uint8_t> previewPadding(
            static_cast<std::size_t>(
                    newContentStart - oldContentStart - webpBytes.size()),
            0);
    writeRange(
            file,
            oldContentStart + webpBytes.size(),
            previewPadding.data(),
            previewPadding.size(),
            "preview alignment padding");

    for (SectionDescriptor& section : descriptors) {
        for (ColumnDescriptor& column : section.columns) {
            for (ChunkDescriptor& chunk : column.chunks) {
                chunk.fileOffset += shift;
            }
        }
    }
    const std::vector<std::uint8_t> newDirectory = encodeDirectory(descriptors);
    if (newDirectory.size() != originalHeader.directoryBytes) {
        throw std::logic_error(
                "Release directory size changed while attaching a preview");
    }

    BootstrapHeader newHeader = originalHeader;
    newHeader.declaredFileBytes = newFileBytes;
    newHeader.previewCodec = static_cast<std::uint16_t>(PreviewCodec::WebpLossy);
    newHeader.previewPixelWidth = kSelectedSubstratePreviewPixels;
    newHeader.previewPixelHeight = kSelectedSubstratePreviewPixels;
    newHeader.previewOffset = oldContentStart;
    newHeader.previewBytes = webpBytes.size();
    newHeader.previewCrc32 = crc32(webpBytes);
    newHeader.directoryOffset += shift;
    newHeader.directoryCrc32 = crc32(newDirectory);
    (void)decodeDirectory(
            newDirectory,
            newHeader.directoryCrc32,
            newHeader);
    const std::vector<std::uint8_t> bootstrap = encodeBootstrap(newHeader);
    writeRange(
            file,
            newHeader.directoryOffset,
            newDirectory.data(),
            newDirectory.size(),
            "updated directory");
    writeRange(
            file, 0, bootstrap.data(), bootstrap.size(), "updated bootstrap");
    file.flush();
    if (!file) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::IoFailure,
                "Could not flush substrate preview attachment");
    }
    file.close();
    if (!file) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::IoFailure,
                "Could not close substrate preview attachment");
    }
}

std::vector<std::uint8_t> readSubstratePreviewPath(
        const std::string& path) {
    using namespace cellularity::save_format::release;
    cellularity::snapshot_codec::SnapshotInputFile source =
            cellularity::snapshot_codec::openSnapshotFile(path);
    const std::size_t bootstrapSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(source.sizeBytes, kBootstrapBytes));
    const std::vector<std::uint8_t> bootstrap = readRange(
            source.stream, 0, bootstrapSize, "preview bootstrap");
    if (!isReleaseFile(bootstrap)) {
        return {};
    }
    const BootstrapHeader header = decodeBootstrap(bootstrap, source.sizeBytes);
    if (header.documentKind != DocumentKind::Substrate
            || header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::None)
            || header.previewFlags != 0) {
        return {};
    }
    const std::vector<std::uint8_t> preview = readRange(
            source.stream,
            header.previewOffset,
            header.previewBytes,
            "preview payload");
    return classifyPreview(header, preview) == PreviewDisposition::Available
            ? preview
            : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> writeSubstrateInspectionSnapshot(
        const NativeSubstrateInspection& inspection) {
    ByteWriter writer;
    writer.writeInt(cellularity::internal_wire::kSubstrateInspectionMagic);
    writer.writeInt(cellularity::internal_wire::kSubstrateInspectionVersion);
    writer.writeInt(inspection.familyLocalVersion);
    writer.writeDouble(inspection.time);
    writer.writeInt(inspection.cellCount);
    writer.writeInt(inspection.environment.maxCellCount);
    writer.writeInt(inspection.environment.maxFoodCount);
    writer.writeDouble(inspection.environment.mRadius);
    writer.writeInt(static_cast<std::int32_t>(inspection.previewCodec));
    writer.writeInt(static_cast<std::int32_t>(inspection.previewPixelWidth));
    writer.writeInt(static_cast<std::int32_t>(inspection.previewPixelHeight));
    writer.writeLong(static_cast<std::int64_t>(inspection.previewBytes));
    writer.writeInt(static_cast<std::int32_t>(inspection.previewCrc32));
    writer.writeInt(inspection.previewPotentiallyUsable ? 1 : 0);
    writer.writeInt(static_cast<std::int32_t>(inspection.formatFamily));
    writer.writeInt(static_cast<std::int32_t>(inspection.contentSchemaMajor));
    writer.writeInt(static_cast<std::int32_t>(inspection.contentSchemaMinor));
    writer.writeInt(inspection.loadSupported ? 1 : 0);
    writer.writeInt(inspection.foodCount);
    writer.writeDouble(inspection.foodMass);
    writer.writeLong(inspection.createdAtUnixMillis);
    writer.writeInt(inspection.knownOrganismCount);
    writer.writeInt(inspection.organismIdentityComplete ? 1 : 0);
    writer.writeDouble(inspection.environment.mInnerRadius);
    return writer.takeBytes();
}
