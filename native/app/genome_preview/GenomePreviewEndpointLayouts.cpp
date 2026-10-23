#include "app/genome_preview/GenomePreviewEndpointLayouts.h"

#include <cstring>

namespace cellularity::app::genome_preview {
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

void writeLittleEndianFloat(std::uint8_t* destination, float value) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    writeLittleEndian32(destination, bits);
}

} // namespace

std::array<std::uint8_t, kGenomePreviewPublishResultSize> encodeGenomePreviewPublishResult(GenomePreviewStatePublishResult result) noexcept {
    std::array<std::uint8_t, kGenomePreviewPublishResultSize> bytes{};
    writeLittleEndian32(bytes.data(), kGenomePreviewPublishResultMagic);
    writeLittleEndian16(bytes.data() + 4, kGenomePreviewPublishResultVersion);
    writeLittleEndian16(bytes.data() + 6, kGenomePreviewPublishResultSize);
    writeLittleEndian32(bytes.data() + 8, static_cast<std::uint32_t>(result.status));
    writeLittleEndian64(bytes.data() + 16, result.revision);
    return bytes;
}

std::array<std::uint8_t, kGenomePreviewAcknowledgmentSize> encodeGenomePreviewAcknowledgment(const GenomePreviewAcknowledgment& acknowledgment) noexcept {
    std::array<std::uint8_t, kGenomePreviewAcknowledgmentSize> bytes{};
    writeLittleEndian32(bytes.data(), kGenomePreviewAcknowledgmentMagic);
    writeLittleEndian16(bytes.data() + 4, kGenomePreviewAcknowledgmentVersion);
    writeLittleEndian16(bytes.data() + 6, kGenomePreviewAcknowledgmentSize);
    writeLittleEndian64(bytes.data() + 8, acknowledgment.endpointGeneration);
    writeLittleEndian64(bytes.data() + 16, acknowledgment.attachmentGeneration);
    writeLittleEndian64(bytes.data() + 24, acknowledgment.appliedContentRevision);
    writeLittleEndian64(bytes.data() + 32, acknowledgment.appliedViewRevision);
    writeLittleEndian64(bytes.data() + 40, acknowledgment.presentedContentRevision);
    writeLittleEndian64(bytes.data() + 48, acknowledgment.presentedViewRevision);
    writeLittleEndian64(bytes.data() + 56, acknowledgment.presentedInteractionToken);
    writeLittleEndian64(bytes.data() + 64, acknowledgment.cellCountContentRevision);
    writeLittleEndian64(bytes.data() + 72, acknowledgment.viewportRevision);
    writeLittleEndian64(bytes.data() + 80, acknowledgment.selectionToken);
    writeLittleEndian32(bytes.data() + 88, static_cast<std::uint32_t>(acknowledgment.previewCellCount));
    writeLittleEndian32(bytes.data() + 92, static_cast<std::uint32_t>(acknowledgment.selectedGeneIndex));
    writeLittleEndianFloat(bytes.data() + 96, acknowledgment.viewport[0]);
    writeLittleEndianFloat(bytes.data() + 100, acknowledgment.viewport[1]);
    writeLittleEndianFloat(bytes.data() + 104, acknowledgment.viewport[2]);
    return bytes;
}

} // namespace cellularity::app::genome_preview
