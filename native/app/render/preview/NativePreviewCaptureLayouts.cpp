#include "app/render/preview/NativePreviewCaptureLayouts.h"

#include <limits>

namespace cellularity::app::preview {
namespace {

void writeLittleEndian16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void writeLittleEndian32(std::uint8_t* destination, std::uint32_t value) noexcept {
    for (unsigned index = 0; index < 4; ++index) {
        destination[index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void writeLittleEndian64(std::uint8_t* destination, std::uint64_t value) noexcept {
    for (unsigned index = 0; index < 8; ++index) {
        destination[index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

} // namespace

std::array<std::uint8_t, kPreviewCaptureAdmissionSize> encodePreviewCaptureAdmission(PreviewCaptureAdmission value) noexcept {
    std::array<std::uint8_t, kPreviewCaptureAdmissionSize> bytes{};
    writeLittleEndian32(bytes.data(), kPreviewCaptureAdmissionMagic);
    writeLittleEndian16(bytes.data() + 4, kPreviewCaptureAdmissionVersion);
    writeLittleEndian16(bytes.data() + 6, kPreviewCaptureAdmissionSize);
    writeLittleEndian32(bytes.data() + 8, static_cast<std::uint32_t>(value.status));
    writeLittleEndian64(bytes.data() + 16, value.requestHandle);
    return bytes;
}

std::array<std::uint8_t, kPreviewCaptureResultSize> encodePreviewCaptureResultHeader(const PreviewCaptureResultHeader& value) noexcept {
    std::array<std::uint8_t, kPreviewCaptureResultSize> bytes{};
    writeLittleEndian32(bytes.data(), kPreviewCaptureResultMagic);
    writeLittleEndian16(bytes.data() + 4, kPreviewCaptureResultVersion);
    writeLittleEndian16(bytes.data() + 6, kPreviewCaptureResultSize);
    writeLittleEndian64(bytes.data() + 8, value.requestId);
    writeLittleEndian32(bytes.data() + 16, static_cast<std::uint32_t>(value.status));
    writeLittleEndian32(bytes.data() + 20, value.width);
    writeLittleEndian32(bytes.data() + 24, value.height);
    writeLittleEndian32(bytes.data() + 28, value.rowStrideBytes);
    writeLittleEndian32(bytes.data() + 32, static_cast<std::uint32_t>(value.pixelFormat));
    writeLittleEndian32(bytes.data() + 36, static_cast<std::uint32_t>(value.origin));
    writeLittleEndian32(bytes.data() + 40, static_cast<std::uint32_t>(value.alphaMode));
    writeLittleEndian64(bytes.data() + 48, value.payloadBytes);
    return bytes;
}

bool validateCompletedPreviewCaptureResult(const PreviewCaptureResultHeader& value, std::size_t payloadSize) noexcept {
    if (value.status != PreviewCaptureStatus::Completed || value.requestId == 0 || value.width == 0 || value.height == 0 || value.pixelFormat != PreviewCapturePixelFormat::Rgba8 || value.origin != PreviewCaptureOrigin::TopLeft || (value.alphaMode != PreviewCaptureAlphaMode::Straight && value.alphaMode != PreviewCaptureAlphaMode::Opaque)) {
        return false;
    }
    if (value.width > std::numeric_limits<std::uint32_t>::max() / 4U || value.rowStrideBytes != value.width * 4U) {
        return false;
    }
    const std::uint64_t expected = static_cast<std::uint64_t>(value.rowStrideBytes) * static_cast<std::uint64_t>(value.height);
    return expected == value.payloadBytes && expected == payloadSize;
}

} // namespace cellularity::app::preview
