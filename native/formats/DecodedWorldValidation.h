#pragma once

#include <cstddef>

#include "core/Gene.h"

class SimulationWorld;
class Environment;

namespace cellularity::snapshot_codec {

/** Validates one decoded Gene against its owning Genome length. */
void validateDecodedGene(
        const GeneData& gene,
        const char* formatName,
        std::size_t geneCount);

/** Rejects unsafe or non-finite Environment allocation inputs. */
void validateEnvironmentForAllocation(const Environment& environment);

/** Shared semantic validation used by release and frozen legacy imports. */
void validateDecodedWorldDynamicValues(const SimulationWorld& world);

/** Applies constructor-derived Environment fields at runtime boundaries. */
void preserveEnvironmentRuntimeFields(
        SimulationWorld& world,
        const Environment& source);

} // namespace cellularity::snapshot_codec
