#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/GenomePool.h"

class ByteReader;

namespace cellularity {

inline constexpr std::size_t kGenomeDraftWireHeaderBytes =
        6U * sizeof(std::int32_t);
inline constexpr std::size_t kGenomeDraftWireBytesPerGene =
        10U * sizeof(float)
        + 2U * sizeof(std::int32_t)
        + 7U * sizeof(std::uint8_t)
        + sizeof(std::int32_t)
        + Gene::MAX_PARAMETRIC_VALUES_COUNT
                * (3U * sizeof(float) + 2U * sizeof(std::int16_t))
        + Gene::MAX_INTEGERS_COUNT * sizeof(std::int32_t)
        + Gene::MAX_FLOATS_COUNT * sizeof(float);
inline constexpr std::size_t kGenomeDraftMaximumWireBytes =
        kGenomeDraftWireHeaderBytes
        + MAX_SUPPORTED_GENES_PER_GENOME * kGenomeDraftWireBytesPerGene;

static_assert(kGenomeDraftMaximumWireBytes == 78'616U,
        "Native and Kotlin GenomeDraft wire limits must remain identical");

[[nodiscard]] bool hasGenomeDraftWireHeader(
        const std::uint8_t* bytes, std::size_t byteCount) noexcept;

} // namespace cellularity

/** Decodes and validates the bounded dynamic Kotlin GenomeDraft payload. */
cellularity::GenomeData readGenomeDraft(ByteReader& reader);

/** Encodes an immutable genome copy for Kotlin inspection/editor consumers. */
std::vector<uint8_t> writeGenomeDraft(
        const cellularity::GenomeData& genome);

/** Applies the same semantic validation to an already decoded legacy genome. */
void validateGenomeDraft(
        const cellularity::GenomeData& genome);
