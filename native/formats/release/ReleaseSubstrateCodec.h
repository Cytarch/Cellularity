#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "formats/release/SaveContainerBody.h"

class SimulationWorld;
class CellData;
class DeadSplashData;
class Environment;
class FoodData;

namespace cellularity::save_format::release {

constexpr std::uint32_t kSubstrateContentSchemaMajor = 1;
constexpr std::uint32_t kSubstrateContentSchemaMinor = 2;

struct ReleaseSubstrateInspection final {
    BootstrapHeader header;
    PreviewDisposition previewDisposition = PreviewDisposition::None;
    std::uint32_t contentSchemaMajor = 0;
    std::uint32_t contentSchemaMinor = 0;
    double simulationTimeHours = 0.0;
    std::uint64_t cellCount = 0;
    std::uint64_t foodCount = 0;
    double foodMass = 0.0;
    double dishRadius = 0.0;
    double dishInnerRadius = 0.0;
    std::uint64_t maximumCellCount = 0;
    std::uint64_t maximumFoodCount = 0;
    std::uint64_t genomeCount = 0;
    std::uint64_t uniqueGeneCount = 0;
    std::uint64_t topologyEdgeCount = 0;
    std::uint64_t deadSplashCount = 0;
    std::uint64_t knownOrganismCount = 0;
    bool organismIdentityComplete = false;
    std::int64_t createdAtUnixMillis = 0;
};

/** Exports every persisted field directly from runtime SoA arrays and pools. */
[[nodiscard]] LogicalSnapshot exportSubstrateRuntimeSoa(
        const SimulationWorld& world);

/** Test/laboratory boundary that avoids constructing render/grid state. */
[[nodiscard]] LogicalSnapshot exportSubstrateRuntimeSoa(
        const Environment& environment,
        const CellData& cells,
        const FoodData& food,
        const DeadSplashData& deadSplashes,
        double simulationTimeSeconds,
        std::int64_t createdAtUnixMillis = 0);

/** Encodes one complete release substrate in deterministic canonical order. */
[[nodiscard]] ContainerArtifact encodeReleaseSubstrate(
        const SimulationWorld& world,
        const EncodedPreview& preview = {},
        const BodyWriteOptions& options = {});

/** Production substrate writer: bounded chunks, no complete-file buffer. */
[[nodiscard]] ContainerWriteResult writeReleaseSubstratePath(
        const std::string& path,
        const SimulationWorld& world,
        const EncodedPreview& preview = {},
        const BodyWriteOptions& options = {});

/** Validates and imports a release substrate into a new unpublished world. */
[[nodiscard]] std::unique_ptr<SimulationWorld> decodeReleaseSubstrate(
        const std::vector<std::uint8_t>& bytes,
        bool previewMode);

/** Production substrate reader: indexes the trailing directory first. */
[[nodiscard]] std::unique_ptr<SimulationWorld> decodeReleaseSubstratePath(
        const std::string& path,
        bool previewMode);

/** Validates every stored body chunk without a complete-file allocation. */
void validateReleaseSubstratePath(const std::string& path);

/** Bounded metadata/preview inspection without decoding body columns. */
[[nodiscard]] ReleaseSubstrateInspection inspectReleaseSubstrate(
        const std::vector<std::uint8_t>& bytes);
/** Inspects a bounded prefix while validating offsets against the full file. */
[[nodiscard]] ReleaseSubstrateInspection inspectReleaseSubstratePrefix(
        const std::vector<std::uint8_t>& prefixBytes,
        std::uint64_t completeFileBytes);

/** Physical-format-independent SHA-256 over canonical logical state. */
[[nodiscard]] std::string substrateSemanticDigest(const SimulationWorld& world);
[[nodiscard]] std::string logicalSnapshotSemanticDigest(
        const LogicalSnapshot& snapshot);

} // namespace cellularity::save_format::release
