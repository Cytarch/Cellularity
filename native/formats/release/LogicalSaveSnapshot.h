#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/release/SaveContainerCodec.h"

namespace cellularity::save_format::release {

/** Canonical little-endian bytes for one named logical column. */
struct LogicalColumn final {
    std::uint32_t fieldId = 0;
    PrimitiveType primitiveType = PrimitiveType::U8;
    std::string canonicalName;
    std::uint64_t logicalValueCount = 0;
    std::vector<std::uint8_t> decodedBytes;
};

struct LogicalSection final {
    std::uint32_t sectionId = 0;
    std::uint16_t schemaMajor = 1;
    std::uint16_t schemaMinor = 0;
    std::string canonicalName;
    std::uint64_t logicalRowCount = 0;
    std::vector<LogicalColumn> columns;
};

struct LogicalSnapshotDiagnostics final {
    std::uint64_t cellCount = 0;
    std::uint64_t uniqueGenomeCount = 0;
    std::uint64_t uniqueGeneCount = 0;
    std::uint64_t totalGeneReferences = 0;
    std::uint64_t canonicalDecodedBodyBytes = 0;
    std::uint64_t avoidedPerCellGeneDataBytes = 0;
};

struct LogicalSnapshot final {
    DocumentKind documentKind = DocumentKind::Substrate;
    std::vector<MetadataEntry> metadata;
    std::vector<LogicalSection> sections;
    LogicalSnapshotDiagnostics diagnostics;
};

[[nodiscard]] inline const LogicalSection* findLogicalSection(
        const LogicalSnapshot& snapshot,
        std::uint32_t sectionId) noexcept {
    for (const LogicalSection& section : snapshot.sections) {
        if (section.sectionId == sectionId) {
            return &section;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const LogicalColumn* findLogicalColumn(
        const LogicalSection& section,
        std::uint32_t fieldId) noexcept {
    for (const LogicalColumn& column : section.columns) {
        if (column.fieldId == fieldId) {
            return &column;
        }
    }
    return nullptr;
}

} // namespace cellularity::save_format::release
