#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/CellData.h"
#include "core/Gene.h"

class JavaObjectStreamReader;

namespace cellularity::snapshot_codec {

GeneData readLegacyGene(JavaObjectStreamReader& input, int cellVersion);
std::array<GeneData, CellData::MAX_GENES_COUNT> readLegacyGenome(
        JavaObjectStreamReader& input,
        int cellVersion);
std::array<GeneData, CellData::MAX_GENES_COUNT> decodeLegacyGenomeBytes(
        std::vector<std::uint8_t> bytes);

} // namespace cellularity::snapshot_codec
