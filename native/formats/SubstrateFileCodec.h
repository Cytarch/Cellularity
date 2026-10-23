#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Environment.h"
#include "formats/SubstrateFormatTypes.h"

class SimulationWorld;

/** Inspects a bounded substrate prefix without constructing a simulation world. */
NativeSubstrateInspection inspectSubstrateHeader(
        std::vector<std::uint8_t> prefixBytes,
        bool previewMode,
        const Environment& fallbackEnvironment);
NativeSubstrateInspection inspectSubstratePath(
        const std::string& path,
        bool previewMode,
        const Environment& fallbackEnvironment);

void writeSubstratePath(const std::string& path, const SimulationWorld& world);
/** Attaches an Android-encoded WebP preview to a writer-owned temporary file. */
void attachSubstratePreviewPath(
        const std::string& path,
        const std::vector<std::uint8_t>& webpBytes);
/** Reads and CRC-checks only the optional encoded release preview range. */
[[nodiscard]] std::vector<std::uint8_t> readSubstratePreviewPath(
        const std::string& path);
std::vector<std::uint8_t> writeSubstrateInspectionSnapshot(
        const NativeSubstrateInspection& inspection);
