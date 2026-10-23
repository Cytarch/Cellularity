#include "formats/release/SaveContainerCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

#include <zlib.h>

#include "io/DecodeError.h"

namespace cellularity::save_format::release {
namespace {

constexpr std::uint16_t kMaximumBootstrapBytes = 4096;
constexpr std::uint32_t kSupportedRequiredFeatureBits = 0;
constexpr std::uint32_t kSupportedHeaderFlags = 0;
constexpr std::uint16_t kSupportedDescriptorFlags = kDescriptorCriticalFlag;

constexpr std::uint8_t kSubstrateMask = 1U;
constexpr std::uint8_t kGenomeMask = 2U;
constexpr std::uint8_t kBothDocumentKinds = kSubstrateMask | kGenomeMask;

struct MetadataFieldDefinition final {
    std::uint32_t id;
    const char* name;
    MetadataValueType type;
    std::uint8_t documentMask;
    bool required;
};

constexpr std::array<MetadataFieldDefinition, 23> kMetadataFields{{
        {metadata_field::kContentSchemaMajor,
                "content_schema_major", MetadataValueType::U32, kBothDocumentKinds, true},
        {metadata_field::kContentSchemaMinor,
                "content_schema_minor", MetadataValueType::U32, kBothDocumentKinds, true},
        {metadata_field::kSavedAtUnixMillis,
                "saved_at_unix_millis", MetadataValueType::I64, kBothDocumentKinds, false},
        {metadata_field::kSubstrateSimulationTimeHours,
                "simulation_time_hours", MetadataValueType::F64, kSubstrateMask, true},
        {metadata_field::kSubstrateCellCount,
                "cell_count", MetadataValueType::U64, kSubstrateMask, true},
        {metadata_field::kSubstrateFoodCount,
                "food_count", MetadataValueType::U64, kSubstrateMask, true},
        {metadata_field::kSubstrateFoodMass,
                "food_mass", MetadataValueType::F64, kSubstrateMask, true},
        {metadata_field::kSubstrateDishRadius,
                "dish_radius", MetadataValueType::F64, kSubstrateMask, true},
        {metadata_field::kSubstrateMaximumCellCount,
                "max_cell_count", MetadataValueType::U64, kSubstrateMask, true},
        {metadata_field::kSubstrateMaximumFoodCount,
                "max_food_count", MetadataValueType::U64, kSubstrateMask, true},
        {metadata_field::kSubstrateGenomeCount,
                "genome_count", MetadataValueType::U64, kSubstrateMask, false},
        {metadata_field::kSubstrateUniqueGeneCount,
                "unique_gene_count", MetadataValueType::U64, kSubstrateMask, false},
        {metadata_field::kSubstrateTopologyEdgeCount,
                "topology_edge_count", MetadataValueType::U64, kSubstrateMask, false},
        {metadata_field::kSubstrateDeadSplashCount,
                "dead_splash_count", MetadataValueType::U64, kSubstrateMask, false},
        {metadata_field::kSubstrateCreatedAtUnixMillis,
                "created_at_unix_millis", MetadataValueType::I64, kSubstrateMask, false},
        {metadata_field::kSubstrateKnownOrganismCount,
                "known_organism_count", MetadataValueType::U64, kSubstrateMask, true},
        {metadata_field::kSubstrateOrganismIdentityComplete,
                "organism_identity_complete", MetadataValueType::U32, kSubstrateMask, true},
        {metadata_field::kSubstrateNextOrganismId,
                "next_organism_id", MetadataValueType::U64, kSubstrateMask, true},
        {metadata_field::kSubstrateDishInnerRadius,
                "dish_inner_radius", MetadataValueType::F64, kSubstrateMask, false},
        {metadata_field::kGenomeGeneCount,
                "gene_count", MetadataValueType::U32, kGenomeMask, true},
        {metadata_field::kGenomeInitialGeneIndex,
                "initial_gene_index", MetadataValueType::U32, kGenomeMask, true},
        {metadata_field::kGenomeUniqueGeneCount,
                "unique_gene_count", MetadataValueType::U32, kGenomeMask, false},
        {metadata_field::kGenomeCellTypeMask,
                "cell_type_mask", MetadataValueType::U32, kGenomeMask, false},
}};

struct SectionDefinition final {
    std::uint32_t id;
    const char* name;
};

constexpr std::array<SectionDefinition, 8> kSections{{
        {section_id::kEnvironment, "environment"},
        {section_id::kGenes, "genes"},
        {section_id::kGenomes, "genomes"},
        {section_id::kCells, "cells"},
        {section_id::kTopologyEdges, "topology_edges"},
        {section_id::kTopologyBoundaries, "topology_boundaries"},
        {section_id::kFood, "food"},
        {section_id::kDeadSplashes, "dead_splashes"},
}};

[[noreturn]] void fail(DecodeErrorCode code, const std::string& message) {
    throw DecodeError(code, message);
}

std::uint8_t documentMask(DocumentKind kind) {
    switch (kind) {
        case DocumentKind::Substrate:
            return kSubstrateMask;
        case DocumentKind::Genome:
            return kGenomeMask;
    }
    fail(DecodeErrorCode::UnsupportedFeature,
            "Unsupported release save document kind");
}

const MetadataFieldDefinition* findMetadataField(std::uint32_t id) {
    const auto found = std::find_if(
            kMetadataFields.begin(), kMetadataFields.end(),
            [id](const MetadataFieldDefinition& field) {
                return field.id == id;
            });
    return found == kMetadataFields.end() ? nullptr : &*found;
}

const SectionDefinition* findSection(std::uint32_t id) {
    const auto found = std::find_if(
            kSections.begin(), kSections.end(),
            [id](const SectionDefinition& section) {
                return section.id == id;
            });
    return found == kSections.end() ? nullptr : &*found;
}

bool checkedAdd(std::uint64_t left,
        std::uint64_t right,
        std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(std::uint64_t left,
        std::uint64_t right,
        std::uint64_t& result) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

class Writer final {
public:
    explicit Writer(std::uint64_t maximumBytes) : mMaximumBytes(maximumBytes) {
    }

    void u8(std::uint8_t value) {
        ensure(1);
        mBytes.push_back(value);
    }

    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }

    void u32(std::uint32_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
        u8(static_cast<std::uint8_t>(value >> 16U));
        u8(static_cast<std::uint8_t>(value >> 24U));
    }

    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value));
        u32(static_cast<std::uint32_t>(value >> 32U));
    }

    void raw(const std::string& value) {
        ensure(value.size());
        mBytes.insert(mBytes.end(), value.begin(), value.end());
    }

    void raw(const std::vector<std::uint8_t>& value) {
        ensure(value.size());
        mBytes.insert(mBytes.end(), value.begin(), value.end());
    }

    void patchU32(std::size_t offset, std::uint32_t value) {
        if (offset > mBytes.size() || mBytes.size() - offset < 4) {
            throw std::logic_error("Release writer patch is out of bounds");
        }
        mBytes[offset] = static_cast<std::uint8_t>(value);
        mBytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
        mBytes[offset + 2] = static_cast<std::uint8_t>(value >> 16U);
        mBytes[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const {
        return mBytes;
    }

    [[nodiscard]] std::vector<std::uint8_t> take() {
        return std::move(mBytes);
    }

private:
    void ensure(std::size_t count) const {
        if (static_cast<std::uint64_t>(count) > mMaximumBytes - mBytes.size()) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Release save output exceeds its structural limit");
        }
    }

    std::uint64_t mMaximumBytes;
    std::vector<std::uint8_t> mBytes;
};

class Reader final {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : mBytes(bytes) {
    }

    [[nodiscard]] std::uint8_t u8() {
        require(1);
        return mBytes[mPosition++];
    }

    [[nodiscard]] std::uint16_t u16() {
        const std::uint16_t result = u8();
        return static_cast<std::uint16_t>(result | (u8() << 8U));
    }

    [[nodiscard]] std::uint32_t u32() {
        const std::uint32_t low = u16();
        return low | (static_cast<std::uint32_t>(u16()) << 16U);
    }

    [[nodiscard]] std::uint64_t u64() {
        const std::uint64_t low = u32();
        return low | (static_cast<std::uint64_t>(u32()) << 32U);
    }

    [[nodiscard]] std::string text(std::uint32_t count) {
        require(count);
        std::string result(
                mBytes.begin() + static_cast<std::ptrdiff_t>(mPosition),
                mBytes.begin() + static_cast<std::ptrdiff_t>(mPosition + count));
        mPosition += count;
        return result;
    }

    void skip(std::uint32_t count) {
        require(count);
        mPosition += count;
    }

    [[nodiscard]] std::size_t remaining() const {
        return mBytes.size() - mPosition;
    }

private:
    void require(std::uint64_t count) const {
        if (count > remaining()) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Release save structure ended unexpectedly");
        }
    }

    const std::vector<std::uint8_t>& mBytes;
    std::size_t mPosition = 0;
};

void validateName(const std::string& name, const char* context) {
    if (name.empty() || name.size() > kMaximumNameBytes) {
        fail(DecodeErrorCode::InvalidLength,
                std::string("Invalid release ") + context + " name length");
    }
    for (std::size_t index = 0; index < name.size(); ++index) {
        const unsigned char value = static_cast<unsigned char>(name[index]);
        const bool valid = (value >= 'a' && value <= 'z')
                || (index > 0 && value >= '0' && value <= '9')
                || (index > 0 && value == '_');
        if (!valid) {
            fail(DecodeErrorCode::InvalidValue,
                    std::string("Release ") + context
                            + " name must be lowercase ASCII snake_case");
        }
    }
}

std::uint32_t metadataValueBytes(MetadataValueType type) {
    switch (type) {
        case MetadataValueType::U32:
            return 4;
        case MetadataValueType::U64:
        case MetadataValueType::I64:
        case MetadataValueType::F64:
            return 8;
    }
    fail(DecodeErrorCode::UnsupportedFeature,
            "Unsupported release metadata value type");
}

std::uint64_t primitiveWidth(std::uint8_t type) {
    switch (static_cast<PrimitiveType>(type)) {
        case PrimitiveType::U8:
        case PrimitiveType::I8:
            return 1;
        case PrimitiveType::U16:
            return 2;
        case PrimitiveType::U32:
        case PrimitiveType::I32:
        case PrimitiveType::F32:
            return 4;
        case PrimitiveType::U64:
        case PrimitiveType::I64:
        case PrimitiveType::F64:
            return 8;
    }
    return 0;
}

bool isKnownPhysicalEncoding(std::uint8_t encoding) {
    return encoding == static_cast<std::uint8_t>(PhysicalEncoding::Plain)
            || encoding == static_cast<std::uint8_t>(PhysicalEncoding::Constant);
}

bool isKnownCompressionCodec(std::uint16_t codec) {
    return codec == static_cast<std::uint16_t>(CompressionCodec::None)
            || codec == static_cast<std::uint16_t>(CompressionCodec::Zlib)
            || codec == static_cast<std::uint16_t>(CompressionCodec::Zstd);
}

struct Region final {
    std::uint64_t offset;
    std::uint64_t bytes;
};

bool overlaps(const Region& left, const Region& right) {
    return left.offset < right.offset + right.bytes
            && right.offset < left.offset + left.bytes;
}

Region checkedRegion(std::uint64_t offset,
        std::uint64_t bytes,
        std::uint64_t fileBytes,
        const char* name) {
    std::uint64_t end = 0;
    if (!checkedAdd(offset, bytes, end) || end > fileBytes) {
        fail(DecodeErrorCode::InvalidLength,
                std::string("Release ") + name + " region is out of bounds");
    }
    return Region{offset, bytes};
}

void validateBootstrap(const BootstrapHeader& header,
        std::uint64_t actualFileBytes) {
    if (header.containerMajor == 0) {
        fail(DecodeErrorCode::InvalidVersion,
                "Release container major version is zero");
    }
    if (header.containerMajor > kContainerMajor) {
        fail(DecodeErrorCode::NewerSnapshotVersion,
                "Release container requires a newer major version");
    }
    (void)documentMask(header.documentKind);
    if (header.headerBytes < kBootstrapBytes
            || header.headerBytes > kMaximumBootstrapBytes) {
        fail(DecodeErrorCode::InvalidLength,
                "Release bootstrap byte count is invalid");
    }
    if ((header.requiredFeatureBits & ~kSupportedRequiredFeatureBits) != 0) {
        fail(DecodeErrorCode::UnsupportedFeature,
                "Release container requires an unsupported feature");
    }
    if ((header.headerFlags & ~kSupportedHeaderFlags) != 0
            || header.reserved0 != 0) {
        fail(DecodeErrorCode::InvalidValue,
                "Release bootstrap has nonzero reserved flags");
    }
    if (actualFileBytes < header.headerBytes
            || actualFileBytes > kMaximumFileBytes
            || header.declaredFileBytes != actualFileBytes) {
        fail(DecodeErrorCode::InvalidLength,
                "Release declared file size does not match the input");
    }
    if (header.metadataBytes == 0
            || header.metadataBytes > kMaximumMetadataBytes
            || header.directoryBytes < 8
            || header.directoryBytes > kMaximumDirectoryBytes) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release metadata or directory exceeds its structural limit");
    }

    const Region bootstrap{0, header.headerBytes};
    const Region metadata = checkedRegion(
            header.metadataOffset, header.metadataBytes,
            actualFileBytes, "metadata");
    const Region directory = checkedRegion(
            header.directoryOffset, header.directoryBytes,
            actualFileBytes, "directory");
    if (overlaps(bootstrap, metadata) || overlaps(bootstrap, directory)
            || overlaps(metadata, directory)) {
        fail(DecodeErrorCode::InvalidLength,
                "Release bootstrap regions overlap");
    }

    if (header.previewCodec == static_cast<std::uint16_t>(PreviewCodec::None)) {
        if (header.previewFlags != 0 || header.previewPixelWidth != 0
                || header.previewPixelHeight != 0 || header.previewOffset != 0
                || header.previewBytes != 0 || header.previewCrc32 != 0) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release NONE preview has nonzero descriptor fields");
        }
        return;
    }

    std::uint64_t pixels = 0;
    if (header.previewPixelWidth == 0 || header.previewPixelHeight == 0
            || header.previewPixelWidth > kMaximumPreviewDimension
            || header.previewPixelHeight > kMaximumPreviewDimension
            || !checkedMultiply(header.previewPixelWidth,
                    header.previewPixelHeight, pixels)
            || pixels > kMaximumPreviewPixels
            || header.previewBytes == 0
            || header.previewBytes > kMaximumPreviewBytes) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release preview exceeds its structural limit");
    }
    const Region preview = checkedRegion(
            header.previewOffset, header.previewBytes,
            actualFileBytes, "preview");
    if (overlaps(bootstrap, preview) || overlaps(metadata, preview)
            || overlaps(directory, preview)) {
        fail(DecodeErrorCode::InvalidLength,
                "Release preview overlaps another container region");
    }
}

void requireMetadataFields(DocumentKind kind,
        const std::set<std::uint32_t>& present) {
    const std::uint8_t mask = documentMask(kind);
    for (const MetadataFieldDefinition& field : kMetadataFields) {
        if (field.required && (field.documentMask & mask) != 0
                && present.count(field.id) == 0) {
            fail(DecodeErrorCode::InvalidValue,
                    std::string("Release metadata is missing required field ")
                            + field.name);
        }
    }
}

void validateMetadataEntry(DocumentKind kind,
        const MetadataEntry& entry,
        bool allowUnknownOptional) {
    validateName(entry.canonicalName, "metadata field");
    const MetadataFieldDefinition* definition = findMetadataField(entry.fieldId);
    if (definition == nullptr) {
        if (entry.critical || !allowUnknownOptional) {
            fail(DecodeErrorCode::UnsupportedFeature,
                    "Release metadata contains an unknown critical field");
        }
        return;
    }
    if ((definition->documentMask & documentMask(kind)) == 0
            || entry.canonicalName != definition->name
            || entry.valueType != definition->type
            || entry.critical != definition->required) {
        fail(DecodeErrorCode::InvalidValue,
                "Release metadata field does not match its registered schema");
    }
    if (entry.valueType == MetadataValueType::U32
            && entry.rawBits > std::numeric_limits<std::uint32_t>::max()) {
        fail(DecodeErrorCode::InvalidValue,
                "Release metadata U32 value is out of range");
    }
    if (entry.valueType == MetadataValueType::F64) {
        double value = 0.0;
        std::memcpy(&value, &entry.rawBits, sizeof(value));
        if (!std::isfinite(value)) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release metadata floating value must be finite");
        }
    }
}

void validateChunkShape(const ColumnDescriptor& column,
        bool effectiveCritical) {
    const std::uint64_t width = primitiveWidth(column.primitiveType);
    const bool knownEncoding = isKnownPhysicalEncoding(column.physicalEncoding);
    if ((width == 0 || !knownEncoding) && effectiveCritical) {
        fail(DecodeErrorCode::UnsupportedFeature,
                "Release critical column uses an unsupported representation");
    }
    if (column.logicalValueCount == 0) {
        if (!column.chunks.empty()) {
            fail(DecodeErrorCode::InvalidCount,
                    "Release empty column contains chunks");
        }
        return;
    }
    if (column.chunks.empty()) {
        fail(DecodeErrorCode::InvalidCount,
                "Release nonempty column has no chunks");
    }

    std::uint64_t expectedFirst = 0;
    for (const ChunkDescriptor& chunk : column.chunks) {
        if (chunk.firstLogicalValue != expectedFirst
                || chunk.logicalValueCount == 0
                || !checkedAdd(expectedFirst, chunk.logicalValueCount,
                        expectedFirst)
                || expectedFirst > column.logicalValueCount) {
            fail(DecodeErrorCode::InvalidCount,
                    "Release chunk logical ranges are not contiguous");
        }
        const bool knownCodec = isKnownCompressionCodec(chunk.compressionCodec);
        if (!knownCodec && effectiveCritical) {
            fail(DecodeErrorCode::UnsupportedFeature,
                    "Release critical column requires an unsupported codec");
        }
        if (chunk.compressionFlags != 0 || chunk.reserved != 0) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release chunk has nonzero reserved fields");
        }
        if (chunk.storedBytes == 0) {
            fail(DecodeErrorCode::InvalidLength,
                    "Release nonempty chunk has no stored bytes");
        }
        if (knownCodec && chunk.storedBytes > kMaximumStoredChunkBytes) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Release stored chunk exceeds the bounded decode limit");
        }
        if (width != 0 && knownEncoding) {
            std::uint64_t decodedBytes = 0;
            if (!checkedMultiply(width, chunk.logicalValueCount, decodedBytes)
                    || decodedBytes > kMaximumDecodedChunkBytes
                    || chunk.decodedBytes != decodedBytes) {
                fail(DecodeErrorCode::LimitExceeded,
                        "Release chunk decoded size is invalid");
            }
            if (column.physicalEncoding
                            == static_cast<std::uint8_t>(PhysicalEncoding::Constant)
                    && knownCodec
                    && (chunk.compressionCodec
                                    != static_cast<std::uint16_t>(CompressionCodec::None)
                            || chunk.storedBytes != width)) {
                fail(DecodeErrorCode::InvalidCompression,
                        "Release constant chunk must store one raw value");
            }
            if (column.physicalEncoding
                            == static_cast<std::uint8_t>(PhysicalEncoding::Plain)
                    && chunk.compressionCodec
                            == static_cast<std::uint16_t>(CompressionCodec::None)
                    && chunk.storedBytes != decodedBytes) {
                fail(DecodeErrorCode::InvalidCompression,
                        "Release raw chunk size does not match decoded size");
            }
        }
    }
    if (expectedFirst != column.logicalValueCount) {
        fail(DecodeErrorCode::InvalidCount,
                "Release chunks do not cover their column");
    }
}

void validateDirectoryModel(const std::vector<SectionDescriptor>& sections,
        const BootstrapHeader* header) {
    if (sections.size() > kMaximumSections) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release directory contains too many sections");
    }
    std::set<std::uint32_t> sectionIds;
    std::set<std::string> sectionNames;
    std::vector<Region> liveRegions;
    if (header != nullptr) {
        liveRegions.push_back(Region{0, header->headerBytes});
        liveRegions.push_back(Region{header->metadataOffset, header->metadataBytes});
        liveRegions.push_back(Region{header->directoryOffset, header->directoryBytes});
        if (header->previewCodec != static_cast<std::uint16_t>(PreviewCodec::None)) {
            liveRegions.push_back(Region{header->previewOffset, header->previewBytes});
        }
    }

    std::uint64_t totalChunks = 0;
    for (const SectionDescriptor& section : sections) {
        validateName(section.canonicalName, "section");
        if (!sectionIds.insert(section.sectionId).second
                || !sectionNames.insert(section.canonicalName).second) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release directory contains a duplicate section");
        }
        if ((section.sectionFlags & ~kSupportedDescriptorFlags) != 0
                || section.schemaMajor == 0) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release section has invalid flags or schema version");
        }
        const bool sectionCritical =
                (section.sectionFlags & kDescriptorCriticalFlag) != 0;
        const SectionDefinition* definition = findSection(section.sectionId);
        if (definition == nullptr) {
            if (sectionCritical) {
                fail(DecodeErrorCode::UnsupportedFeature,
                        "Release directory contains an unknown critical section");
            }
        } else {
            if (section.canonicalName != definition->name) {
                fail(DecodeErrorCode::InvalidValue,
                        "Release section ID and name do not match");
            }
            if (section.schemaMajor > 1 && sectionCritical) {
                fail(DecodeErrorCode::UnsupportedFeature,
                        "Release critical section requires a newer schema");
            }
        }
        if (section.columns.size() > kMaximumColumnsPerSection) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Release section contains too many columns");
        }

        std::set<std::uint32_t> fieldIds;
        std::set<std::string> fieldNames;
        for (const ColumnDescriptor& column : section.columns) {
            validateName(column.canonicalName, "column");
            if (!fieldIds.insert(column.fieldId).second
                    || !fieldNames.insert(column.canonicalName).second) {
                fail(DecodeErrorCode::InvalidValue,
                        "Release section contains a duplicate column");
            }
            if ((column.fieldFlags & ~kSupportedDescriptorFlags) != 0) {
                fail(DecodeErrorCode::InvalidValue,
                        "Release column has nonzero reserved flags");
            }
            if (column.chunks.size() > kMaximumChunks
                    || totalChunks > kMaximumChunks - column.chunks.size()) {
                fail(DecodeErrorCode::LimitExceeded,
                        "Release directory contains too many chunks");
            }
            totalChunks += column.chunks.size();
            const bool fieldCritical =
                    (column.fieldFlags & kDescriptorCriticalFlag) != 0;
            validateChunkShape(column, sectionCritical && fieldCritical);

            if (header == nullptr) {
                continue;
            }
            for (const ChunkDescriptor& chunk : column.chunks) {
                const Region chunkRegion = checkedRegion(
                        chunk.fileOffset, chunk.storedBytes,
                        header->declaredFileBytes, "chunk");
                for (const Region& region : liveRegions) {
                    if (overlaps(region, chunkRegion)) {
                        fail(DecodeErrorCode::InvalidLength,
                                "Release chunk overlaps another live region");
                    }
                }
                liveRegions.push_back(chunkRegion);
            }
        }
    }
}

} // namespace

MetadataEntry MetadataEntry::u32(std::uint32_t fieldId,
        std::string canonicalName,
        std::uint32_t value,
        bool critical) {
    return MetadataEntry{fieldId, MetadataValueType::U32, critical,
            std::move(canonicalName), value};
}

MetadataEntry MetadataEntry::u64(std::uint32_t fieldId,
        std::string canonicalName,
        std::uint64_t value,
        bool critical) {
    return MetadataEntry{fieldId, MetadataValueType::U64, critical,
            std::move(canonicalName), value};
}

MetadataEntry MetadataEntry::i64(std::uint32_t fieldId,
        std::string canonicalName,
        std::int64_t value,
        bool critical) {
    return MetadataEntry{fieldId, MetadataValueType::I64, critical,
            std::move(canonicalName),
            static_cast<std::uint64_t>(value)};
}

MetadataEntry MetadataEntry::f64(std::uint32_t fieldId,
        std::string canonicalName,
        double value,
        bool critical) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return MetadataEntry{fieldId, MetadataValueType::F64, critical,
            std::move(canonicalName), bits};
}

std::uint32_t MetadataEntry::asU32() const {
    if (valueType != MetadataValueType::U32
            || rawBits > std::numeric_limits<std::uint32_t>::max()) {
        throw std::logic_error("Release metadata value is not U32");
    }
    return static_cast<std::uint32_t>(rawBits);
}

std::uint64_t MetadataEntry::asU64() const {
    if (valueType != MetadataValueType::U64) {
        throw std::logic_error("Release metadata value is not U64");
    }
    return rawBits;
}

std::int64_t MetadataEntry::asI64() const {
    if (valueType != MetadataValueType::I64) {
        throw std::logic_error("Release metadata value is not I64");
    }
    return static_cast<std::int64_t>(rawBits);
}

double MetadataEntry::asF64() const {
    if (valueType != MetadataValueType::F64) {
        throw std::logic_error("Release metadata value is not F64");
    }
    double value = 0.0;
    std::memcpy(&value, &rawBits, sizeof(value));
    return value;
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    uLong crc = ::crc32(0L, Z_NULL, 0);
    std::size_t position = 0;
    while (position < size) {
        const std::size_t count = std::min<std::size_t>(
                size - position, std::numeric_limits<uInt>::max());
        crc = ::crc32(crc, bytes + position, static_cast<uInt>(count));
        position += count;
    }
    return static_cast<std::uint32_t>(crc);
}

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) {
    return crc32(bytes.data(), bytes.size());
}

std::vector<std::uint8_t> encodeBootstrap(const BootstrapHeader& header) {
    if (header.headerBytes != kBootstrapBytes) {
        fail(DecodeErrorCode::InvalidLength,
                "Release writer supports only the 112-byte bootstrap");
    }
    validateBootstrap(header, header.declaredFileBytes);
    Writer writer(kBootstrapBytes);
    writer.raw("CLTY");
    writer.u16(header.containerMajor);
    writer.u16(header.containerMinor);
    writer.u16(static_cast<std::uint16_t>(header.documentKind));
    writer.u16(header.headerBytes);
    writer.u32(header.requiredFeatureBits);
    writer.u32(header.optionalFeatureBits);
    writer.u32(header.headerFlags);
    writer.u64(header.declaredFileBytes);
    writer.u64(header.metadataOffset);
    writer.u64(header.metadataBytes);
    writer.u32(header.metadataCrc32);
    writer.u16(header.previewCodec);
    writer.u16(header.previewFlags);
    writer.u32(header.previewPixelWidth);
    writer.u32(header.previewPixelHeight);
    writer.u64(header.previewOffset);
    writer.u64(header.previewBytes);
    writer.u32(header.previewCrc32);
    writer.u32(header.reserved0);
    writer.u64(header.directoryOffset);
    writer.u64(header.directoryBytes);
    writer.u32(header.directoryCrc32);
    writer.u32(0);
    if (writer.bytes().size() != kBootstrapBytes) {
        throw std::logic_error("Release bootstrap layout is not 112 bytes");
    }
    writer.patchU32(108, crc32(writer.bytes().data(), 108));
    return writer.take();
}

BootstrapHeader decodeBootstrap(const std::vector<std::uint8_t>& bootstrapBytes,
        std::uint64_t actualFileBytes) {
    if (bootstrapBytes.size() != kBootstrapBytes) {
        fail(bootstrapBytes.size() < kBootstrapBytes
                        ? DecodeErrorCode::TruncatedInput
                        : DecodeErrorCode::InvalidLength,
                "Release bootstrap must contain exactly 112 bytes");
    }
    if (std::memcmp(bootstrapBytes.data(), "CLTY", 4) != 0) {
        fail(DecodeErrorCode::InvalidHeader,
                "Release bootstrap magic is invalid");
    }
    const std::uint32_t expectedHeaderCrc = crc32(bootstrapBytes.data(), 108);
    Reader reader(bootstrapBytes);
    reader.skip(4);
    BootstrapHeader header;
    header.containerMajor = reader.u16();
    header.containerMinor = reader.u16();
    header.documentKind = static_cast<DocumentKind>(reader.u16());
    header.headerBytes = reader.u16();
    header.requiredFeatureBits = reader.u32();
    header.optionalFeatureBits = reader.u32();
    header.headerFlags = reader.u32();
    header.declaredFileBytes = reader.u64();
    header.metadataOffset = reader.u64();
    header.metadataBytes = reader.u64();
    header.metadataCrc32 = reader.u32();
    header.previewCodec = reader.u16();
    header.previewFlags = reader.u16();
    header.previewPixelWidth = reader.u32();
    header.previewPixelHeight = reader.u32();
    header.previewOffset = reader.u64();
    header.previewBytes = reader.u64();
    header.previewCrc32 = reader.u32();
    header.reserved0 = reader.u32();
    header.directoryOffset = reader.u64();
    header.directoryBytes = reader.u64();
    header.directoryCrc32 = reader.u32();
    const std::uint32_t storedHeaderCrc = reader.u32();
    if (reader.remaining() != 0 || storedHeaderCrc != expectedHeaderCrc) {
        fail(DecodeErrorCode::InvalidValue,
                "Release bootstrap CRC is invalid");
    }
    validateBootstrap(header, actualFileBytes);
    return header;
}

PreviewDisposition classifyPreview(
        const BootstrapHeader& header,
        const std::vector<std::uint8_t>& storedPreviewBytes) noexcept {
    if (header.previewCodec == static_cast<std::uint16_t>(PreviewCodec::None)) {
        return PreviewDisposition::None;
    }
    const bool supportedCodec =
            header.previewCodec == static_cast<std::uint16_t>(PreviewCodec::Png)
            || header.previewCodec == static_cast<std::uint16_t>(PreviewCodec::Jpeg)
            || header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::WebpLossy)
            || header.previewCodec
                    == static_cast<std::uint16_t>(PreviewCodec::WebpLossless);
    if (!supportedCodec || header.previewFlags != 0
            || header.previewBytes != storedPreviewBytes.size()
            || crc32(storedPreviewBytes) != header.previewCrc32) {
        return PreviewDisposition::Placeholder;
    }
    return PreviewDisposition::Available;
}

std::vector<std::uint8_t> encodeMetadata(
        DocumentKind documentKind,
        const std::vector<MetadataEntry>& entries) {
    if (entries.size() > kMaximumMetadataEntries) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release metadata contains too many entries");
    }
    std::vector<MetadataEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const MetadataEntry& left, const MetadataEntry& right) {
        return left.fieldId < right.fieldId;
    });
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    Writer writer(kMaximumMetadataBytes);
    writer.u32(static_cast<std::uint32_t>(sorted.size()));
    for (const MetadataEntry& entry : sorted) {
        validateMetadataEntry(documentKind, entry, false);
        if (!ids.insert(entry.fieldId).second
                || !names.insert(entry.canonicalName).second) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release metadata contains a duplicate field");
        }
        writer.u32(entry.fieldId);
        writer.u8(static_cast<std::uint8_t>(entry.valueType));
        writer.u8(entry.critical ? kCriticalFlag : 0);
        writer.u16(static_cast<std::uint16_t>(entry.canonicalName.size()));
        writer.u32(metadataValueBytes(entry.valueType));
        writer.raw(entry.canonicalName);
        if (entry.valueType == MetadataValueType::U32) {
            writer.u32(static_cast<std::uint32_t>(entry.rawBits));
        } else {
            writer.u64(entry.rawBits);
        }
    }
    requireMetadataFields(documentKind, ids);
    return writer.take();
}

std::vector<MetadataEntry> decodeMetadata(
        DocumentKind documentKind,
        const std::vector<std::uint8_t>& bytes,
        std::uint32_t expectedCrc32) {
    if (bytes.size() > kMaximumMetadataBytes) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release metadata exceeds its byte limit");
    }
    if (crc32(bytes) != expectedCrc32) {
        fail(DecodeErrorCode::InvalidValue,
                "Release metadata CRC is invalid");
    }
    Reader reader(bytes);
    const std::uint32_t count = reader.u32();
    if (count > kMaximumMetadataEntries) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release metadata contains too many entries");
    }
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    std::vector<MetadataEntry> entries;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t fieldId = reader.u32();
        const auto valueType = static_cast<MetadataValueType>(reader.u8());
        const std::uint8_t flags = reader.u8();
        const std::uint16_t nameBytes = reader.u16();
        const std::uint32_t valueBytes = reader.u32();
        if ((flags & ~kCriticalFlag) != 0 || nameBytes > kMaximumNameBytes) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release metadata entry has invalid flags or name size");
        }
        const std::string name = reader.text(nameBytes);
        validateName(name, "metadata field");
        if (!ids.insert(fieldId).second || !names.insert(name).second) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release metadata contains a duplicate field");
        }
        const bool critical = (flags & kCriticalFlag) != 0;
        const MetadataFieldDefinition* definition = findMetadataField(fieldId);
        if (definition == nullptr) {
            if (critical) {
                fail(DecodeErrorCode::UnsupportedFeature,
                        "Release metadata contains an unknown critical field");
            }
            reader.skip(valueBytes);
            continue;
        }
        if ((definition->documentMask & documentMask(documentKind)) == 0
                || name != definition->name
                || valueType != definition->type
                || critical != definition->required
                || valueBytes != metadataValueBytes(valueType)) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release metadata field does not match its registered schema");
        }
        const std::uint64_t rawBits = valueBytes == 4 ? reader.u32() : reader.u64();
        MetadataEntry entry{fieldId, valueType, critical, name, rawBits};
        validateMetadataEntry(documentKind, entry, true);
        entries.push_back(std::move(entry));
    }
    if (reader.remaining() != 0) {
        fail(DecodeErrorCode::TrailingData,
                "Release metadata contains trailing bytes");
    }
    requireMetadataFields(documentKind, ids);
    return entries;
}

std::vector<std::uint8_t> encodeDirectory(
        const std::vector<SectionDescriptor>& sections) {
    validateDirectoryModel(sections, nullptr);
    std::vector<SectionDescriptor> sortedSections = sections;
    std::sort(sortedSections.begin(), sortedSections.end(),
            [](const SectionDescriptor& left, const SectionDescriptor& right) {
                return left.sectionId < right.sectionId;
            });
    Writer writer(kMaximumDirectoryBytes);
    writer.u32(kDirectoryFormatVersion);
    writer.u32(static_cast<std::uint32_t>(sortedSections.size()));
    for (SectionDescriptor& section : sortedSections) {
        std::sort(section.columns.begin(), section.columns.end(),
                [](const ColumnDescriptor& left, const ColumnDescriptor& right) {
                    return left.fieldId < right.fieldId;
                });
        writer.u32(section.sectionId);
        writer.u16(section.schemaMajor);
        writer.u16(section.schemaMinor);
        writer.u32(section.sectionFlags);
        writer.u64(section.logicalRowCount);
        writer.u32(static_cast<std::uint32_t>(section.canonicalName.size()));
        writer.u32(static_cast<std::uint32_t>(section.columns.size()));
        writer.raw(section.canonicalName);
        for (const ColumnDescriptor& column : section.columns) {
            writer.u32(column.fieldId);
            writer.u8(column.primitiveType);
            writer.u8(column.physicalEncoding);
            writer.u16(column.fieldFlags);
            writer.u64(column.logicalValueCount);
            writer.u32(static_cast<std::uint32_t>(column.canonicalName.size()));
            writer.u32(static_cast<std::uint32_t>(column.chunks.size()));
            writer.raw(column.canonicalName);
            for (const ChunkDescriptor& chunk : column.chunks) {
                writer.u64(chunk.firstLogicalValue);
                writer.u64(chunk.logicalValueCount);
                writer.u16(chunk.compressionCodec);
                writer.u16(chunk.compressionFlags);
                writer.u32(chunk.codecParameter);
                writer.u64(chunk.fileOffset);
                writer.u64(chunk.storedBytes);
                writer.u64(chunk.decodedBytes);
                writer.u32(chunk.decodedCrc32);
                writer.u32(chunk.reserved);
            }
        }
    }
    return writer.take();
}

std::vector<SectionDescriptor> decodeDirectory(
        const std::vector<std::uint8_t>& bytes,
        std::uint32_t expectedCrc32,
        const BootstrapHeader& header) {
    validateBootstrap(header, header.declaredFileBytes);
    if (bytes.size() > kMaximumDirectoryBytes) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release directory exceeds its byte limit");
    }
    if (bytes.size() != header.directoryBytes
            || expectedCrc32 != header.directoryCrc32
            || crc32(bytes) != expectedCrc32) {
        fail(DecodeErrorCode::InvalidValue,
                "Release directory size or CRC is invalid");
    }
    Reader reader(bytes);
    const std::uint32_t version = reader.u32();
    if (version == 0) {
        fail(DecodeErrorCode::InvalidVersion,
                "Release directory version is zero");
    }
    if (version > kDirectoryFormatVersion) {
        fail(DecodeErrorCode::UnsupportedFeature,
                "Release directory requires a newer decoder");
    }
    const std::uint32_t sectionCount = reader.u32();
    if (sectionCount > kMaximumSections) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release directory contains too many sections");
    }
    std::vector<SectionDescriptor> sections;
    sections.reserve(sectionCount);
    for (std::uint32_t sectionIndex = 0;
            sectionIndex < sectionCount;
            ++sectionIndex) {
        SectionDescriptor section;
        section.sectionId = reader.u32();
        section.schemaMajor = reader.u16();
        section.schemaMinor = reader.u16();
        section.sectionFlags = reader.u32();
        section.logicalRowCount = reader.u64();
        const std::uint32_t nameBytes = reader.u32();
        const std::uint32_t columnCount = reader.u32();
        if (nameBytes > kMaximumNameBytes
                || columnCount > kMaximumColumnsPerSection) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Release section descriptor exceeds its limit");
        }
        section.canonicalName = reader.text(nameBytes);
        section.columns.reserve(columnCount);
        for (std::uint32_t columnIndex = 0;
                columnIndex < columnCount;
                ++columnIndex) {
            ColumnDescriptor column;
            column.fieldId = reader.u32();
            column.primitiveType = reader.u8();
            column.physicalEncoding = reader.u8();
            column.fieldFlags = reader.u16();
            column.logicalValueCount = reader.u64();
            const std::uint32_t columnNameBytes = reader.u32();
            const std::uint32_t chunkCount = reader.u32();
            if (columnNameBytes > kMaximumNameBytes
                    || chunkCount > kMaximumChunks) {
                fail(DecodeErrorCode::LimitExceeded,
                        "Release column descriptor exceeds its limit");
            }
            column.canonicalName = reader.text(columnNameBytes);
            column.chunks.reserve(chunkCount);
            for (std::uint32_t chunkIndex = 0;
                    chunkIndex < chunkCount;
                    ++chunkIndex) {
                ChunkDescriptor chunk;
                chunk.firstLogicalValue = reader.u64();
                chunk.logicalValueCount = reader.u64();
                chunk.compressionCodec = reader.u16();
                chunk.compressionFlags = reader.u16();
                chunk.codecParameter = reader.u32();
                chunk.fileOffset = reader.u64();
                chunk.storedBytes = reader.u64();
                chunk.decodedBytes = reader.u64();
                chunk.decodedCrc32 = reader.u32();
                chunk.reserved = reader.u32();
                column.chunks.push_back(chunk);
            }
            section.columns.push_back(std::move(column));
        }
        sections.push_back(std::move(section));
    }
    if (reader.remaining() != 0) {
        fail(DecodeErrorCode::TrailingData,
                "Release directory contains trailing bytes");
    }
    validateDirectoryModel(sections, &header);
    return sections;
}

} // namespace cellularity::save_format::release
