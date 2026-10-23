#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/GenomePool.h"
#include "formats/release/SaveContainerBody.h"

namespace cellularity::save_format::release {

constexpr std::uint32_t kGenomeContentSchemaMajor = 1;
constexpr std::uint32_t kGenomeContentSchemaMinor = 0;

struct ReleaseGenomeInspection final {
    BootstrapHeader header;
    PreviewDisposition previewDisposition = PreviewDisposition::None;
    std::uint32_t geneCount = 0;
    std::uint32_t initialGeneIndex = 0;
    std::uint32_t uniqueGeneCount = 0;
    std::uint32_t cellTypeMask = 0;
};

/** Validates the current 87-column schema and reconstructs canonical row data. */
[[nodiscard]] std::vector<GeneData> decodeReleaseGeneTable(
        const LogicalSection& genes);

using ReleaseColumnLoader = std::function<void(
        std::uint32_t fieldId,
        PrimitiveType primitiveType,
        const std::string& canonicalName,
        std::optional<std::uint64_t> exactValueCount,
        const DecodedChunkConsumer& consumer)>;

/** Reconstructs rows while retaining at most one decoded column chunk. */
[[nodiscard]] std::vector<GeneData> decodeReleaseGeneTableFromColumns(
        std::uint64_t rowCount,
        const ReleaseColumnLoader& loadColumn);

/** Encodes one validated logical genome as a deterministic CLTY genome file. */
[[nodiscard]] std::vector<std::uint8_t> encodeReleaseGenome(
        const GenomeData& genome,
        const EncodedPreview& preview = {},
        const BodyWriteOptions& options = {});

/** Fully validates and decodes one CLTY standalone genome document. */
[[nodiscard]] GenomeData decodeReleaseGenome(
        const std::vector<std::uint8_t>& bytes);

/** Reads bounded card metadata and preview status without decoding body chunks. */
[[nodiscard]] ReleaseGenomeInspection inspectReleaseGenome(
        const std::vector<std::uint8_t>& bytes);

} // namespace cellularity::save_format::release
