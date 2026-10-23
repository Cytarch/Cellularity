#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace cellularity {

inline constexpr std::size_t MAX_SUPPORTED_GENES_PER_GENOME = 256;

class GeneIndex final {
public:
    using Value = std::uint16_t;

    constexpr GeneIndex() noexcept
            : value_(std::numeric_limits<Value>::max() - 1U) {
    }

    explicit constexpr GeneIndex(Value value) noexcept : value_(value) {
    }

    [[nodiscard]] constexpr Value value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(GeneIndex left, GeneIndex right) noexcept {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(GeneIndex left, GeneIndex right) noexcept {
        return !(left == right);
    }

private:
    Value value_;
};

class GeneId final {
public:
    using Value = std::uint32_t;

    constexpr GeneId() noexcept : value_(std::numeric_limits<Value>::max()) {
    }

    explicit constexpr GeneId(Value value) noexcept : value_(value) {
    }

    [[nodiscard]] constexpr Value value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(GeneId left, GeneId right) noexcept {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(GeneId left, GeneId right) noexcept {
        return !(left == right);
    }

private:
    Value value_;
};

class GenomeId final {
public:
    using Value = std::uint32_t;

    constexpr GenomeId() noexcept : value_(std::numeric_limits<Value>::max()) {
    }

    explicit constexpr GenomeId(Value value) noexcept : value_(value) {
    }

    [[nodiscard]] constexpr Value value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(GenomeId left, GenomeId right) noexcept {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(GenomeId left, GenomeId right) noexcept {
        return !(left == right);
    }

private:
    Value value_;
};

inline constexpr GeneIndex SELF_GENE_INDEX{
        std::numeric_limits<GeneIndex::Value>::max()
};
inline constexpr GeneIndex INVALID_GENE_INDEX{
        std::numeric_limits<GeneIndex::Value>::max() - 1U
};
inline constexpr GeneId DEFAULT_GENE_ID{0};
inline constexpr GeneId INVALID_GENE_ID{};
inline constexpr GenomeId INVALID_GENOME_ID{};

static_assert(sizeof(GeneIndex) == sizeof(GeneIndex::Value));
static_assert(
        MAX_SUPPORTED_GENES_PER_GENOME
                <= static_cast<std::size_t>(INVALID_GENE_INDEX.value()),
        "Every supported local gene index must remain distinct from sentinels");
static_assert(
        static_cast<std::size_t>(INVALID_GENE_INDEX.value()) + 1U
                == static_cast<std::size_t>(SELF_GENE_INDEX.value()),
        "The 16-bit gene reference sentinels must remain reserved");

[[nodiscard]] constexpr bool isSupportedGeneCount(std::size_t geneCount) noexcept {
    return geneCount >= 1 && geneCount <= MAX_SUPPORTED_GENES_PER_GENOME;
}

[[nodiscard]] constexpr bool isExplicitGeneIndex(GeneIndex index) noexcept {
    return index.value() < MAX_SUPPORTED_GENES_PER_GENOME;
}

[[nodiscard]] constexpr bool isGeneIndexInBounds(
        GeneIndex index,
        std::size_t geneCount) noexcept {
    return isSupportedGeneCount(geneCount)
            && isExplicitGeneIndex(index)
            && index.value() < geneCount;
}

[[nodiscard]] inline std::optional<GeneIndex> geneIndexFromBoundary(
        std::int64_t value,
        std::size_t geneCount) noexcept {
    if (!isSupportedGeneCount(geneCount)
            || value < 0
            || static_cast<std::uint64_t>(value) >= geneCount) {
        return std::nullopt;
    }
    return GeneIndex(static_cast<GeneIndex::Value>(value));
}

[[nodiscard]] inline std::optional<GeneIndex> geneReferenceFromBoundary(
        std::int64_t value,
        std::size_t geneCount) noexcept {
    if (value == SELF_GENE_INDEX.value()) {
        return isSupportedGeneCount(geneCount)
                ? std::optional<GeneIndex>(SELF_GENE_INDEX)
                : std::nullopt;
    }
    return geneIndexFromBoundary(value, geneCount);
}

[[nodiscard]] inline std::optional<std::uint16_t> geneIndexToBoundary(
        GeneIndex index,
        std::size_t geneCount) noexcept {
    if (!isGeneIndexInBounds(index, geneCount)) {
        return std::nullopt;
    }
    return index.value();
}

[[nodiscard]] inline std::optional<std::uint16_t> geneReferenceToBoundary(
        GeneIndex reference,
        std::size_t geneCount) noexcept {
    if (reference == SELF_GENE_INDEX && isSupportedGeneCount(geneCount)) {
        return SELF_GENE_INDEX.value();
    }
    return geneIndexToBoundary(reference, geneCount);
}

[[nodiscard]] constexpr GeneIndex resolveGeneReference(
        GeneIndex reference,
        GeneIndex owner,
        std::size_t geneCount) noexcept {
    if (!isGeneIndexInBounds(owner, geneCount)) {
        return INVALID_GENE_INDEX;
    }
    if (reference == SELF_GENE_INDEX) {
        return owner;
    }
    return isGeneIndexInBounds(reference, geneCount)
            ? reference
            : INVALID_GENE_INDEX;
}

struct GeneIndexRemapView final {
    const GeneIndex* oldToNew = nullptr;
    std::size_t oldGeneCount = 0;
    std::size_t newGeneCount = 0;
};

[[nodiscard]] constexpr bool isValidGeneIndexRemap(
        const GeneIndexRemapView& remap) noexcept {
    if (!isSupportedGeneCount(remap.oldGeneCount)
            || !isSupportedGeneCount(remap.newGeneCount)
            || remap.oldToNew == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < remap.oldGeneCount; index++) {
        const GeneIndex mapped = remap.oldToNew[index];
        if (mapped != INVALID_GENE_INDEX
                && !isGeneIndexInBounds(mapped, remap.newGeneCount)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr GeneIndex remapGeneIndex(
        GeneIndex index,
        const GeneIndexRemapView& remap) noexcept {
    if (!isValidGeneIndexRemap(remap)
            || !isGeneIndexInBounds(index, remap.oldGeneCount)) {
        return INVALID_GENE_INDEX;
    }
    return remap.oldToNew[index.value()];
}

[[nodiscard]] constexpr GeneIndex remapGeneReference(
        GeneIndex reference,
        const GeneIndexRemapView& remap) noexcept {
    if (reference == SELF_GENE_INDEX) {
        return isValidGeneIndexRemap(remap)
                ? SELF_GENE_INDEX
                : INVALID_GENE_INDEX;
    }
    return remapGeneIndex(reference, remap);
}

} // namespace cellularity
