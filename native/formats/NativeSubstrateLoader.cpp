#include "formats/NativeSubstrateLoader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "formats/LegacySubstrateCodec.h"
#include "formats/SnapshotCodecSupport.h"
#include "formats/SnapshotFileIO.h"
#include "formats/release/ReleaseSubstrateCodec.h"

namespace {

bool hasMagic(const std::vector<std::uint8_t>& bytes,
        const std::array<std::uint8_t, 4>& magic) {
    return bytes.size() >= magic.size()
            && std::equal(magic.begin(), magic.end(), bytes.begin());
}

} // namespace

NativeSubstrateLoadResult loadNativeSubstrateFromPath(
        const std::string& path,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    cellularity::snapshot_codec::SnapshotInputFile source =
            cellularity::snapshot_codec::openSnapshotFile(path);
    constexpr std::size_t currentHeaderBytes = 4;
    std::vector<std::uint8_t> headerBytes(
            static_cast<std::size_t>(std::min<std::uint64_t>(
                    source.sizeBytes, currentHeaderBytes)));
    if (headerBytes.size() >= sizeof(std::int32_t)) {
        source.stream.read(reinterpret_cast<char*>(headerBytes.data()),
                static_cast<std::streamsize>(headerBytes.size()));
        if (!source.stream) {
            cellularity::snapshot_codec::failDecode(
                    DecodeErrorCode::IoFailure,
                    "Could not read substrate format header");
        }
    }
    if (hasMagic(headerBytes, {'C', 'L', 'T', 'Y'})) {
        auto world =
                cellularity::save_format::release::decodeReleaseSubstratePath(
                        path, previewMode);
        return {
                std::move(world),
                NativeSubstrateFormatFamily::ReleaseClty,
                static_cast<int>(
                        cellularity::save_format::release::
                                kSubstrateContentSchemaMajor)
        };
    }

    if (hasMagic(headerBytes, {'C', 'L', 'S', 'B'})
            || hasMagic(headerBytes, {'C', 'L', 'G', 'N'})
            || hasMagic(headerBytes, {'C', 'L', 'E', 'N'})
            || hasMagic(headerBytes, {'C', 'G', 'D', 'W'})) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::UnsupportedFeature,
                "Discarded pre-release Cellularity format is not supported");
    }

    if (!hasMagic(headerBytes, {0xAC, 0xED, 0x00, 0x05})) {
        cellularity::snapshot_codec::failDecode(
                DecodeErrorCode::InvalidHeader,
                "Unsupported substrate file header");
    }
    cellularity::snapshot_codec::LegacySubstrateLoadResult loaded =
            cellularity::snapshot_codec::loadLegacySubstratePathDirect(
                    path, previewMode, fallbackEnvironment);
    return {
            std::move(loaded.world),
            NativeSubstrateFormatFamily::OriginalJava,
            loaded.productVersion
    };
}

void validateNativeSubstratePath(
        const std::string& path,
        bool previewMode,
        const Environment& fallbackEnvironment) {
    cellularity::snapshot_codec::SnapshotInputFile source =
            cellularity::snapshot_codec::openSnapshotFile(path);
    std::vector<std::uint8_t> headerBytes(
            static_cast<std::size_t>(std::min<std::uint64_t>(
                    source.sizeBytes, 4U)));
    if (!headerBytes.empty()) {
        source.stream.read(
                reinterpret_cast<char*>(headerBytes.data()),
                static_cast<std::streamsize>(headerBytes.size()));
        if (!source.stream) {
            cellularity::snapshot_codec::failDecode(
                    DecodeErrorCode::IoFailure,
                    "Could not read substrate validation header");
        }
    }
    if (hasMagic(headerBytes, {'C', 'L', 'T', 'Y'})) {
        cellularity::save_format::release::validateReleaseSubstratePath(path);
        return;
    }
    (void)loadNativeSubstrateFromPath(
            path, previewMode, fallbackEnvironment);
}
