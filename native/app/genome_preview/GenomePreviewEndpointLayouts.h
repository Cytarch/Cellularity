#pragma once

#include <array>
#include <cstdint>

#include "app/genome_preview/GenomePreviewEndpoint.h"

namespace cellularity::app::genome_preview {

inline constexpr std::uint32_t kGenomePreviewPublishResultMagic = 0x31535047U;
inline constexpr std::uint16_t kGenomePreviewPublishResultVersion = 1;
inline constexpr std::uint16_t kGenomePreviewPublishResultSize = 24;

inline constexpr std::uint32_t kGenomePreviewAcknowledgmentMagic = 0x32414647U;
inline constexpr std::uint16_t kGenomePreviewAcknowledgmentVersion = 2;
inline constexpr std::uint16_t kGenomePreviewAcknowledgmentSize = 112;

[[nodiscard]] std::array<std::uint8_t, kGenomePreviewPublishResultSize> encodeGenomePreviewPublishResult(GenomePreviewStatePublishResult result) noexcept;

[[nodiscard]] std::array<std::uint8_t, kGenomePreviewAcknowledgmentSize> encodeGenomePreviewAcknowledgment(const GenomePreviewAcknowledgment& acknowledgment) noexcept;

} // namespace cellularity::app::genome_preview
