#pragma once

namespace cellularity::legacy_format {

// These constants describe the original Java object-stream formats. They are
// permanently frozen and must not follow the version of any Cellularity-native
// runtime or file format.
inline constexpr int kMinimumProductVersion = 1;
inline constexpr int kMaximumProductVersion = 103;
inline constexpr int kMinimumCellProductVersion = 2;
inline constexpr int kObsoleteExtensionProductVersion = 103;
inline constexpr int kObsoleteGeneExtensionFieldCount = 8;

inline constexpr int kStandaloneGenome15LastProductVersion = 3;
inline constexpr int kStandaloneGenome20LastProductVersion = 17;
inline constexpr int kStandaloneGenome15GeneCount = 15;
inline constexpr int kStandaloneGenome20GeneCount = 20;

// Both count-prefixed Cell genomes and standalone 15/20-gene genomes are
// expanded to this effective size by repeating gene zero. This is legacy
// import behavior, not the future runtime genome limit.
inline constexpr int kEffectiveImportedGenomeGeneCount = 40;

[[nodiscard]] constexpr int standaloneGenomeSerializedGeneCount(
        int productVersion) noexcept {
    return productVersion <= kStandaloneGenome15LastProductVersion
            ? kStandaloneGenome15GeneCount
            : productVersion <= kStandaloneGenome20LastProductVersion
            ? kStandaloneGenome20GeneCount
            : kEffectiveImportedGenomeGeneCount;
}

} // namespace cellularity::legacy_format
