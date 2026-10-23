#pragma once

#include <cstdint>

enum class CellLinkCreationReason : std::uint8_t {
    UNSPECIFIED = 0,
    // The direct link created between two daughters by one division.
    SPLIT_SIBLING = 1,
    // A division-preserved link whose source was organism-forming.
    SPLIT_INHERITANCE = 2,
    GLUEOCYTE_COLLISION = 3,
    // A division-preserved Glueocyte link; it never merges organism IDs.
    GLUEOCYTE_INHERITANCE = 4
};

[[nodiscard]] constexpr bool isOrganismFormingCellLink(
        CellLinkCreationReason reason) noexcept {
    return reason == CellLinkCreationReason::SPLIT_SIBLING
            || reason == CellLinkCreationReason::SPLIT_INHERITANCE;
}

[[nodiscard]] constexpr bool isGlueocyteCellLink(
        CellLinkCreationReason reason) noexcept {
    return reason == CellLinkCreationReason::GLUEOCYTE_COLLISION
            || reason == CellLinkCreationReason::GLUEOCYTE_INHERITANCE;
}

[[nodiscard]] constexpr CellLinkCreationReason inheritedCellLinkReason(
        CellLinkCreationReason reason) noexcept {
    if (isOrganismFormingCellLink(reason)) {
        return CellLinkCreationReason::SPLIT_INHERITANCE;
    }
    if (isGlueocyteCellLink(reason)) {
        return CellLinkCreationReason::GLUEOCYTE_INHERITANCE;
    }
    // In particular, do not upgrade an unclassified legacy link into evidence
    // of shared organism identity.
    return CellLinkCreationReason::UNSPECIFIED;
}

[[nodiscard]] constexpr bool isValidCellLinkCreationReason(
        CellLinkCreationReason reason) noexcept {
    switch (reason) {
        case CellLinkCreationReason::UNSPECIFIED:
        case CellLinkCreationReason::SPLIT_SIBLING:
        case CellLinkCreationReason::SPLIT_INHERITANCE:
        case CellLinkCreationReason::GLUEOCYTE_COLLISION:
        case CellLinkCreationReason::GLUEOCYTE_INHERITANCE:
            return true;
    }
    return false;
}

static_assert(sizeof(CellLinkCreationReason) == sizeof(std::uint8_t));
