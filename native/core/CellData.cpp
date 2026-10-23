#include "core/CellData.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/SimulationWorld.h"
#include "core/SimulationWorldProfile.h"
#include "core/Environment.h"
#include "core/GeneAccess.h"
#include "core/GenomeMetadata.h"
#include "core/ProfileInstrumentation.h"
#include "verification/SemanticTrace.h"

namespace {

#ifdef CELLULARITY_PROFILE
#define CELLULARITY_PROFILE_IF(profile_ptr) if ((profile_ptr) != nullptr)
#else
#define CELLULARITY_PROFILE_IF(profile_ptr) if constexpr (false)
#endif

#ifndef CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
#if defined(__clang__) || defined(__GNUC__)
#define CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD 1
#else
#define CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD 0
#endif
#endif

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;
constexpr double FOUR_PI = 4.0 * PI;
constexpr float TWO_PI_F = static_cast<float>(TWO_PI);
constexpr int MIN_RANDOM_FOUNDER_GENE_COUNT = 1;
constexpr int MAX_RANDOM_FOUNDER_GENE_COUNT = 40;
static_assert(MAX_RANDOM_FOUNDER_GENE_COUNT
        <= static_cast<int>(cellularity::MAX_SUPPORTED_GENES_PER_GENOME));

double signum(double value) {
    if (value > 0.0) {
        return 1.0;
    }
    if (value < 0.0) {
        return -1.0;
    }
    return 0.0;
}

float randomSplittableSeedSplitMass(SimulationRandom& random) {
    return static_cast<float>(CellDataConstants::MIN_MASS
            + random.nextUniform() * (CellDataConstants::MAX_MASS - CellDataConstants::MIN_MASS));
}

float randomSeedSplitMass(SimulationRandom& random) {
    if (random.nextInt(8) == 0) {
        return CellDataConstants::MAX_SPLIT_MASS;
    }
    return randomSplittableSeedSplitMass(random);
}

int randomLocalSeedCycleGeneIndex(int parentGeneIndex, SimulationRandom& random) {
    constexpr int LOCAL_CYCLE_WINDOW = 6;
    int localAncestorCount = std::min(LOCAL_CYCLE_WINDOW, parentGeneIndex);
    if (parentGeneIndex > 0 && random.nextInt(4) == 0) {
        return 0;
    }
    if (localAncestorCount == 0 || random.nextInt(2) == 0) {
        return parentGeneIndex;
    }
    return parentGeneIndex - 1 - random.nextInt(localAncestorCount);
}

int seedTreeChildGeneIndex(int parentGeneIndex, int childOffset, int activeGeneCount, SimulationRandom& random) {
    int childGeneIndex = parentGeneIndex * 2 + childOffset;
    if (childGeneIndex < activeGeneCount) {
        return childGeneIndex;
    }
    return randomLocalSeedCycleGeneIndex(parentGeneIndex, random);
}

/**
 * Copies one Gene between differently sized local-index domains.
 *
 * Cross-domain reference table (S = source count, D = destination count):
 *
 *   child1, child2, virus-from, virus-to
 *     source:      resolve SELF against the source owner's index in 0..<S
 *     destination: resolved source index modulo D
 *
 * Initial-gene identity is deliberately not transferred. It belongs to the
 * destination GenomeRecord and callers restore its metadata after all copies.
 * When S == D every valid explicit reference is unchanged, preserving the
 * historical 40-Gene transfer and crossover behavior exactly.
 */
bool copyGeneAcrossGenomeDomains(
        GeneData& destination,
        cellularity::GeneView source,
        cellularity::GeneIndex sourceOwner,
        std::size_t sourceGeneCount,
        std::size_t destinationGeneCount) {
    if (!cellularity::isGeneIndexInBounds(sourceOwner, sourceGeneCount)
            || !cellularity::isSupportedGeneCount(destinationGeneCount)) {
        return false;
    }

    const auto remapReference = [&](cellularity::GeneIndex reference)
            -> std::optional<cellularity::GeneIndex> {
        const cellularity::GeneIndex resolved = cellularity::resolveGeneReference(
                reference, sourceOwner, sourceGeneCount);
        if (resolved == cellularity::INVALID_GENE_INDEX) {
            return std::nullopt;
        }
        return cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(
                static_cast<std::size_t>(resolved.value())
                % destinationGeneCount));
    };

    const auto child1 = remapReference(source.child1GeneIndex());
    const auto child2 = remapReference(source.child2GeneIndex());
    const auto virusFrom = remapReference(source.virusFromGeneIndex());
    const auto virusTo = remapReference(source.virusToGeneIndex());
    if (!child1.has_value()
            || !child2.has_value()
            || !virusFrom.has_value()
            || !virusTo.has_value()) {
        return false;
    }

    cellularity::GeneBuilder builder(destination);
    builder.copyFrom(source);
    builder.setChild1GeneIndex(*child1);
    builder.setChild2GeneIndex(*child2);
    builder.setVirusFromGeneIndex(*virusFrom);
    builder.setVirusToGeneIndex(*virusTo);
    builder.setInitialCompatibilityFlag(false);
    return true;
}

/**
 * Maps a source Gene to the victim placement domain for viral transfer.
 *
 * sourceOffset = (sourceIndex - sourceStart) modulo sourceCount
 * destination   = (targetStart + sourceOffset) modulo victimCount
 *
 * This is the old offset placement when both counts are equal. If several
 * source Genes fold onto one smaller victim slot, traversal order is the
 * deterministic tie-breaker and the later write wins.
 */
cellularity::GeneIndex viralTransferDestination(
        cellularity::GeneIndex sourceIndex,
        cellularity::GeneIndex sourceStart,
        std::size_t sourceGeneCount,
        cellularity::GeneIndex targetStart,
        std::size_t victimGeneCount) {
    assert(cellularity::isGeneIndexInBounds(sourceIndex, sourceGeneCount));
    assert(cellularity::isGeneIndexInBounds(sourceStart, sourceGeneCount));
    assert(cellularity::isGeneIndexInBounds(targetStart, victimGeneCount));
    const std::size_t sourceOffset =
            (static_cast<std::size_t>(sourceIndex.value())
                    + sourceGeneCount
                    - static_cast<std::size_t>(sourceStart.value()))
            % sourceGeneCount;
    return cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(
            (static_cast<std::size_t>(targetStart.value()) + sourceOffset)
            % victimGeneCount));
}

bool isSeedTreeDescendant(int candidateGeneIndex, int ancestorGeneIndex) {
    while (candidateGeneIndex > 0) {
        candidateGeneIndex = (candidateGeneIndex - 1) / 2;
        if (candidateGeneIndex == ancestorGeneIndex) {
            return true;
        }
    }
    return false;
}

int randomGenomeIndexExcept(SimulationRandom& random,
        int geneCount,
        int firstExcluded,
        int secondExcluded,
        int thirdExcluded,
        int fourthExcluded = -1) {
    if (geneCount <= 0) {
        return -1;
    }
    int index = firstExcluded;
    for (int attempt = 0; attempt < 64; ++attempt) {
        index = random.nextInt(geneCount);
        if (index != firstExcluded && index != secondExcluded
                && index != thirdExcluded && index != fourthExcluded) {
            return index;
        }
    }
    std::array<int, cellularity::MAX_SUPPORTED_GENES_PER_GENOME> candidates{};
    int candidateCount = 0;
    for (int candidate = 0; candidate < geneCount; ++candidate) {
        if (candidate != firstExcluded && candidate != secondExcluded
                && candidate != thirdExcluded && candidate != fourthExcluded) {
            candidates[static_cast<std::size_t>(candidateCount++)] = candidate;
        }
    }
    return candidateCount == 0
            ? -1
            : candidates[static_cast<std::size_t>(random.nextInt(candidateCount))];
}

int randomLocalGenomeIndexExcept(SimulationRandom& random,
        int geneCount,
        int centerIndex,
        int excludedIndex) {
    constexpr int LOCAL_WINDOW = 6;
    if (geneCount <= 1) {
        return -1;
    }
    int index = excludedIndex;
    for (int attempt = 0; attempt < 64; ++attempt) {
        int choice = random.nextInt(4);
        if (choice == 0) {
            index = 0;
        } else if (choice == 1) {
            index = centerIndex;
        } else {
            int offset = 1 + random.nextInt(LOCAL_WINDOW);
            if (choice == 2) {
                index = std::max(0, centerIndex - offset);
            } else {
                index = std::min(geneCount - 1, centerIndex + offset);
            }
        }
        if (index != excludedIndex) {
            return index;
        }
    }
    for (int candidate = 0; candidate < geneCount; ++candidate) {
        if (candidate != excludedIndex) {
            return candidate;
        }
    }
    return -1;
}

int randomGameteGeneIndex(
        cellularity::GenomeView genome,
        SimulationRandom& random) {
    int gameteGeneCount = 0;
    for (std::size_t geneIndex = 0; geneIndex < genome.geneCount(); geneIndex++) {
        if (genome.gene(geneIndex).cellType() == CellType::GAMETE) {
            gameteGeneCount++;
        }
    }

    if (gameteGeneCount == 0) {
        return random.nextInt(static_cast<int>(genome.geneCount()));
    }

    int selectedGamete = random.nextInt(gameteGeneCount);
    for (std::size_t geneIndex = 0; geneIndex < genome.geneCount(); geneIndex++) {
        if (genome.gene(geneIndex).cellType() != CellType::GAMETE) {
            continue;
        }
        if (selectedGamete == 0) {
            return static_cast<int>(geneIndex);
        }
        selectedGamete--;
    }
    return 0;
}

bool isGameteGeneIndex(cellularity::GenomeView genome, int geneIndex) {
    const auto index = cellularity::geneIndexFromBoundary(
            geneIndex, genome.geneCount());
    return index.has_value()
            && genome.gene(*index).cellType() == CellType::GAMETE;
}

void randomizeGameteCompatibilityTargets(cellularity::GenomeData& genome,
        SimulationRandom& random) {
    const cellularity::GenomeView view(genome.data(), genome.size());
    for (std::size_t geneIndex = 0; geneIndex < genome.size(); geneIndex++) {
        if (view.gene(geneIndex).cellType() == CellType::GAMETE) {
            cellularity::GeneBuilder(genome[geneIndex]).setVirusToGeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(randomGameteGeneIndex(view, random))));
        }
    }
}

void normalizeGameteCompatibilityTargets(cellularity::GenomeData& genome,
        SimulationRandom& random) {
    const cellularity::GenomeView view(genome.data(), genome.size());
    for (std::size_t geneIndex = 0; geneIndex < genome.size(); geneIndex++) {
        const cellularity::GeneView gene = view.gene(geneIndex);
        if (gene.cellType() != CellType::GAMETE) {
            continue;
        }
        if (!isGameteGeneIndex(view, gene.virusToGeneIndex().value())) {
            cellularity::GeneBuilder(genome[geneIndex]).setVirusToGeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(randomGameteGeneIndex(view, random))));
        }
    }
}

void markProducedSignalChannel(std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT>& usedSignals,
        int signalIndex) {
    if (signalIndex >= 0 && signalIndex < CellDataConstants::SIGNAL_SUBSTANCE_COUNT) {
        usedSignals[signalIndex] = true;
    }
}

std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT> collectProducedSignalChannels(
        cellularity::GenomeView genome) {
    std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT> usedSignals{};
    for (std::size_t geneIndex = 0; geneIndex < genome.geneCount(); geneIndex++) {
        const cellularity::GeneView gene = genome.gene(geneIndex);
        switch (gene.cellType()) {
            case CellType::SENSEOCYTE:
            case CellType::STEREOCYTE:
                markProducedSignalChannel(
                        usedSignals,
                        gene.integerValue(Gene::SMELL_OUT_INTEGER_INDEX));
                break;
            case CellType::NEUROCYTE:
                for (int outputIndex = 0; outputIndex < CellDataConstants::NEUROCYTE_OUTPUT_COUNT; outputIndex++) {
                    markProducedSignalChannel(
                            usedSignals,
                            gene.integerValue(
                                    Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX
                                    + outputIndex));
                }
                break;
            case CellType::SECROCYTE: {
                const int secreteType =
                        gene.integerValue(Gene::SECRETE_TYPE_INTEGER_INDEX);
                if (secreteType >= Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                        && secreteType < Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                        + CellDataConstants::SIGNAL_SUBSTANCE_COUNT) {
                    markProducedSignalChannel(
                            usedSignals,
                            secreteType - Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT);
                }
                if (secreteType >= Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                        + CellDataConstants::SIGNAL_SUBSTANCE_COUNT
                        && secreteType < Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                        + 2 * CellDataConstants::SIGNAL_SUBSTANCE_COUNT) {
                    markProducedSignalChannel(
                            usedSignals,
                            secreteType - Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                    - CellDataConstants::SIGNAL_SUBSTANCE_COUNT);
                }
                break;
            }
            default:
                break;
        }
    }
    return usedSignals;
}

int randomProducedSignalChannel(const std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT>& usedSignals,
        SimulationRandom& random) {
    int usedCount = 0;
    for (bool used : usedSignals) {
        usedCount += used ? 1 : 0;
    }
    if (usedCount == 0) {
        return -1;
    }

    int selected = random.nextInt(usedCount);
    for (int signalIndex = 0; signalIndex < CellDataConstants::SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
        if (!usedSignals[signalIndex]) {
            continue;
        }
        if (selected == 0) {
            return signalIndex;
        }
        selected--;
    }
    return 0;
}

int randomReceiverSignalInput(const std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT>& usedSignals,
        SimulationRandom& random) {
    if (random.nextInt(5) != 0) {
        int signalIndex = randomProducedSignalChannel(usedSignals, random);
        if (signalIndex >= 0) {
            return signalIndex;
        }
    }
    return random.nextInt(CellDataConstants::SIGNAL_SUBSTANCE_COUNT
            + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT);
}

void randomizeReceiverSignalInputs(cellularity::GenomeData& genome,
        SimulationRandom& random) {
    const auto usedSignals = collectProducedSignalChannels(
            cellularity::GenomeView(genome.data(), genome.size()));
    for (GeneData& gene : genome) {
        cellularity::GeneBuilder builder(gene);
        for (int valueIndex = 0;
                valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT;
                valueIndex++) {
            builder.setParametricSignalChannel(
                    valueIndex, randomReceiverSignalInput(usedSignals, random));
        }
    }
}

void addInternalSeedTreeCycle(GeneData* genome, int activeGeneCount, SimulationRandom& random) {
    if (activeGeneCount < 8) {
        return;
    }

    int cycleGeneIndex = 1 + random.nextInt(std::max(1, activeGeneCount / 2 - 1));
    const cellularity::GeneView cycleGene(genome[cycleGeneIndex]);
    const bool useChild2 = cycleGene.child2GeneIndex().value() < activeGeneCount
            && random.nextInt(2) == 0;
    const int displacedGeneIndex = useChild2
            ? cycleGene.child2GeneIndex().value()
            : cycleGene.child1GeneIndex().value();
    if (displacedGeneIndex >= activeGeneCount || displacedGeneIndex <= cycleGeneIndex) {
        return;
    }

    for (int leafGeneIndex = activeGeneCount - 1; leafGeneIndex >= activeGeneCount / 2; leafGeneIndex--) {
        if (leafGeneIndex == displacedGeneIndex || isSeedTreeDescendant(leafGeneIndex, displacedGeneIndex)) {
            continue;
        }
        cellularity::GeneBuilder cycleBuilder(genome[cycleGeneIndex]);
        const cellularity::GeneIndex cycleTarget(
                static_cast<cellularity::GeneIndex::Value>(
                        randomLocalSeedCycleGeneIndex(cycleGeneIndex, random)));
        if (useChild2) {
            cycleBuilder.setChild2GeneIndex(cycleTarget);
        } else {
            cycleBuilder.setChild1GeneIndex(cycleTarget);
        }
        cellularity::GeneBuilder(genome[leafGeneIndex]).setChild1GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(displacedGeneIndex)));
        return;
    }
}

// Copies one existing child gene into another genome slot and redirects the selected parent
// edge to that copy. This duplicates a reachable subtree entry point while preserving the old child.
void duplicateChildGeneMutation(cellularity::GenomeData& genome,
        int from,
        int to,
        SimulationRandom& random) {
    if (random.nextInt(2) == 0) {
        const int oldChild = cellularity::GeneView(genome[from])
                                     .child1GeneIndex()
                                     .value();
        cellularity::GeneBuilder(genome[to]).copyFrom(cellularity::GeneView(genome[oldChild]));
        cellularity::GeneBuilder(genome[from]).setChild1GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
    } else {
        const int oldChild = cellularity::GeneView(genome[from])
                                     .child2GeneIndex()
                                     .value();
        cellularity::GeneBuilder(genome[to]).copyFrom(cellularity::GeneView(genome[oldChild]));
        cellularity::GeneBuilder(genome[from]).setChild2GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
    }
}

// Inserts a new link-making node between a parent and one of its child edges. The inserted
// node points both children to the old child, so the split creates linked sibling cells.
void insertLinkedChildGeneMutation(cellularity::GenomeData& genome,
        int from,
        int to,
        SimulationRandom& random) {
    cellularity::GeneBuilder inserted(genome[to]);
    inserted.setMakesLink(true);
    const cellularity::GeneView parent(genome[from]);
    if (random.nextInt(2) == 0) {
        inserted.setChild1GeneIndex(parent.child1GeneIndex());
        inserted.setChild2GeneIndex(parent.child1GeneIndex());
        cellularity::GeneBuilder(genome[from]).setChild1GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
    } else {
        inserted.setChild1GeneIndex(parent.child2GeneIndex());
        inserted.setChild2GeneIndex(parent.child2GeneIndex());
        cellularity::GeneBuilder(genome[from]).setChild2GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
    }
}

// Replaces one parent edge with a new intermediate node whose two children are the old child
// and a random third gene. This creates a branch point without copying an existing gene.
void splitChildEdgeMutation(cellularity::GenomeData& genome,
        int from,
        int to,
        SimulationRandom& random) {
    if (random.nextInt(2) == 0) {
        const int old = cellularity::GeneView(genome[from]).child2GeneIndex().value();
        int m1 = randomGenomeIndexExcept(
                random, static_cast<int>(genome.size()), from, to, old);
        assert(m1 >= 0);
        cellularity::GeneBuilder(genome[from]).setChild2GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
        cellularity::GeneBuilder inserted(genome[to]);
        if (random.nextInt(2) == 0) {
            inserted.setChild1GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(m1)));
            inserted.setChild2GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(old)));
        } else {
            inserted.setChild1GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(old)));
            inserted.setChild2GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(m1)));
        }
    } else {
        const int old = cellularity::GeneView(genome[from]).child1GeneIndex().value();
        int m1 = randomGenomeIndexExcept(
                random, static_cast<int>(genome.size()), from, to, old);
        assert(m1 >= 0);
        cellularity::GeneBuilder(genome[from]).setChild1GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
        cellularity::GeneBuilder inserted(genome[to]);
        if (random.nextInt(2) == 0) {
            inserted.setChild1GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(m1)));
            inserted.setChild2GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(old)));
        } else {
            inserted.setChild1GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(old)));
            inserted.setChild2GeneIndex(cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(m1)));
        }
    }
}

// Removes one intermediate child node from a parent edge by redirecting that edge to one of
// the child's children. If the child only points to itself, the parent edge collapses to self.
void bypassChildGeneMutation(cellularity::GenomeData& genome,
        int from,
        SimulationRandom& random) {
    const bool useChild1 = random.nextInt(2) == 0;
    const cellularity::GeneView parent(genome[from]);
    const int oldChild = (useChild1
                    ? parent.child1GeneIndex()
                    : parent.child2GeneIndex())
                                 .value();
    const cellularity::GeneView childGene(genome[oldChild]);
    int replacement = (random.nextInt(2) == 0
                    ? childGene.child1GeneIndex()
                    : childGene.child2GeneIndex())
                              .value();
    if (replacement == oldChild) {
        replacement = (childGene.child1GeneIndex().value() == oldChild
                        ? childGene.child2GeneIndex()
                        : childGene.child1GeneIndex())
                              .value();
    }
    if (replacement == oldChild) {
        replacement = from;
    }
    cellularity::GeneBuilder parentBuilder(genome[from]);
    const cellularity::GeneIndex replacementIndex(
            static_cast<cellularity::GeneIndex::Value>(replacement));
    if (useChild1) {
        parentBuilder.setChild1GeneIndex(replacementIndex);
    } else {
        parentBuilder.setChild2GeneIndex(replacementIndex);
    }
}

// Retargets one child edge to a nearby genome index, self, or root. This creates local cycles
// and small topology edits without jumping uniformly across the whole genome.
void localEdgeRetargetMutation(cellularity::GenomeData& genome,
        int from,
        SimulationRandom& random) {
    const bool useChild1 = random.nextInt(2) == 0;
    const cellularity::GeneView parent(genome[from]);
    const int oldTarget = (useChild1
                    ? parent.child1GeneIndex()
                    : parent.child2GeneIndex())
                                  .value();
    const cellularity::GeneIndex replacement(
            static_cast<cellularity::GeneIndex::Value>(
                    randomLocalGenomeIndexExcept(
                            random,
                            static_cast<int>(genome.size()),
                            from,
                            oldTarget)));
    cellularity::GeneBuilder parentBuilder(genome[from]);
    if (useChild1) {
        parentBuilder.setChild1GeneIndex(replacement);
    } else {
        parentBuilder.setChild2GeneIndex(replacement);
    }
}

// Copies the parent gene itself into another genome slot and redirects one parent edge to the
// copy. This is a node duplication: the copy starts with the same behavior and can diverge later.
void cloneParentGeneMutation(cellularity::GenomeData& genome,
        int from,
        int to,
        SimulationRandom& random) {
    cellularity::GeneBuilder(genome[to]).copyFrom(cellularity::GeneView(genome[from]));
    if (random.nextInt(2) == 0) {
        cellularity::GeneBuilder(genome[from]).setChild1GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
    } else {
        cellularity::GeneBuilder(genome[from]).setChild2GeneIndex(cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(to)));
    }
}

bool copyGeneWithIndexRemap(
        GeneData& destination,
        cellularity::GeneView source,
        cellularity::GeneIndex oldOwner,
        cellularity::GeneIndex newOwner,
        const std::vector<cellularity::GeneIndex>& oldToNew,
        std::size_t newGeneCount) {
    const cellularity::GeneIndexRemapView remap{
            oldToNew.data(), oldToNew.size(), newGeneCount
    };
    if (!cellularity::isGeneIndexInBounds(oldOwner, remap.oldGeneCount)
            || !cellularity::isGeneIndexInBounds(newOwner, remap.newGeneCount)
            || !cellularity::isValidGeneIndexRemap(remap)) {
        return false;
    }

    const auto remapReference = [&](cellularity::GeneIndex reference) {
        const cellularity::GeneIndex resolved = cellularity::resolveGeneReference(
                reference, oldOwner, remap.oldGeneCount);
        return cellularity::remapGeneIndex(resolved, remap);
    };
    const cellularity::GeneIndex child1 = remapReference(source.child1GeneIndex());
    const cellularity::GeneIndex child2 = remapReference(source.child2GeneIndex());
    const cellularity::GeneIndex virusFrom = remapReference(
            source.virusFromGeneIndex());
    const cellularity::GeneIndex virusTo = remapReference(
            source.virusToGeneIndex());
    if (child1 == cellularity::INVALID_GENE_INDEX
            || child2 == cellularity::INVALID_GENE_INDEX
            || virusFrom == cellularity::INVALID_GENE_INDEX
            || virusTo == cellularity::INVALID_GENE_INDEX) {
        return false;
    }

    cellularity::GeneBuilder builder(destination);
    builder.copyFrom(source);
    builder.setChild1GeneIndex(child1 == newOwner
                    ? cellularity::SELF_GENE_INDEX
                    : child1);
    builder.setChild2GeneIndex(child2 == newOwner
                    ? cellularity::SELF_GENE_INDEX
                    : child2);
    builder.setVirusFromGeneIndex(
            virusFrom == newOwner ? cellularity::SELF_GENE_INDEX : virusFrom);
    builder.setVirusToGeneIndex(
            virusTo == newOwner ? cellularity::SELF_GENE_INDEX : virusTo);
    builder.setInitialCompatibilityFlag(false);
    return true;
}

bool insertEvolvedGene(cellularity::GenomeData& genome,
        int sourceIndex,
        Environment& environment) {
    if (genome.size() >= cellularity::MAX_SUPPORTED_GENES_PER_GENOME
            || sourceIndex < 0
            || static_cast<std::size_t>(sourceIndex) >= genome.size()) {
        return false;
    }
    const std::size_t oldCount = genome.size();
    const cellularity::GeneIndex sourceOwner(
            static_cast<cellularity::GeneIndex::Value>(sourceIndex));
    const cellularity::GeneIndex newOwner(
            static_cast<cellularity::GeneIndex::Value>(oldCount));
    std::vector<cellularity::GeneIndex> oldToNew(oldCount);
    for (std::size_t index = 0; index < oldCount; ++index) {
        oldToNew[index] = cellularity::GeneIndex(
                static_cast<cellularity::GeneIndex::Value>(index));
    }
    // A canonical self-reference in the source must become self in the copy.
    oldToNew[static_cast<std::size_t>(sourceIndex)] = newOwner;

    GeneData duplicate;
    if (!copyGeneWithIndexRemap(
                duplicate,
                cellularity::GeneView(genome[static_cast<std::size_t>(sourceIndex)]),
                sourceOwner,
                newOwner,
                oldToNew,
                oldCount + 1U)) {
        return false;
    }
    genome.push_back(duplicate);
    cellularity::GeneBuilder source(genome[static_cast<std::size_t>(sourceIndex)]);
    if (environment.mRandom.nextInt(2) == 0) {
        source.setChild1GeneIndex(newOwner);
    } else {
        source.setChild2GeneIndex(newOwner);
    }
    normalizeGameteCompatibilityTargets(genome, environment.mRandom);
    return true;
}

bool deleteEvolvedGene(cellularity::GenomeData& genome,
        int deletedIndex,
        cellularity::GeneIndex& activeGeneIndex,
        Environment& environment) {
    if (genome.size() <= 1
            || deletedIndex < 0
            || static_cast<std::size_t>(deletedIndex) >= genome.size()
            || !cellularity::isGeneIndexInBounds(activeGeneIndex, genome.size())) {
        return false;
    }
    const std::size_t oldCount = genome.size();
    const cellularity::GeneIndex deleted(
            static_cast<cellularity::GeneIndex::Value>(deletedIndex));
    const cellularity::GeneView deletedGene(
            genome[static_cast<std::size_t>(deletedIndex)]);
    cellularity::GeneIndex replacement = cellularity::resolveGeneReference(
            environment.mRandom.nextInt(2) == 0
                    ? deletedGene.child1GeneIndex()
                    : deletedGene.child2GeneIndex(),
            deleted,
            oldCount);
    if (replacement == deleted || replacement == cellularity::INVALID_GENE_INDEX) {
        replacement = cellularity::resolveGeneReference(
                deletedGene.child1GeneIndex(), deleted, oldCount);
    }
    if (replacement == deleted || replacement == cellularity::INVALID_GENE_INDEX) {
        replacement = cellularity::resolveGeneReference(
                deletedGene.child2GeneIndex(), deleted, oldCount);
    }
    if (replacement == deleted || replacement == cellularity::INVALID_GENE_INDEX) {
        replacement = cellularity::GeneIndex(
                static_cast<cellularity::GeneIndex::Value>(
                        (static_cast<std::size_t>(deletedIndex) + 1U) % oldCount));
    }

    const auto oldMetadata =
            cellularity::extractGenomeMetadataFromCompatibilityFlags(
                    genome.data(), genome.size());
    if (!oldMetadata.has_value()) {
        return false;
    }
    const auto afterDeletion = [&](cellularity::GeneIndex oldIndex) {
        const std::size_t raw = oldIndex.value();
        return cellularity::GeneIndex(
                static_cast<cellularity::GeneIndex::Value>(
                        raw < static_cast<std::size_t>(deletedIndex)
                                ? raw
                                : raw - 1U));
    };
    const cellularity::GeneIndex replacementAfterDeletion =
            afterDeletion(replacement);
    std::vector<cellularity::GeneIndex> oldToNew(oldCount);
    for (std::size_t index = 0; index < oldCount; ++index) {
        const cellularity::GeneIndex old(
                static_cast<cellularity::GeneIndex::Value>(index));
        oldToNew[index] = old == deleted
                ? replacementAfterDeletion
                : afterDeletion(old);
    }

    cellularity::GenomeData result(oldCount - 1U);
    for (std::size_t oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
        if (oldIndex == static_cast<std::size_t>(deletedIndex)) {
            continue;
        }
        const cellularity::GeneIndex oldOwner(
                static_cast<cellularity::GeneIndex::Value>(oldIndex));
        const cellularity::GeneIndex newOwner = oldToNew[oldIndex];
        if (!copyGeneWithIndexRemap(
                    result[newOwner.value()],
                    cellularity::GeneView(genome[oldIndex]),
                    oldOwner,
                    newOwner,
                    oldToNew,
                    oldCount - 1U)) {
            return false;
        }
    }
    const cellularity::GenomeMetadata newMetadata{
            oldToNew[oldMetadata->initialGeneIndex.value()]
    };
    if (!cellularity::applyGenomeMetadataToCompatibilityFlags(
                result.data(), result.size(), newMetadata)) {
        return false;
    }
    activeGeneIndex = oldToNew[activeGeneIndex.value()];
    genome = std::move(result);
    normalizeGameteCompatibilityTargets(genome, environment.mRandom);
    return true;
}

bool mutateStructuralGenomeByType(cellularity::GenomeData& genome,
        int mutationType,
        Environment& environment) {
    constexpr std::size_t MIN_STRUCTURAL_GENOME_SIZE = 4;
    if (genome.size() < MIN_STRUCTURAL_GENOME_SIZE) {
        return false;
    }
    const int geneCount = static_cast<int>(genome.size());
    int from = environment.mRandom.nextInt(geneCount);
    const cellularity::GeneView source(genome[from]);
    int to = randomGenomeIndexExcept(
            environment.mRandom,
            geneCount,
            from,
            source.child1GeneIndex().value(),
            source.child2GeneIndex().value());
    if (to < 0) {
        return false;
    }

    if (mutationType == 0) {
        duplicateChildGeneMutation(genome, from, to, environment.mRandom);
    }
    if (mutationType == 1) {
        insertLinkedChildGeneMutation(genome, from, to, environment.mRandom);
    }
    if (mutationType == 2) {
        splitChildEdgeMutation(genome, from, to, environment.mRandom);
    }
    if (mutationType == 3) {
        bypassChildGeneMutation(genome, from, environment.mRandom);
    }
    if (mutationType == 4) {
        localEdgeRetargetMutation(genome, from, environment.mRandom);
    }
    if (mutationType == 5) {
        cloneParentGeneMutation(genome, from, to, environment.mRandom);
    }
    normalizeGameteCompatibilityTargets(genome, environment.mRandom);
    return true;
}

bool isAllowedGenomeMutationWeightIndex(cellularity::GenomeView genome,
        int geneIndex,
        int mutationWeightIndex,
        const Environment& environment) {
    if (mutationWeightIndex < 0 || mutationWeightIndex >= Environment::MUTATION_WEIGHT_COUNT) {
        return false;
    }
    if (mutationWeightIndex < Environment::MUTATION_POINT_WEIGHT_COUNT) {
        return Gene::isPointMutationWeightAllowed(mutationWeightIndex,
                environment.IsOnlyTweakMutations,
                genome.gene(
                              static_cast<std::size_t>(geneIndex))
                        .cellType());
    }
    if (mutationWeightIndex < Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET + Environment::MUTATION_STRUCTURAL_WEIGHT_COUNT) {
        constexpr std::size_t MIN_STRUCTURAL_GENOME_SIZE = 4;
        return !environment.IsOnlyTweakMutations
                && genome.geneCount() >= MIN_STRUCTURAL_GENOME_SIZE;
    }
    if (mutationWeightIndex < Environment::MUTATION_GENOME_LENGTH_WEIGHT_OFFSET
                    + Environment::MUTATION_GENOME_LENGTH_WEIGHT_COUNT) {
        if (environment.IsOnlyTweakMutations) {
            return false;
        }
        return mutationWeightIndex == Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX
                ? genome.geneCount() < cellularity::MAX_SUPPORTED_GENES_PER_GENOME
                : genome.geneCount() > 1;
    }
    return false;
}

struct SelectedGenomeMutation {
    int geneIndex = -1;
    int mutationWeightIndex = -1;
};

SelectedGenomeMutation selectGenomeMutation(cellularity::GenomeView genome,
        Environment& environment) {
    int geneIndex = environment.mRandom.nextInt(
            static_cast<int>(genome.geneCount()));
    double totalWeight = 0.0;
    for (int mutationWeightIndex = 0; mutationWeightIndex < Environment::MUTATION_WEIGHT_COUNT; mutationWeightIndex++) {
        if (!isAllowedGenomeMutationWeightIndex(genome, geneIndex, mutationWeightIndex, environment)) {
            continue;
        }
        double weight = environment.mMutationWeights[mutationWeightIndex];
        if (std::isfinite(weight) && weight > 0.0) {
            totalWeight += weight;
        }
    }
    if (totalWeight <= 0.0) {
        return {};
    }

    double selectedWeight = environment.mRandom.nextUniform() * totalWeight;
    for (int mutationWeightIndex = 0; mutationWeightIndex < Environment::MUTATION_WEIGHT_COUNT; mutationWeightIndex++) {
        if (!isAllowedGenomeMutationWeightIndex(genome, geneIndex, mutationWeightIndex, environment)) {
            continue;
        }
        double weight = environment.mMutationWeights[mutationWeightIndex];
        if (!std::isfinite(weight) || weight <= 0.0) {
            continue;
        }
        selectedWeight -= weight;
        if (selectedWeight < 0.0) {
            return {geneIndex, mutationWeightIndex};
        }
    }
    return {};
}

bool applySelectedGenomeMutation(cellularity::GenomeData& genome,
        const SelectedGenomeMutation& mutation,
        cellularity::GeneIndex& activeGeneIndex,
        Environment& environment) {
    if (mutation.geneIndex < 0 || mutation.mutationWeightIndex < 0) {
        return false;
    }
    if (mutation.mutationWeightIndex < Environment::MUTATION_POINT_WEIGHT_COUNT) {
        const bool mutated = cellularity::GeneBuilder(genome[mutation.geneIndex])
                                     .mutatePointByWeightIndex(
                                             environment,
                                             mutation.mutationWeightIndex,
                                             cellularity::GenomeView(
                                                     genome.data(),
                                                     genome.size()));
        if (mutated) {
            normalizeGameteCompatibilityTargets(genome, environment.mRandom);
        }
        return mutated;
    }

    if (mutation.mutationWeightIndex < Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET + Environment::MUTATION_STRUCTURAL_WEIGHT_COUNT) {
        return mutateStructuralGenomeByType(
                genome,
                mutation.mutationWeightIndex
                        - Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET,
                environment);
    }

    if (mutation.mutationWeightIndex == Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX) {
        return insertEvolvedGene(genome, mutation.geneIndex, environment);
    }
    if (mutation.mutationWeightIndex == Environment::MUTATION_DELETE_GENE_WEIGHT_INDEX) {
        return deleteEvolvedGene(
                genome, mutation.geneIndex, activeGeneIndex, environment);
    }

    return false;
}

bool recordDeferredGenomeMutation(cellularity::GenomeView genome,
        Environment& environment) {
    SelectedGenomeMutation mutation = selectGenomeMutation(genome, environment);
    return mutation.geneIndex >= 0 && mutation.mutationWeightIndex >= 0;
}

constexpr std::array<double, CellTypes::length> CELL_METABOLISM_DRAIN{
        0.1, // PHAGOCYTE
        0.1, // FLAGELLOCYTE
        0.25, // PHOTOCYTE
        0.1, // DEVOROCYTE
        0.005, // LIPOCYTE
        0.01, // KERATINOCYTE
        0.02, // BUOYOCYTE
        0.01, // GLUEOCYTE
        0.2, // VIROCYTE
        0.02, // NITROCYTE
        0.01, // STEREOCYTE
        0.01, // SENSEOCYTE
        0.02, // MYOCYTE
        0.02, // NEUROCYTE
        0.03, // SECROCYTE
        0.02, // STEMOCYTE
        0.005, // GAMETE
        0.005, // CILIOCYTE
};

double getCellMetabolismDrain(CellType cellType) {
    return CELL_METABOLISM_DRAIN[CellTypes::ordinal(cellType)];
}

bool isCellGrowsInPreview(CellType cellType) {
    return cellType == CellType::PHAGOCYTE
            || cellType == CellType::PHOTOCYTE
            || cellType == CellType::DEVOROCYTE
            || cellType == CellType::GLUEOCYTE;
}

static_assert(CellDataConstants::SIGNAL_SUBSTANCE_COUNT == 4,
        "Signal kernels rely on one contiguous four-float block");

#if CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
using SignalBlock = float __attribute__((vector_size(
        sizeof(float) * CellDataConstants::SIGNAL_SUBSTANCE_COUNT)));

static_assert(sizeof(SignalBlock)
                == sizeof(float) * CellDataConstants::SIGNAL_SUBSTANCE_COUNT,
        "Signal block must contain exactly four floats");

[[nodiscard]] SignalBlock loadSignalBlock(const float* values) noexcept {
    SignalBlock block{};
    std::memcpy(&block, values, sizeof(block));
    return block;
}

void storeSignalBlock(float* values, SignalBlock block) noexcept {
    std::memcpy(values, &block, sizeof(block));
}

[[nodiscard]] SignalBlock splatSignalBlock(float value) noexcept {
    return {value, value, value, value};
}
#endif

float evaluateParametricValue(const CellData& cellData,
        int cellIndex,
        cellularity::GeneParametricValueView parametricValue,
        int valueIndex,
        int readOffset) {
    float result;
    const int formulaType = parametricValue.formulaType();

    if (formulaType == 0) {
        result = parametricValue.variableB();
    } else {
        float in = 0.0f;
        const int signalChannel = parametricValue.signalChannel();
        if (signalChannel < CellData::SIGNAL_SUBSTANCE_COUNT) {
            in = cellData.mSignalSubstance[cellIndex][readOffset + signalChannel];
        } else {
            switch (signalChannel) {
                case CellData::SIGNAL_SUBSTANCE_COUNT:
                    in = static_cast<float>(
                            cellData.mMass[cellIndex] / CellData::MAX_MASS);
                    break;
                case CellData::SIGNAL_SUBSTANCE_COUNT + 1:
                    in = static_cast<float>(
                            cellData.mAge[cellIndex]
                            * (1.0f / CellData::MAX_AGE));
                    break;
                case CellData::SIGNAL_SUBSTANCE_COUNT + 2:
                    in = static_cast<float>(cellData.mNitrates[cellIndex]);
                    break;
                case CellData::SIGNAL_SUBSTANCE_COUNT + 3:
                    in = static_cast<float>(
                                 cellData.mTopology.linkCount(cellIndex))
                            / CellData::MAX_NEIGHBORS;
                    break;
                default:
                    break;
            }
        }
        if (formulaType == 1) {
            result = in * parametricValue.variableA()
                    + parametricValue.variableB();
        } else if (formulaType == 2) {
            result = in < parametricValue.variableC()
                    ? parametricValue.variableA()
                    : parametricValue.variableB();
        } else {
            result = parametricValue.variableA()
                    * std::sin(
                            in * parametricValue.variableC()
                            - parametricValue.variableB());
        }
    }
    result = std::max(-1.0f, std::min(1.0f, result));
    return Gene::VALUE_MIN[valueIndex]
            + (Gene::VALUE_MAX[valueIndex] - Gene::VALUE_MIN[valueIndex])
            * 0.5f * (result + 1.0f);
}

void evaluateNeurocyteSignalOutputs(CellData& cellData,
        int cellIndex,
        int readOffset,
        SimulationWorldProfile* profile) {
    CELLULARITY_PROFILE_IF(profile) {
        profile->signalNeurocyteEvaluations++;
    }
    const cellularity::GeneView gene = cellData.getActiveGeneView(cellIndex);
    auto& signalOutput = cellData.mSignalSubstanceOut[cellIndex];
    signalOutput.fill(0.0f);
    for (int outputIndex = 0;
            outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT;
            ++outputIndex) {
        const int valueIndex =
                Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + outputIndex;
        float output = evaluateParametricValue(
                cellData,
                cellIndex,
                gene.parametricValue(valueIndex),
                valueIndex,
                readOffset);
        if (output < -CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT) {
            output = -CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT;
        }
        if (output > CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT) {
            output = CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT;
        }
        signalOutput[gene.integerValue(
                Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + outputIndex)] += output;
    }
}

void initializeSignalWriteBlock(CellData& cellData,
        int cellIndex,
        int readOffset,
        int writeOffset,
        float localDeltaTime,
        bool isLastPass) {
    auto& signals = cellData.mSignalSubstance[cellIndex];
    auto& signalOutput = cellData.mSignalSubstanceOut[cellIndex];
#if CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
    const SignalBlock readValues = loadSignalBlock(signals.data() + readOffset);
    const SignalBlock outputValues = loadSignalBlock(signalOutput.data());
    const SignalBlock updatedValues =
            readValues
            + (outputValues
                      - splatSignalBlock(CellData::SIGNAL_SUBSTANCE_DECAY)
                              * readValues)
                    * splatSignalBlock(localDeltaTime);
    storeSignalBlock(signals.data() + writeOffset, updatedValues);
    if (!isLastPass) {
        return;
    }
    storeSignalBlock(
            cellData.mNewSignalSubstance[cellIndex].data(), outputValues);
    storeSignalBlock(signalOutput.data(), splatSignalBlock(0.0f));
#else
    for (int channel = 0; channel < CellData::SIGNAL_SUBSTANCE_COUNT; ++channel) {
        const float readValue = signals[readOffset + channel];
        signals[writeOffset + channel] =
                readValue
                + (signalOutput[channel]
                          - CellData::SIGNAL_SUBSTANCE_DECAY * readValue)
                        * localDeltaTime;
    }
    if (!isLastPass) {
        return;
    }
    for (int channel = 0; channel < CellData::SIGNAL_SUBSTANCE_COUNT; ++channel) {
        cellData.mNewSignalSubstance[cellIndex][channel] = signalOutput[channel];
        signalOutput[channel] = 0.0f;
    }
#endif
}

template <bool OwnerIsCellB, typename Value>
[[nodiscard]] Value applySignalEndpointContribution(
        Value ownerWrite,
        Value firstRead,
        Value secondRead,
        Value flowStep) {
    if constexpr (OwnerIsCellB) {
        return ownerWrite + (firstRead - secondRead) * flowStep;
    }
    return ownerWrite + (secondRead - firstRead) * flowStep;
}

template <bool OwnerIsCellB>
void addSignalEndpointFlow(
        std::array<float, 2 * CellData::SIGNAL_SUBSTANCE_COUNT>& ownerSignals,
        const std::array<float, 2 * CellData::SIGNAL_SUBSTANCE_COUNT>& firstSignals,
        const std::array<float, 2 * CellData::SIGNAL_SUBSTANCE_COUNT>& secondSignals,
        int readOffset,
        int writeOffset,
        float flowStep) {
#if CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
    const SignalBlock firstRead =
            loadSignalBlock(firstSignals.data() + readOffset);
    const SignalBlock secondRead =
            loadSignalBlock(secondSignals.data() + readOffset);
    const SignalBlock ownerWrite =
            loadSignalBlock(ownerSignals.data() + writeOffset);
    storeSignalBlock(
            ownerSignals.data() + writeOffset,
            applySignalEndpointContribution<OwnerIsCellB>(
                    ownerWrite,
                    firstRead,
                    secondRead,
                    splatSignalBlock(flowStep)));
#else
    for (int channel = 0; channel < CellData::SIGNAL_SUBSTANCE_COUNT; ++channel) {
        const float firstRead = firstSignals[readOffset + channel];
        const float secondRead = secondSignals[readOffset + channel];
        ownerSignals[writeOffset + channel] =
                applySignalEndpointContribution<OwnerIsCellB>(
                        ownerSignals[writeOffset + channel],
                        firstRead,
                        secondRead,
                        flowStep);
    }
#endif
}

void addSignalEdgeFlow(
        std::array<float, 2 * CellData::SIGNAL_SUBSTANCE_COUNT>& firstSignals,
        std::array<float, 2 * CellData::SIGNAL_SUBSTANCE_COUNT>& secondSignals,
        int readOffset,
        int writeOffset,
        float flowStep) {
#if CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
    const SignalBlock firstRead =
            loadSignalBlock(firstSignals.data() + readOffset);
    const SignalBlock secondRead =
            loadSignalBlock(secondSignals.data() + readOffset);
    const SignalBlock flow = splatSignalBlock(flowStep);
    const SignalBlock firstWrite =
            loadSignalBlock(firstSignals.data() + writeOffset);
    const SignalBlock secondWrite =
            loadSignalBlock(secondSignals.data() + writeOffset);
    storeSignalBlock(
            firstSignals.data() + writeOffset,
            applySignalEndpointContribution<false>(
                    firstWrite, firstRead, secondRead, flow));
    storeSignalBlock(
            secondSignals.data() + writeOffset,
            applySignalEndpointContribution<true>(
                    secondWrite, firstRead, secondRead, flow));
#else
    for (int channel = 0; channel < CellData::SIGNAL_SUBSTANCE_COUNT; ++channel) {
        const float firstRead = firstSignals[readOffset + channel];
        const float secondRead = secondSignals[readOffset + channel];
        firstSignals[writeOffset + channel] =
                applySignalEndpointContribution<false>(
                        firstSignals[writeOffset + channel],
                        firstRead,
                        secondRead,
                        flowStep);
        secondSignals[writeOffset + channel] =
                applySignalEndpointContribution<true>(
                        secondSignals[writeOffset + channel],
                        firstRead,
                        secondRead,
                        flowStep);
    }
#endif
}

void clampSignalWriteBlock(CellData& cellData,
        int cellIndex,
        int writeOffset,
        SimulationWorldProfile* profile) {
    auto& signals = cellData.mSignalSubstance[cellIndex];
    for (int channel = 0; channel < CellData::SIGNAL_SUBSTANCE_COUNT; ++channel) {
        float& value = signals[writeOffset + channel];
        if (value < -1.0f) {
            value = -1.0f;
            CELLULARITY_PROFILE_IF(profile) {
                profile->signalClampLowWrites++;
            }
        }
        if (value > 1.0f) {
            value = 1.0f;
            CELLULARITY_PROFILE_IF(profile) {
                profile->signalClampHighWrites++;
            }
        }
    }
}

template <bool HasNeurocytes, bool HasLipocytes>
void runGlobalEdgeSignalKernel(CellData& cellData,
        float localDeltaTime,
        float flowStep,
        SimulationWorldProfile* profile) {
    int writeOffset = CellData::SIGNAL_SUBSTANCE_COUNT;
    int readOffset = 0;
    const CellTopology::EdgeView edges = cellData.mTopology.edgeView();

    for (int pass = 0; pass < SimulationWorld::SIGNAL_PASSES; ++pass) {
        const bool isLastPass = pass == SimulationWorld::SIGNAL_PASSES - 1;
        {
            CELLULARITY_PROFILE_COARSE_REGION(profile, signalCellPhaseNanos);
            CELLULARITY_PROFILE_IF(profile) {
                profile->signalCellPhaseVisits += cellData.mCellCount;
            }
            for (int cellIndex = 0; cellIndex < cellData.mCellCount; ++cellIndex) {
                if constexpr (HasNeurocytes) {
                    if (cellData.mActiveCellType[cellIndex] == CellType::NEUROCYTE) {
                        evaluateNeurocyteSignalOutputs(
                                cellData, cellIndex, readOffset, profile);
                    }
                }
                initializeSignalWriteBlock(
                        cellData,
                        cellIndex,
                        readOffset,
                        writeOffset,
                        localDeltaTime,
                        isLastPass);
            }
        }

        {
            CELLULARITY_PROFILE_COARSE_REGION(profile, signalEdgePhaseNanos);
            for (int edgeIndex = 0; edgeIndex < edges.count; ++edgeIndex) {
                const int firstCellIndex = edges.cellA[edgeIndex];
                const int secondCellIndex = edges.cellB[edgeIndex];
                CELLULARITY_PROFILE_IF(profile) {
                    profile->signalTopologyEdgeIterations++;
                }
                if constexpr (HasLipocytes) {
                    if (cellData.mActiveCellType[firstCellIndex] == CellType::LIPOCYTE
                            || cellData.mActiveCellType[secondCellIndex]
                                    == CellType::LIPOCYTE) {
                        CELLULARITY_PROFILE_IF(profile) {
                            profile->signalLipocyteLinkSkips++;
                        }
                        continue;
                    }
                }
                CELLULARITY_PROFILE_IF(profile) {
                    profile->signalTopologyEndpointUpdates += 2;
                    profile->signalLinkIterations += 2;
                }
                auto& firstSignals = cellData.mSignalSubstance[firstCellIndex];
                auto& secondSignals = cellData.mSignalSubstance[secondCellIndex];
                addSignalEdgeFlow(
                        firstSignals,
                        secondSignals,
                        readOffset,
                        writeOffset,
                        flowStep);
            }
        }

        {
            CELLULARITY_PROFILE_COARSE_REGION(profile, signalClampPhaseNanos);
            CELLULARITY_PROFILE_IF(profile) {
                profile->signalClampEvaluations +=
                        static_cast<long long>(cellData.mCellCount)
                        * CellData::SIGNAL_SUBSTANCE_COUNT;
            }
            for (int cellIndex = 0; cellIndex < cellData.mCellCount; ++cellIndex) {
                clampSignalWriteBlock(cellData, cellIndex, writeOffset, profile);
            }
        }
        std::swap(readOffset, writeOffset);
    }
}

#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
template <bool HasNeurocytes, bool HasLipocytes>
void runCellOwnedCsrSignalKernel(CellData& cellData,
        CellSignalAdjacency::View adjacency,
        float localDeltaTime,
        float flowStep,
        SimulationWorldProfile* profile) {
    int writeOffset = CellData::SIGNAL_SUBSTANCE_COUNT;
    int readOffset = 0;
    const CellTopology::EdgeView edges = cellData.mTopology.edgeView();

    for (int pass = 0; pass < SimulationWorld::SIGNAL_PASSES; ++pass) {
        const bool isLastPass = pass == SimulationWorld::SIGNAL_PASSES - 1;
        CELLULARITY_PROFILE_IF(profile) {
            profile->signalTopologyEdgeIterations += edges.count;
        }
        {
            CELLULARITY_PROFILE_COARSE_REGION(profile, signalFusedCellPhaseNanos);
            CELLULARITY_PROFILE_IF(profile) {
                profile->signalCellPhaseVisits += cellData.mCellCount;
                profile->signalClampEvaluations +=
                        static_cast<long long>(cellData.mCellCount)
                        * CellData::SIGNAL_SUBSTANCE_COUNT;
            }
            for (int cellIndex = 0; cellIndex < cellData.mCellCount; ++cellIndex) {
                if constexpr (HasNeurocytes) {
                    if (cellData.mActiveCellType[cellIndex] == CellType::NEUROCYTE) {
                        evaluateNeurocyteSignalOutputs(
                                cellData, cellIndex, readOffset, profile);
                    }
                }
                initializeSignalWriteBlock(
                        cellData,
                        cellIndex,
                        readOffset,
                        writeOffset,
                        localDeltaTime,
                        isLastPass);

                const std::uint32_t referenceBegin = adjacency.offsets[cellIndex];
                const std::uint32_t referenceEnd = adjacency.offsets[cellIndex + 1];
                for (std::uint32_t referenceIndex = referenceBegin;
                        referenceIndex < referenceEnd;
                        ++referenceIndex) {
                    const CellSignalAdjacency::IncidentReference reference =
                            adjacency.references[referenceIndex];
                    const int edgeIndex = CellSignalAdjacency::edgeIndex(reference);
                    const bool ownerIsCellB =
                            CellSignalAdjacency::ownerIsCellB(reference);
                    const int firstCellIndex = edges.cellA[edgeIndex];
                    const int secondCellIndex = edges.cellB[edgeIndex];
                    assert(ownerIsCellB
                                    ? secondCellIndex == cellIndex
                                    : firstCellIndex == cellIndex);
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->signalCsrReferenceVisits++;
                    }
                    if constexpr (HasLipocytes) {
                        if (cellData.mActiveCellType[firstCellIndex] == CellType::LIPOCYTE
                                || cellData.mActiveCellType[secondCellIndex]
                                        == CellType::LIPOCYTE) {
                            CELLULARITY_PROFILE_IF(profile) {
                                if (ownerIsCellB) {
                                    profile->signalLipocyteLinkSkips++;
                                }
                            }
                            continue;
                        }
                    }
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->signalTopologyEndpointUpdates++;
                        profile->signalLinkIterations++;
                    }
                    auto& ownerSignals = cellData.mSignalSubstance[cellIndex];
                    const auto& firstSignals =
                            cellData.mSignalSubstance[firstCellIndex];
                    const auto& secondSignals =
                            cellData.mSignalSubstance[secondCellIndex];
                    if (ownerIsCellB) {
                        addSignalEndpointFlow<true>(
                                ownerSignals,
                                firstSignals,
                                secondSignals,
                                readOffset,
                                writeOffset,
                                flowStep);
                    } else {
                        addSignalEndpointFlow<false>(
                                ownerSignals,
                                firstSignals,
                                secondSignals,
                                readOffset,
                                writeOffset,
                                flowStep);
                    }
                }
                clampSignalWriteBlock(cellData, cellIndex, writeOffset, profile);
            }
        }
        std::swap(readOffset, writeOffset);
    }
}
#endif

} // namespace

CellData::CellData(int maxCellCount)
        : mGenePool(),
          mGenomePool(mGenePool),
          mGenomeIds(maxCellCount, cellularity::INVALID_GENOME_ID),
          mGeneIndices(maxCellCount, cellularity::INVALID_GENE_INDEX),
          mActiveGeneIds(maxCellCount, cellularity::INVALID_GENE_ID),
          mActiveGeneData(maxCellCount, nullptr),
          mActiveGeneInitialFlags(maxCellCount, 0),
          MAX_CELL_COUNT(maxCellCount),
          RANDOM_CELLS_MAINTAIN_COUNT(maxCellCount / 10),
          mPositionX(maxCellCount),
          mPositionY(maxCellCount),
          mPositionZ(maxCellCount),
          mRadius(maxCellCount),
          mRadiusSq(maxCellCount),
          mVelocityX(maxCellCount),
          mVelocityY(maxCellCount),
          mForceX(maxCellCount),
          mForceY(maxCellCount),
          mForceZ(maxCellCount),
          mTorque(maxCellCount),
          mAngle(maxCellCount),
          mCachedAngle(maxCellCount, std::numeric_limits<double>::quiet_NaN()),
          mCachedAngleCos(maxCellCount, 1.0),
          mCachedAngleSin(maxCellCount, 0.0),
          mAngleCytoplasm(maxCellCount),
          mAngularVelocity(maxCellCount),
          mAngularVelocityCytoplasm(maxCellCount),
          mAge(maxCellCount),
          mNitrates(maxCellCount),
          mSwimAnimationTime(maxCellCount),
          mSwimAnimationTime2(maxCellCount),
          mHairRotation(maxCellCount),
          hairX(maxCellCount),
          hairY(maxCellCount),
          mMass(maxCellCount),
          mIsDead(maxCellCount),
          mIsMirrored(maxCellCount),
          mIsStuck(maxCellCount),
          mOriginTag(maxCellCount),
          mOrganismIds(maxCellCount, kUnknownOrganismId),
          mParentOrganismIds(maxCellCount, kUnknownOrganismId),
          mSelectTag(maxCellCount),
          mMutationCount(maxCellCount),
          mPendingMutationCount(maxCellCount),
          mTopology(maxCellCount),
          mColor(maxCellCount),
          mGridIndex1(maxCellCount),
          mSeldomCounter(maxCellCount),
          mOldLinkCount(maxCellCount),
          mNewMass(maxCellCount),
          mNewNitrates(maxCellCount),
          mMetabolismScaledMass(maxCellCount),
          mMetabolismMassFlowPriority(maxCellCount),
          mLipids(maxCellCount),
          mTelomeresCount(maxCellCount),
          mActiveCellType(maxCellCount),
          mActiveCellCollisionElasticity(maxCellCount),
          mSignalSubstance(maxCellCount),
          mNewSignalSubstance(maxCellCount),
          mSignalSubstanceOut(maxCellCount),
          mToxin(maxCellCount),
          mToxication(maxCellCount),
          mNewToxin(maxCellCount),
          mMyocyteContraction(maxCellCount),
          mMyocyteBending(maxCellCount),
          mMyocyteLifting(maxCellCount),
          mFertilizationTarget(maxCellCount),
          mCiliocyteInteractionCellIndex1(
                  static_cast<std::size_t>(maxCellCount)
                  * MAX_CILIOCYTE_INTERACTIONS_PER_CELL),
          mCiliocyteInteractionCellIndex2(
                  static_cast<std::size_t>(maxCellCount)
                  * MAX_CILIOCYTE_INTERACTIONS_PER_CELL),
          mCellContactBuffer(maxCellCount),
          mCellCollisionIntentBuffer(maxCellCount),
          mRuntimeIds(maxCellCount, kInvalidCellRuntimeId),
          mOrganismTraversalMarks(maxCellCount, 0),
          mOrganismTraversalQueue(maxCellCount)
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
          ,
          mSignalAdjacency(maxCellCount)
#endif
{
}

CellData::~CellData() {
    for (int cellIndex = 0; cellIndex < mCellCount; ++cellIndex) {
        releaseGenomeHandle(cellIndex);
    }
    if (mDefaultGenomeId != cellularity::INVALID_GENOME_ID) {
        const cellularity::GenomePoolError released =
                mGenomePool.release(mDefaultGenomeId);
        (void)released;
        assert(released == cellularity::GenomePoolError::None);
        mDefaultGenomeId = cellularity::INVALID_GENOME_ID;
    }
}

CellData::CellContactBuffer::CellContactBuffer(int maxCellCount)
        : collidedCellIndex(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          geometry(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          cellContactCount(maxCellCount) {
    clear(maxCellCount);
}

CellData::CellContactBuffer::CellContactBuffer(CellContactBuffer&& other) noexcept
        : collidedCellIndex(std::move(other.collidedCellIndex)),
          geometry(std::move(other.geometry)),
          cellContactCount(std::move(other.cellContactCount)) {
}

CellData::CellContactBuffer& CellData::CellContactBuffer::operator=(CellContactBuffer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    collidedCellIndex = std::move(other.collidedCellIndex);
    geometry = std::move(other.geometry);
    cellContactCount = std::move(other.cellContactCount);
    return *this;
}

void CellData::CellContactBuffer::clear(int cellCount) {
    std::fill(cellContactCount.begin(), cellContactCount.begin() + cellCount, 0);
}

void CellData::CellContactBuffer::clearCell(int cellIndex) {
    cellContactCount[cellIndex] = 0;
}

int CellData::CellContactBuffer::add(int currentCellIndexValue,
        int collidedCellIndexValue,
        double distanceX,
        double distanceY,
        double distanceSq) {
    int currentCount = cellContactCount[currentCellIndexValue];
    if (currentCount >= MAX_COLLISIONS_PER_CELL) {
        return -1;
    }

    int contactIndex = begin(currentCellIndexValue) + currentCount;
    collidedCellIndex[contactIndex] = collidedCellIndexValue;
    StoredGeometry& storedGeometry = this->geometry[contactIndex];
    storedGeometry.distanceX = distanceX;
    storedGeometry.distanceY = distanceY;
    storedGeometry.distanceSq = distanceSq;
    cellContactCount[currentCellIndexValue] = currentCount + 1;
    return contactIndex;
}

int CellData::CellContactBuffer::begin(int cellIndex) const {
    return cellIndex * MAX_COLLISIONS_PER_CELL;
}

int CellData::CellContactBuffer::end(int cellIndex) const {
    return begin(cellIndex) + cellContactCount[cellIndex];
}

int CellData::CellContactBuffer::count(int cellIndex) const {
    return cellContactCount[cellIndex];
}

void CellData::CellContactBuffer::setCount(int cellIndex, int countValue) {
    cellContactCount[cellIndex] = countValue;
}

void CellData::CellContactBuffer::copyCell(int fromCellIndex, int toCellIndex) {
    int sourceBegin = begin(fromCellIndex);
    int targetBegin = begin(toCellIndex);
    int contactCount = cellContactCount[fromCellIndex];
    for (int i = 0; i < contactCount; i++) {
        copyContact(sourceBegin + i, targetBegin + i, toCellIndex);
    }
    cellContactCount[toCellIndex] = contactCount;
}

void CellData::CellContactBuffer::copyContact(int fromContactIndex, int toContactIndex, int currentCellIndexValue) {
    (void)currentCellIndexValue;
    collidedCellIndex[toContactIndex] = collidedCellIndex[fromContactIndex];
    geometry[toContactIndex] = geometry[fromContactIndex];
}

void CellData::CellContactBuffer::getGeometry(int contactIndex,
        double currentCellRadius,
        double collidedCellRadius,
        CellCollisionGeometry& geometry) const {
    const StoredGeometry& storedGeometry = this->geometry[contactIndex];
    geometry.distanceX = storedGeometry.distanceX;
    geometry.distanceY = storedGeometry.distanceY;
    geometry.distance = std::sqrt(storedGeometry.distanceSq);
    geometry.contactChordLengthSquared = ((-geometry.distance + currentCellRadius - collidedCellRadius)
                                                 * (-geometry.distance - currentCellRadius + collidedCellRadius)
                                                 * (-geometry.distance + currentCellRadius + collidedCellRadius)
                                                 * (geometry.distance + currentCellRadius + collidedCellRadius))
            / storedGeometry.distanceSq;
    geometry.normalX = -storedGeometry.distanceX / geometry.distance;
    geometry.normalY = -storedGeometry.distanceY / geometry.distance;
}

CellData::CellCollisionIntentBuffer::CellCollisionIntentBuffer(int maxCellCount)
        : currentCellIndex(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          collidedCellIndex(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          distanceX(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          distanceY(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          distance(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL),
          contactChordLengthSquared(static_cast<size_t>(maxCellCount) * MAX_COLLISIONS_PER_CELL) {
}

void CellData::CellCollisionIntentBuffer::clear() {
    count = 0;
}

void CellData::CellCollisionIntentBuffer::add(int currentCellIndexValue,
        int collidedCellIndexValue,
        const CellCollisionGeometry& geometry) {
    const int intentIndex = count++;
    currentCellIndex[intentIndex] = currentCellIndexValue;
    collidedCellIndex[intentIndex] = collidedCellIndexValue;
    distanceX[intentIndex] = geometry.distanceX;
    distanceY[intentIndex] = geometry.distanceY;
    distance[intentIndex] = geometry.distance;
    contactChordLengthSquared[intentIndex] = geometry.contactChordLengthSquared;
}

void CellData::CellCollisionIntentBuffer::getGeometry(int intentIndex, CellCollisionGeometry& geometry) const {
    geometry.distanceX = distanceX[intentIndex];
    geometry.distanceY = distanceY[intentIndex];
    geometry.distance = distance[intentIndex];
    geometry.contactChordLengthSquared = contactChordLengthSquared[intentIndex];
    geometry.normalX = -geometry.distanceX / geometry.distance;
    geometry.normalY = -geometry.distanceY / geometry.distance;
}

void CellData::moveLastCellForCompaction(int from, int to) {
    assert(from == mCellCount - 1);
    assert(to >= 0 && to <= from);
    mRuntimeIds[to] = mRuntimeIds[from];
    mPositionX[to] = mPositionX[from];
    mPositionY[to] = mPositionY[from];
    mVelocityX[to] = mVelocityX[from];
    mVelocityY[to] = mVelocityY[from];
    mAngle[to] = mAngle[from];
    mCachedAngle[to] = mCachedAngle[from];
    mCachedAngleCos[to] = mCachedAngleCos[from];
    mCachedAngleSin[to] = mCachedAngleSin[from];
    mAngularVelocity[to] = mAngularVelocity[from];
    mAngleCytoplasm[to] = mAngleCytoplasm[from];
    mAngularVelocityCytoplasm[to] = mAngularVelocityCytoplasm[from];
    mRadius[to] = mRadius[from];
    mRadiusSq[to] = mRadiusSq[from];
    mMass[to] = mMass[from];
    mAge[to] = mAge[from];
    mColor[to] = mColor[from];
    mIsDead[to] = mIsDead[from];
    moveGenomeHandle(from, to);
    mOriginTag[to] = mOriginTag[from];
    mOrganismIds[to] = mOrganismIds[from];
    mParentOrganismIds[to] = mParentOrganismIds[from];
    mSelectTag[to] = mSelectTag[from];
    mSeldomCounter[to] = mSeldomCounter[from];
    mPendingMutationCount[to] = mPendingMutationCount[from];
    mHairRotation[to] = mHairRotation[from];
    hairX[to] = hairX[from];
    hairY[to] = hairY[from];
    mNitrates[to] = mNitrates[from];

    mIsMirrored[to] = mIsMirrored[from];
    for (int i = 0; i < SIGNAL_SUBSTANCE_COUNT; i++) {
        mSignalSubstance[to][i] = mSignalSubstance[from][i];
        mSignalSubstanceOut[to][i] = mSignalSubstanceOut[from][i];
    }
    mIsStuck[to] = mIsStuck[from];
    mToxin[to] = mToxin[from];
    mToxication[to] = mToxication[from];
    mMyocyteContraction[to] = mMyocyteContraction[from];
    mMyocyteBending[to] = mMyocyteBending[from];
    mMyocyteLifting[to] = mMyocyteLifting[from];
    mLipids[to] = mLipids[from];
    mSwimAnimationTime[to] = mSwimAnimationTime[from];
    mSwimAnimationTime2[to] = mSwimAnimationTime2[from];

    mForceX[to] = mForceX[from];
    mForceY[to] = mForceY[from];
    mForceZ[to] = mForceZ[from];

    mMutationCount[to] = mMutationCount[from];
    mTelomeresCount[to] = mTelomeresCount[from];
    mGridIndex1[to] = mGridIndex1[from];
    mCellContactBuffer.copyCell(from, to);
    mFertilizationTarget[to] = mFertilizationTarget[from];

    mPositionZ[to] = mPositionZ[from];
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::CellCopy,
            cellularity::verification::traceI32(from),
            cellularity::verification::traceI32(to),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[to])),
            cellularity::verification::traceF64(mMass[to]));
}

cellularity::GeneView CellData::getActiveGeneView(int cellIndex) const {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    const std::size_t index = static_cast<std::size_t>(cellIndex);
    assert(mActiveGeneData[index] != nullptr);
#ifndef NDEBUG
    assert(mGenomePool.verifyCachedGene(
            mGenomeIds[index], mGeneIndices[index], mActiveGeneIds[index]));
    const auto metadata = mGenomePool.metadata(mGenomeIds[index]);
    assert(metadata.has_value());
    assert((mActiveGeneInitialFlags[index] != 0)
            == (mGeneIndices[index] == metadata->initialGeneIndex));
#endif
    return cellularity::GeneView(
            *mActiveGeneData[index],
            mGeneIndices[index],
            mActiveGeneInitialFlags[index] != 0);
}

cellularity::GeneView CellData::getGeneView(
        int cellIndex,
        cellularity::GeneIndex geneIndex) const {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    const auto view = mGenomePool.geneView(
            mGenomeIds[static_cast<std::size_t>(cellIndex)], geneIndex);
    assert(view.has_value());
    return *view;
}

cellularity::GenomeView CellData::getGenomeView(int cellIndex) const {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    const auto view = mGenomePool.view(
            mGenomeIds[static_cast<std::size_t>(cellIndex)]);
    assert(view.has_value());
    return *view;
}

cellularity::GenomeData CellData::materializeGenome(int cellIndex) const {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    const auto genome = mGenomePool.materialize(
            mGenomeIds[static_cast<std::size_t>(cellIndex)]);
    if (!genome.has_value()) {
        throw std::logic_error("Cell owns an invalid GenomeId");
    }
    return *genome;
}

std::size_t CellData::geneCount(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mGenomePool.geneCount(
                              mGenomeIds[static_cast<std::size_t>(cellIndex)])
            .value_or(0);
}

cellularity::GenomeId CellData::genomeId(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mGenomeIds[static_cast<std::size_t>(cellIndex)];
}

cellularity::GeneIndex CellData::activeGeneIndex(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mGeneIndices[static_cast<std::size_t>(cellIndex)];
}

cellularity::GeneId CellData::activeGeneId(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mActiveGeneIds[static_cast<std::size_t>(cellIndex)];
}

std::uint32_t CellData::genomeReferenceCount(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mGenomePool.referenceCount(
                              mGenomeIds[static_cast<std::size_t>(cellIndex)])
            .value_or(0);
}

bool CellData::replaceGenome(
        int cellIndex,
        const cellularity::GenomeData& genome,
        cellularity::GeneIndex activeGeneIndex) {
    if (cellIndex < 0
            || cellIndex >= mCellCount
            || !cellularity::isGeneIndexInBounds(
                    activeGeneIndex, genome.size())) {
        return false;
    }
    const cellularity::GenomePoolResult created = mGenomePool.create(genome);
    if (!created.hasValue()) {
        return false;
    }
    installOwnedGenomeHandle(cellIndex, created.id, activeGeneIndex);
    refreshActiveCellType(cellIndex);
    return true;
}

bool CellData::replaceGenome(
        int cellIndex,
        const cellularity::GenomeData& genome) {
    cellularity::GenomeData normalized = genome;
    const auto metadata = cellularity::normalizeLegacyInitialGeneFlags(
            normalized.data(), normalized.size());
    return metadata.has_value()
            && replaceGenome(cellIndex, normalized, metadata->initialGeneIndex);
}

bool CellData::replaceGenome(
        int cellIndex,
        const cellularity::FixedGenome& genome,
        cellularity::GeneIndex activeGeneIndex) {
    return replaceGenome(
            cellIndex,
            cellularity::GenomeData(genome.begin(), genome.end()),
            activeGeneIndex);
}

bool CellData::replaceGenome(
        int cellIndex,
        const cellularity::FixedGenome& genome) {
    return replaceGenome(
            cellIndex,
            cellularity::GenomeData(genome.begin(), genome.end()));
}

bool CellData::importSharedGenomes(
        const std::vector<cellularity::CanonicalGene>& uniqueGenes,
        const std::vector<std::uint64_t>& geneReferenceOffsets,
        const std::vector<std::uint32_t>& fileGeneReferences,
        const std::vector<cellularity::GeneIndex>& initialGeneIndices,
        const std::vector<std::uint32_t>& cellGenomeReferences,
        const std::vector<cellularity::GeneIndex>& activeGeneIndices) {
    const std::size_t genomeCount = initialGeneIndices.size();
    if (cellGenomeReferences.size() != static_cast<std::size_t>(mCellCount)
            || activeGeneIndices.size() != static_cast<std::size_t>(mCellCount)
            || geneReferenceOffsets.size() != genomeCount + 1U
            || geneReferenceOffsets.empty()
            || geneReferenceOffsets.front() != 0
            || geneReferenceOffsets.back() != fileGeneReferences.size()
            || (mCellCount == 0
                    && (genomeCount != 0 || !uniqueGenes.empty()
                            || !fileGeneReferences.empty()))
            || (mCellCount > 0
                    && (genomeCount == 0 || uniqueGenes.empty()))) {
        return false;
    }

    std::vector<bool> usedGenomes(genomeCount, false);
    std::vector<bool> usedGenes(uniqueGenes.size(), false);
    for (std::size_t genome = 0; genome < genomeCount; ++genome) {
        const std::uint64_t begin = geneReferenceOffsets[genome];
        const std::uint64_t end = geneReferenceOffsets[genome + 1U];
        if (end < begin || end > fileGeneReferences.size()
                || !cellularity::isSupportedGeneCount(end - begin)
                || !cellularity::isGeneIndexInBounds(
                        initialGeneIndices[genome],
                        static_cast<std::size_t>(end - begin))) {
            return false;
        }
        for (std::uint64_t offset = begin; offset < end; ++offset) {
            const std::uint32_t fileGene = fileGeneReferences[static_cast<std::size_t>(offset)];
            if (fileGene >= uniqueGenes.size()) {
                return false;
            }
            usedGenes[fileGene] = true;
        }
    }
    for (std::size_t cell = 0; cell < cellGenomeReferences.size(); ++cell) {
        const std::uint32_t genome = cellGenomeReferences[cell];
        if (genome >= genomeCount) {
            return false;
        }
        const std::uint64_t begin = geneReferenceOffsets[genome];
        const std::uint64_t end = geneReferenceOffsets[genome + 1U];
        if (end < begin || end > fileGeneReferences.size()
                || !cellularity::isSupportedGeneCount(end - begin)
                || !cellularity::isGeneIndexInBounds(
                        activeGeneIndices[cell],
                        static_cast<std::size_t>(end - begin))) {
            return false;
        }
        usedGenomes[genome] = true;
    }
    if (std::find(usedGenomes.begin(), usedGenomes.end(), false)
                    != usedGenomes.end()
            || std::find(usedGenes.begin(), usedGenes.end(), false)
                    != usedGenes.end()) {
        return false;
    }

    std::vector<cellularity::GeneId> importedGenes;
    importedGenes.reserve(uniqueGenes.size());
    std::vector<cellularity::GenomeId> imported;
    imported.reserve(genomeCount);
    std::vector<cellularity::GeneId> resolvedGenes;
    resolvedGenes.reserve(cellularity::MAX_SUPPORTED_GENES_PER_GENOME);
    const auto releaseImportedGenomes = [&]() noexcept {
        for (cellularity::GenomeId id : imported) {
            const cellularity::GenomePoolError released = mGenomePool.release(id);
            (void)released;
            assert(released == cellularity::GenomePoolError::None);
        }
    };
    const auto releaseImportedGenes = [&]() noexcept {
        for (cellularity::GeneId id : importedGenes) {
            const cellularity::GenePoolError released = mGenePool.release(id);
            (void)released;
            assert(released == cellularity::GenePoolError::None);
        }
    };

    for (const cellularity::CanonicalGene& gene : uniqueGenes) {
        const cellularity::GenePoolInternResult interned = mGenePool.intern(gene);
        if (!interned.hasValue()) {
            releaseImportedGenes();
            return false;
        }
        importedGenes.push_back(interned.id);
    }

    for (std::size_t genome = 0; genome < genomeCount; ++genome) {
        const std::uint64_t begin = geneReferenceOffsets[genome];
        const std::uint64_t end = geneReferenceOffsets[genome + 1U];
        resolvedGenes.clear();
        for (std::uint64_t offset = begin; offset < end; ++offset) {
            resolvedGenes.push_back(importedGenes[fileGeneReferences[static_cast<std::size_t>(offset)]]);
        }
        const cellularity::GenomePoolResult created =
                mGenomePool.createFromGeneIds(
                        resolvedGenes.data(),
                        resolvedGenes.size(),
                        {initialGeneIndices[genome]});
        if (!created.hasValue()) {
            releaseImportedGenomes();
            releaseImportedGenes();
            return false;
        }
        imported.push_back(created.id);
    }

    std::size_t retainedCells = 0;
    for (; retainedCells < cellGenomeReferences.size(); ++retainedCells) {
        const cellularity::GenomeId id =
                imported[cellGenomeReferences[retainedCells]];
        if (mGenomePool.retain(id) != cellularity::GenomePoolError::None) {
            for (std::size_t rollback = 0; rollback < retainedCells; ++rollback) {
                const cellularity::GenomePoolError released = mGenomePool.release(
                        imported[cellGenomeReferences[rollback]]);
                (void)released;
                assert(released == cellularity::GenomePoolError::None);
            }
            releaseImportedGenomes();
            releaseImportedGenes();
            return false;
        }
    }

    for (std::size_t cell = 0; cell < cellGenomeReferences.size(); ++cell) {
        installOwnedGenomeHandle(
                static_cast<int>(cell),
                imported[cellGenomeReferences[cell]],
                activeGeneIndices[cell]);
        refreshActiveCellType(static_cast<int>(cell));
    }
    releaseImportedGenomes();
    releaseImportedGenes();
    return true;
}

bool CellData::replaceGene(
        int cellIndex,
        cellularity::GeneIndex geneIndex,
        const GeneData& candidate,
        bool* changed) {
    if (changed != nullptr) {
        *changed = false;
    }
    if (cellIndex < 0 || cellIndex >= mCellCount) {
        return false;
    }
    const std::size_t cell = static_cast<std::size_t>(cellIndex);
    const cellularity::GenomePoolResult result = mGenomePool.replaceGene(
            mGenomeIds[cell], geneIndex, candidate);
    if (!result.hasValue()) {
        return false;
    }
    mGenomeIds[cell] = result.id;
    if (result.detached || geneIndex == mGeneIndices[cell]) {
        refreshActiveGeneCache(cellIndex);
    }
    if (geneIndex == mGeneIndices[cell] && result.changed) {
        const cellularity::GeneView activeGene = getActiveGeneView(cellIndex);
        mActiveCellType[cell] = activeGene.cellType();
        mActiveCellCollisionElasticity[cell] =
                0.5 / activeGene.floatValue(Gene::FloatType::CYTOSKELETON);
    }
    if (changed != nullptr) {
        *changed = result.changed;
    }
    return true;
}

bool CellData::mutatePooledGenomeDuringMetabolism(
        int cellIndex,
        Environment& environment) {
    const cellularity::GenomeView pooledGenome = getGenomeView(cellIndex);
    const SelectedGenomeMutation mutation =
            selectGenomeMutation(pooledGenome, environment);
    if (mutation.geneIndex < 0 || mutation.mutationWeightIndex < 0) {
        return false;
    }

    const bool isStructural =
            mutation.mutationWeightIndex
                    >= Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET
            && mutation.mutationWeightIndex
                    < Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET
                            + Environment::MUTATION_STRUCTURAL_WEIGHT_COUNT;
    const bool changesGenomeLength =
            mutation.mutationWeightIndex
                    >= Environment::MUTATION_GENOME_LENGTH_WEIGHT_OFFSET
            && mutation.mutationWeightIndex
                    < Environment::MUTATION_GENOME_LENGTH_WEIGHT_OFFSET
                            + Environment::MUTATION_GENOME_LENGTH_WEIGHT_COUNT;
    const bool changesCellType =
            mutation.mutationWeightIndex
                    >= Environment::MUTATION_CELL_TYPE_WEIGHT_OFFSET
            && mutation.mutationWeightIndex
                    < Environment::MUTATION_CELL_TYPE_WEIGHT_OFFSET
                            + Environment::MUTATION_CELL_TYPE_WEIGHT_COUNT;

    // Structural edits, genome-length changes, and Cell-type changes can repair
    // compatibility targets in several genes. Keep those operations
    // transactional by materializing once and installing one immutable record.
    if (isStructural || changesGenomeLength || changesCellType) {
        cellularity::GenomeData candidate = materializeGenome(cellIndex);
        cellularity::GeneIndex candidateActive = activeGeneIndex(cellIndex);
        if (!applySelectedGenomeMutation(
                    candidate, mutation, candidateActive, environment)) {
            return false;
        }
        return replaceGenome(cellIndex, candidate, candidateActive);
    }

    const cellularity::GeneIndex targetIndex(
            static_cast<cellularity::GeneIndex::Value>(mutation.geneIndex));
    auto candidate = mGenomePool.copyGeneForMutation(
            mGenomeIds[static_cast<std::size_t>(cellIndex)], targetIndex);
    if (!candidate.has_value()) {
        return false;
    }

    const bool mutated = cellularity::GeneBuilder(*candidate)
                                 .mutatePointByWeightIndex(
                                         environment,
                                         mutation.mutationWeightIndex,
                                         pooledGenome);
    if (!mutated) {
        return false;
    }
    return replaceGene(cellIndex, targetIndex, *candidate);
}

bool CellData::setActiveGeneIndex(
        int cellIndex,
        cellularity::GeneIndex geneIndex) {
    if (cellIndex < 0
            || cellIndex >= mCellCount
            || !cellularity::isGeneIndexInBounds(
                    geneIndex,
                    mGenomePool.geneCount(
                                       mGenomeIds[static_cast<std::size_t>(cellIndex)])
                            .value_or(0))) {
        return false;
    }
    mGeneIndices[static_cast<std::size_t>(cellIndex)] = geneIndex;
    refreshActiveCellType(cellIndex);
    return true;
}

void CellData::refreshActiveCellType(int cellIndex) {
    refreshActiveGeneCache(cellIndex);
    const cellularity::GeneView activeGene = getActiveGeneView(cellIndex);
    mActiveCellType[cellIndex] = activeGene.cellType();
    mActiveCellCollisionElasticity[cellIndex] =
            0.5 / activeGene.floatValue(Gene::FloatType::CYTOSKELETON);
}

void CellData::refreshRadiusSq(int cellIndex) {
    mRadiusSq[cellIndex] = mRadius[cellIndex] * mRadius[cellIndex];
}

void CellData::refreshAllRadiusSq() {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        refreshRadiusSq(cellIndex);
    }
}

float CellData::getValue(int cellIndex, int valueIndex, int readOffset) const {
    const cellularity::GeneParametricValueView parametricValue =
            getActiveGeneView(cellIndex).parametricValue(valueIndex);
    return evaluateParametricValue(
            *this, cellIndex, parametricValue, valueIndex, readOffset);
}

double CellData::getScaledMass(int index) const {
    if (mActiveCellType[index] == CellType::LIPOCYTE) {
        return (mMass[index] + mLipids[index]) * MAX_MASS / MAX_LIPO_MASS;
    }
    return mMass[index];
}

int CellData::getCellIndex(double positionX, double positionY) const {
    double minRadiusSq = MAX_CELL_RADIUS * MAX_CELL_RADIUS * 10.0;
    int resultCellIndex = -1;
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        double dx = positionX - mPositionX[cellIndex];
        double dy = positionY - mPositionY[cellIndex];
        double radiusSq = dx * dx + dy * dy;
        if (radiusSq <= mRadius[cellIndex] * mRadius[cellIndex] && radiusSq < minRadiusSq) {
            resultCellIndex = cellIndex;
            minRadiusSq = radiusSq;
        }
    }
    return resultCellIndex;
}

void CellData::deselectAll() {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        mSelectTag[cellIndex] = 0;
    }
}

int CellData::getIdFromSelectTag(int tag) const {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        if (mSelectTag[cellIndex] == tag) {
            return cellIndex;
        }
    }
    return -1;
}

bool CellData::isCellCanMakeLink(int cellIndex) const {
    return mTopology.canMakeLink(cellIndex);
}

bool CellData::isCellSafeByKeratinocyte(int currentCellIndex) const {
    bool meSafe = mActiveCellType[currentCellIndex] == CellType::KERATINOCYTE;

    if (!meSafe) {
        const int linkCount = mTopology.linkCount(currentCellIndex);
        for (int li = 0; li < linkCount; li++) {
            int connectedCellIndex = mTopology.adjacentCell(currentCellIndex, li);
            if (connectedCellIndex < 0) {
                continue;
            }

            if (mActiveCellType[connectedCellIndex] != CellType::KERATINOCYTE) {
                continue;
            }

            meSafe = true;
            break;
        }
    }
    return meSafe;
}

bool CellData::isNotLinked(int currentCellIndex, int anotherCellIndex) const {
    if (anotherCellIndex == BOUNDARY_CONNECTION_INDEX) {
        return !mTopology.hasBoundaryLink(currentCellIndex);
    }
    return !mTopology.isLinked(currentCellIndex, anotherCellIndex);
}

bool CellData::isCellReadyForFertilization(int cellIndex) const {
    return mActiveCellType[cellIndex] == CellType::GAMETE && mAge[cellIndex] > AGE_OF_CONSENT;
}

bool CellData::isValidCellCollisionTarget(int currentCellIndex, int collidedCellIndex) const {
    return collidedCellIndex != currentCellIndex
            && collidedCellIndex >= 0
            && collidedCellIndex < mCellCount
            && !mIsDead[collidedCellIndex];
}

bool CellData::calculateCellCollisionGeometryFromDistance(int currentCellIndex,
        int collidedCellIndex,
        double distanceX,
        double distanceY,
        double distanceSq,
        CellCollisionGeometry& geometry) const {
    if (!isValidCellCollisionTarget(currentCellIndex, collidedCellIndex) || distanceSq == 0.0) {
        return false;
    }

    double currentCellRadius = mRadius[currentCellIndex];
    double collidedCellRadius = mRadius[collidedCellIndex];
    double contactDistance = currentCellRadius + collidedCellRadius;
    if (distanceSq >= contactDistance * contactDistance) {
        return false;
    }

    double distance = std::sqrt(distanceSq);
    geometry.distanceX = distanceX;
    geometry.distanceY = distanceY;
    geometry.distance = distance;
    geometry.contactChordLengthSquared = ((-distance + currentCellRadius - collidedCellRadius)
                                                 * (-distance - currentCellRadius + collidedCellRadius)
                                                 * (-distance + currentCellRadius + collidedCellRadius)
                                                 * (distance + currentCellRadius + collidedCellRadius))
            / distanceSq;
    geometry.normalX = -distanceX / distance;
    geometry.normalY = -distanceY / distance;
    return true;
}

void CellData::clearCellCollisionContacts() {
    mCellContactBuffer.clear(mCellCount);
}

void CellData::clearCellCollisionContacts(int cellIndex) {
    mCellContactBuffer.clearCell(cellIndex);
}

void CellData::removeCollisionContactsTargetingDeadCells(
        SimulationWorldProfile* profile) {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        if (mIsDead[cellIndex]) {
            continue;
        }

        const int contactBegin = cellCollisionContactBegin(cellIndex);
        const int contactEnd = cellCollisionContactEnd(cellIndex);
        int writeIndex = contactBegin;
        for (int readIndex = contactBegin; readIndex < contactEnd; readIndex++) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->cleanupCollisionReads++;
            }
            const int collidedCellIndex = cellCollisionContactTarget(readIndex);
            const bool keepContact = collidedCellIndex >= 0
                    && collidedCellIndex < mCellCount
                    && !mIsDead[collidedCellIndex];
            CELLULARITY_SEMANTIC_EVENT(
                    cellularity::verification::TraceChannel::Endpoint,
                    cellularity::verification::SemanticEventKind::EndpointCleanup,
                    cellularity::verification::traceI32(cellIndex),
                    cellularity::verification::traceI32(collidedCellIndex),
                    cellularity::verification::traceI32(readIndex - contactBegin),
                    cellularity::verification::traceI32(writeIndex - contactBegin),
                    cellularity::verification::traceBool(keepContact));
            if (!keepContact) {
                continue;
            }

            if (writeIndex != readIndex) {
                mCellContactBuffer.copyContact(
                        readIndex, writeIndex, cellIndex);
            }
            writeIndex++;
            CELLULARITY_PROFILE_IF(profile) {
                profile->cleanupCollisionWrites++;
            }
        }
        mCellContactBuffer.setCount(cellIndex, writeIndex - contactBegin);
    }
}

bool CellData::addCellCollisionContact(int currentCellIndex,
        int collidedCellIndex,
        double distanceX,
        double distanceY,
        double distanceSq) {
    CellCollisionGeometry checkedGeometry;
    if (!calculateCellCollisionGeometryFromDistance(currentCellIndex,
                collidedCellIndex,
                distanceX,
                distanceY,
                distanceSq,
                checkedGeometry)) {
        return false;
    }

    return addKnownCellCollisionContact(currentCellIndex,
            collidedCellIndex,
            distanceX,
            distanceY,
            distanceSq,
            mRadius[currentCellIndex],
            mRadius[collidedCellIndex]);
}

bool CellData::addKnownCellCollisionContact(int currentCellIndex,
        int collidedCellIndex,
        double distanceX,
        double distanceY,
        double distanceSq,
        double currentCellRadius,
        double collidedCellRadius) {
    (void)currentCellRadius;
    (void)collidedCellRadius;
    if (mCellContactBuffer.count(currentCellIndex) >= MAX_COLLISIONS_PER_CELL) {
        return false;
    }

    int contactIndex = mCellContactBuffer.add(currentCellIndex,
            collidedCellIndex,
            distanceX,
            distanceY,
            distanceSq);
    if (contactIndex < 0) {
        return false;
    }

    return true;
}

int CellData::cellCollisionContactBegin(int cellIndex) const {
    return mCellContactBuffer.begin(cellIndex);
}

int CellData::cellCollisionContactEnd(int cellIndex) const {
    return mCellContactBuffer.end(cellIndex);
}

int CellData::cellCollisionContactCount(int cellIndex) const {
    return mCellContactBuffer.count(cellIndex);
}

int CellData::cellCollisionContactTarget(int contactIndex) const {
    return mCellContactBuffer.collidedCellIndex[contactIndex];
}

void CellData::getCellCollisionContactGeometry(int contactIndex, CellCollisionGeometry& geometry) const {
    int currentCellIndex = contactIndex / MAX_COLLISIONS_PER_CELL;
    int collidedCellIndex = cellCollisionContactTarget(contactIndex);
    mCellContactBuffer.getGeometry(contactIndex,
            mRadius[currentCellIndex],
            mRadius[collidedCellIndex],
            geometry);
}

double CellData::calculateGameteForceModifier(int currentCellIndex, int collidedCellIndex) const {
    CellType currentCellType = mActiveCellType[currentCellIndex];
    CellType collidedCellType = mActiveCellType[collidedCellIndex];
    if (currentCellType != CellType::GAMETE || collidedCellType != CellType::GAMETE) {
        return 1.0;
    }

    if (mAge[currentCellIndex] <= AGE_OF_CONSENT || mAge[collidedCellIndex] <= AGE_OF_CONSENT) {
        return 1.0;
    }

    int currentCellGeneIndex = activeGeneIndex(currentCellIndex).value();
    int collidedCellGeneIndex = activeGeneIndex(collidedCellIndex).value();
    const cellularity::GeneView currentCellActiveGene =
            getActiveGeneView(currentCellIndex);
    const cellularity::GeneView collidedCellActiveGene =
            getActiveGeneView(collidedCellIndex);

    const int currentCellTargetKey =
            currentCellActiveGene.virusToGeneIndex().value();
    const int collidedCellTargetKey =
            collidedCellActiveGene.virusToGeneIndex().value();

    bool currentMatchesCollided = (currentCellGeneIndex == collidedCellTargetKey);
    bool collidedMatchesCurrent = (collidedCellGeneIndex == currentCellTargetKey);

    if (currentMatchesCollided && collidedMatchesCurrent) {
        return -0.6;
    }

    if (currentMatchesCollided || collidedMatchesCurrent) {
        return 0.0;
    }

    return 1.0;
}

void CellData::applyCellCollisionForce(int currentCellIndex, int collidedCellIndex, const CellCollisionGeometry& geometry) {
    double currentCellElasticity = mActiveCellCollisionElasticity[currentCellIndex];
    double collidedCellElasticity = mActiveCellCollisionElasticity[collidedCellIndex];

    double forceMagnitude = (TURGOR_PRESSURE * geometry.contactChordLengthSquared / (currentCellElasticity + collidedCellElasticity) / geometry.distance)
            * calculateGameteForceModifier(currentCellIndex, collidedCellIndex);

    mForceX[currentCellIndex] += geometry.distanceX * forceMagnitude;
    mForceY[currentCellIndex] += geometry.distanceY * forceMagnitude;
}

void CellData::handleSingleCellCollisionPartial(int currentCellIndex,
        int collidedCellIndex,
        const CellCollisionGeometry& geometry,
        double deltaTime,
        Environment& environment) {
    applyCellCollisionForce(currentCellIndex, collidedCellIndex, geometry);
    handleVirocyteCollision(currentCellIndex, collidedCellIndex, deltaTime, environment);
    handleGlueocyteCollision(currentCellIndex, collidedCellIndex);
    handleDevorocyteCollision(currentCellIndex, collidedCellIndex, deltaTime);
    handleSecrocyteCollision(currentCellIndex, collidedCellIndex, deltaTime, environment);
    handleCiliocyteCollision(currentCellIndex, collidedCellIndex, geometry.normalX, geometry.normalY);
}

void CellData::collectCellToCellCollisionIntentsPartial(int currentCellIndex) {
    if (mIsDead[currentCellIndex]) {
        return;
    }

    const int contactBegin = cellCollisionContactBegin(currentCellIndex);
    const int contactEnd = cellCollisionContactEnd(currentCellIndex);
    if (contactBegin == contactEnd) {
        return;
    }

    const double currentCellRadius = mRadius[currentCellIndex];
    for (int contactIndex = contactBegin; contactIndex < contactEnd; contactIndex++) {
        const int collidedCellIndex = mCellContactBuffer.collidedCellIndex[contactIndex];
        if (collidedCellIndex < 0
                || collidedCellIndex >= mCellCount
                || mIsDead[collidedCellIndex]) {
            continue;
        }
        const CellContactBuffer::StoredGeometry& storedGeometry = mCellContactBuffer.geometry[contactIndex];
        const double collidedCellRadius = mRadius[collidedCellIndex];
        const double distance = std::sqrt(storedGeometry.distanceSq);
        const double contactChordLengthSquared = ((-distance + currentCellRadius - collidedCellRadius)
                                                         * (-distance - currentCellRadius + collidedCellRadius)
                                                         * (-distance + currentCellRadius + collidedCellRadius)
                                                         * (distance + currentCellRadius + collidedCellRadius))
                / storedGeometry.distanceSq;
        const int intentIndex = mCellCollisionIntentBuffer.count++;
        mCellCollisionIntentBuffer.currentCellIndex[intentIndex] = currentCellIndex;
        mCellCollisionIntentBuffer.collidedCellIndex[intentIndex] = collidedCellIndex;
        mCellCollisionIntentBuffer.distanceX[intentIndex] = storedGeometry.distanceX;
        mCellCollisionIntentBuffer.distanceY[intentIndex] = storedGeometry.distanceY;
        mCellCollisionIntentBuffer.distance[intentIndex] = distance;
        mCellCollisionIntentBuffer.contactChordLengthSquared[intentIndex] = contactChordLengthSquared;
    }
}

void CellData::collectAllCellToCellCollisionIntentsPartial() {
    mCellCollisionIntentBuffer.clear();
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        collectCellToCellCollisionIntentsPartial(cellIndex);
    }
}

void CellData::applyCellTypedCollisionIntentPartial(int intentIndex,
        double deltaTime,
        Environment& environment,
        SimulationWorldProfile* profile) {
    const int currentCellIndex = mCellCollisionIntentBuffer.currentCellIndex[intentIndex];
    const int victimCellIndex = mCellCollisionIntentBuffer.collidedCellIndex[intentIndex];
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::ApplyTypedHandler,
            cellularity::verification::traceI32(intentIndex),
            cellularity::verification::traceI32(currentCellIndex),
            cellularity::verification::traceI32(victimCellIndex),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[currentCellIndex])),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[victimCellIndex])));
    switch (mActiveCellType[currentCellIndex]) {
        case CellType::VIROCYTE:
            CELLULARITY_PROFILE_IF(profile) {
                profile->collisionBiologicalIntentDispatched++;
                profile->collisionTypedIntentCommitted++;
                profile->collisionTypedIntentCommittedByActorType[CellTypes::ordinal(CellType::VIROCYTE)]++;
            }
            handleVirocyteCollision(currentCellIndex, victimCellIndex, deltaTime, environment);
            break;
        case CellType::GLUEOCYTE:
            CELLULARITY_PROFILE_IF(profile) {
                profile->collisionBiologicalIntentDispatched++;
                profile->collisionTypedIntentCommitted++;
                profile->collisionTypedIntentCommittedByActorType[CellTypes::ordinal(CellType::GLUEOCYTE)]++;
            }
            handleGlueocyteCollision(currentCellIndex, victimCellIndex);
            break;
        case CellType::DEVOROCYTE:
            CELLULARITY_PROFILE_IF(profile) {
                profile->collisionBiologicalIntentDispatched++;
                profile->collisionTypedIntentCommitted++;
                profile->collisionTypedIntentCommittedByActorType[CellTypes::ordinal(CellType::DEVOROCYTE)]++;
            }
            handleDevorocyteCollision(currentCellIndex, victimCellIndex, deltaTime);
            break;
        case CellType::SECROCYTE:
            CELLULARITY_PROFILE_IF(profile) {
                profile->collisionBiologicalIntentDispatched++;
                profile->collisionTypedIntentCommitted++;
                profile->collisionTypedIntentCommittedByActorType[CellTypes::ordinal(CellType::SECROCYTE)]++;
            }
            handleSecrocyteCollision(currentCellIndex, victimCellIndex, deltaTime, environment);
            break;
        case CellType::CILIOCYTE:
            CELLULARITY_PROFILE_IF(profile) {
                profile->collisionCiliocyteIntentDispatched++;
                profile->collisionTypedIntentCommitted++;
                profile->collisionTypedIntentCommittedByActorType[CellTypes::ordinal(CellType::CILIOCYTE)]++;
            }
            handleCiliocyteCollision(currentCellIndex,
                    victimCellIndex,
                    -mCellCollisionIntentBuffer.distanceX[intentIndex]
                            / mCellCollisionIntentBuffer.distance[intentIndex],
                    -mCellCollisionIntentBuffer.distanceY[intentIndex]
                            / mCellCollisionIntentBuffer.distance[intentIndex]);
            break;
        default:
            break;
    }
}

void CellData::applyCellToCellCollisionIntentPartial(int intentIndex,
        double deltaTime,
        Environment& environment,
        SimulationWorldProfile* profile) {
    const int currentCellIndex = mCellCollisionIntentBuffer.currentCellIndex[intentIndex];
    const int collidedCellIndex = mCellCollisionIntentBuffer.collidedCellIndex[intentIndex];
    CellCollisionGeometry geometry;
    geometry.distanceX = mCellCollisionIntentBuffer.distanceX[intentIndex];
    geometry.distanceY = mCellCollisionIntentBuffer.distanceY[intentIndex];
    geometry.distance = mCellCollisionIntentBuffer.distance[intentIndex];
    geometry.contactChordLengthSquared = mCellCollisionIntentBuffer.contactChordLengthSquared[intentIndex];
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::ApplyIntentBegin,
            cellularity::verification::traceI32(intentIndex),
            cellularity::verification::traceI32(currentCellIndex),
            cellularity::verification::traceI32(collidedCellIndex),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[currentCellIndex])),
            cellularity::verification::traceF64(geometry.distanceX),
            cellularity::verification::traceF64(geometry.distanceY),
            cellularity::verification::traceF64(geometry.distance),
            cellularity::verification::traceF64(geometry.contactChordLengthSquared));
    applyCellCollisionForce(currentCellIndex, collidedCellIndex, geometry);
    CELLULARITY_PROFILE_IF(profile) {
        profile->collisionOrdinaryForceApplications++;
    }
    applyCellTypedCollisionIntentPartial(intentIndex, deltaTime, environment, profile);
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::ApplyIntentResult,
            cellularity::verification::traceI32(intentIndex),
            cellularity::verification::traceI32(currentCellIndex),
            cellularity::verification::traceI32(collidedCellIndex),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[currentCellIndex])),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[collidedCellIndex])),
            cellularity::verification::traceF64(mForceX[currentCellIndex]),
            cellularity::verification::traceF64(mForceY[currentCellIndex]),
            cellularity::verification::traceF64(mMass[currentCellIndex]),
            cellularity::verification::traceF64(mMass[collidedCellIndex]),
            cellularity::verification::traceF64(mNitrates[currentCellIndex]),
            cellularity::verification::traceF64(mNitrates[collidedCellIndex]),
            cellularity::verification::traceF32(mToxin[currentCellIndex]),
            cellularity::verification::traceF32(mToxin[collidedCellIndex]),
            cellularity::verification::traceF32(mToxication[currentCellIndex]),
            cellularity::verification::traceF32(mToxication[collidedCellIndex]),
            cellularity::verification::traceI32(mTopology.edgeCount()),
            cellularity::verification::traceI32(mTopology.boundaryCount()),
            cellularity::verification::traceI32(mCiliocyteInteractionCount),
            cellularity::verification::traceI64(environment.mRandom.getSeed()));
}

void CellData::applyCellToCellCollisionIntentsPartial(double deltaTime,
        Environment& environment,
        SimulationWorldProfile* profile) {
    CELLULARITY_PROFILE_COARSE_REGION(profile, collisionIntentApplyNanos);
    for (int intentIndex = 0; intentIndex < mCellCollisionIntentBuffer.count; intentIndex++) {
        applyCellToCellCollisionIntentPartial(intentIndex, deltaTime, environment, profile);
    }
    CELLULARITY_PROFILE_IF(profile) {
        profile->collisionIntentApplied += mCellCollisionIntentBuffer.count;
    }
}

void CellData::handleCellToCellCollisionsPartial(
        int currentCellIndex,
        double deltaTime,
        Environment& environment) {
    mCellCollisionIntentBuffer.clear();
    collectCellToCellCollisionIntentsPartial(currentCellIndex);
    applyCellToCellCollisionIntentsPartial(
            deltaTime,
            environment);
}

void CellData::handleAllCellToCellCollisionsPartial(double deltaTime,
        Environment& environment,
        SimulationWorldProfile* profile) {
    mCiliocyteInteractionCount = 0;
    {
        CELLULARITY_PROFILE_COARSE_REGION(profile, collisionIntentCollectNanos);
        collectAllCellToCellCollisionIntentsPartial();
        CELLULARITY_PROFILE_IF(profile) {
            profile->collisionIntentCollected += mCellCollisionIntentBuffer.count;
            profile->collisionGeometryEvaluations += mCellCollisionIntentBuffer.count;
            for (int intentIndex = 0; intentIndex < mCellCollisionIntentBuffer.count; intentIndex++) {
                const int actorCellIndex = mCellCollisionIntentBuffer.currentCellIndex[intentIndex];
                const CellType actorType = mActiveCellType[actorCellIndex];
                switch (actorType) {
                    case CellType::VIROCYTE:
                    case CellType::GLUEOCYTE:
                    case CellType::DEVOROCYTE:
                    case CellType::SECROCYTE:
                    case CellType::CILIOCYTE:
                        profile->collisionTypedIntentCollected++;
                        profile->collisionTypedIntentCollectedByActorType[CellTypes::ordinal(actorType)]++;
                        break;
                    default:
                        break;
                }
            }
        }
    }
    applyCellToCellCollisionIntentsPartial(
            deltaTime,
            environment,
            profile);
}

void CellData::handleCellToBoundaryCollisionPartial(
        int currentCellIndex,
        const Environment& environment) {
    if (mIsDead[currentCellIndex]) {
        return;
    }

    const cellularity::GeneView currentCellActiveGene =
            getActiveGeneView(currentCellIndex);
    CellType currentCellType = mActiveCellType[currentCellIndex];

    double currentCellPositionX = mPositionX[currentCellIndex];
    double currentCellPositionY = mPositionY[currentCellIndex];
    double currentCellRadius = mRadius[currentCellIndex];

    const auto geometry = environment.geometry();
    cellularity::SubstrateBoundaryContact contact;
    if (!geometry.cellBoundaryContact(currentCellPositionX, currentCellPositionY, currentCellRadius, contact)) {
        return;
    }

    const double distance = contact.centerDistance;
    const double contactChordLength = contact.chordLength;
    if (environment.dieEdge) {
        return;
    }

    double forceMagnitude = TURGOR_PRESSURE / distance * contactChordLength * contactChordLength * 2
            * currentCellActiveGene.floatValue(
                    Gene::FloatType::CYTOSKELETON);
    forceMagnitude *= contact.normalDirection;
    mForceX[currentCellIndex] -= currentCellPositionX * forceMagnitude;
    mForceY[currentCellIndex] -= currentCellPositionY * forceMagnitude;

    if (currentCellType == CellType::GLUEOCYTE
            && mTopology.linkCount(currentCellIndex) < MAX_GLUEOCYTE_LINK_COUNT
            && mTopology.canMakeLink(currentCellIndex)
            && !mTopology.hasBoundaryLink(currentCellIndex)) {
        double an = std::atan2(currentCellPositionY, currentCellPositionX);
        if (contact.normalDirection < 0.0) {
            an = std::atan2(-currentCellPositionY, -currentCellPositionX);
        }
        double angleA = an - mAngle[currentCellIndex];
        const auto anchor = geometry.boundaryAnchor(currentCellPositionX, currentCellPositionY, distance, MIN_CELL_RADIUS * LINK_ATTACH_DEPTH);
        mTopology.addBoundaryLink(currentCellIndex, angleA, anchor[0], anchor[1], 0.5f, 0.0f);
    }

    handleCiliocyteToBoundaryCollision(currentCellIndex);
}

void CellData::handleAllCellToBoundaryCollisionsPartial(
        const Environment& environment) {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        handleCellToBoundaryCollisionPartial(
                cellIndex,
                environment);
    }
}

void CellData::updateAllCellInteractionsPartial(
        double deltaTime,
        Environment& environment) {
    handleAllCellToCellCollisionsPartial(
            deltaTime,
            environment);
    updateAllCellLinkInteractions(deltaTime);
    handleAllCellToBoundaryCollisionsPartial(environment);
    updateAllCellForces(deltaTime, environment);
}

void CellData::collectAllCollisionInteractionsPartial(
        double deltaTime,
        Environment& environment) {
    mCiliocyteInteractionCount = 0;
    collectAllCellToCellCollisionIntentsPartial();
    applyCellToCellCollisionIntentsPartial(
            deltaTime,
            environment);
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        handleCellToBoundaryCollisionPartial(
                cellIndex,
                environment);
    }
}

void CellData::applyCollectedCiliocyteInteractionsPartial(const Environment& environment) {
    updateCiliocyteInteractions(environment);
}

void CellData::updateCellForces(int currentCellIndex,
        double deltaTime,
        const Environment& environment,
        SimulationWorldProfile* profile) {
    CELLULARITY_PROFILE_IF(profile) {
        profile->cellForcesVisitedCells++;
    }
    if (mIsDead[currentCellIndex]) {
        CELLULARITY_PROFILE_IF(profile) {
            profile->cellForcesDeadSkips++;
        }
        return;
    }
    CELLULARITY_PROFILE_IF(profile) {
        profile->cellForcesLiveCells++;
    }

    double currentCellRadius = mRadius[currentCellIndex];
    double currentCellMass = mMass[currentCellIndex];

    double totalViscosity = currentCellRadius * environment.mViscosity;
    double effectiveViscosity = totalViscosity * (1.0 - 1.0 * deltaTime * totalViscosity * 0.5 / currentCellMass);

    mTorque[currentCellIndex] -= mAngularVelocity[currentCellIndex] * currentCellRadius * currentCellRadius * currentCellRadius * environment.mViscosity * 0.5;
    mForceX[currentCellIndex] -= mVelocityX[currentCellIndex] * effectiveViscosity;
    mForceY[currentCellIndex] -= mVelocityY[currentCellIndex] * effectiveViscosity;

    if (environment.mIsPreviewMode) {
        CELLULARITY_PROFILE_IF(profile) {
            profile->cellForcesPreviewSkips++;
        }
        return;
    }

    CellType currentCellType = mActiveCellType[currentCellIndex];
    double density = environment.getDensity(mPositionX[currentCellIndex], mPositionY[currentCellIndex]);
    double currentCellDensity;
    if (currentCellType == CellType::BUOYOCYTE) {
        CELLULARITY_PROFILE_IF(profile) {
            profile->cellForcesBuoyocytes++;
        }
        currentCellDensity = getValue(currentCellIndex, Gene::DENSITY_VALUE_INDEX, 0) - density;
    } else {
        currentCellDensity = 0.0f - density;
    }

    double gravityX;
    double gravityY;
    environment.getGravityVector(mPositionX[currentCellIndex], mPositionY[currentCellIndex], gravityX, gravityY);
    mForceX[currentCellIndex] += currentCellMass * currentCellDensity * gravityX;
    mForceY[currentCellIndex] += currentCellMass * currentCellDensity * gravityY;
}

void CellData::updateAllCellForces(double deltaTime, const Environment& environment, SimulationWorldProfile* profile) {
    const bool isPreviewMode = environment.mIsPreviewMode;
    const bool gravityLinear = environment.mGravityFieldType == Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL;
    const bool densityConstant = environment.mDensityFieldType == Environment::DENSITY_FIELD_CONSTANT;
    const bool densityLinear = environment.mDensityFieldType == Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL;
    const double radiusInv = environment.mRadius != 0.0 ? 1.0 / environment.mRadius : 1.0;
    const double gravityLinearX = gravityLinear ? std::cos(environment.mGravityFieldAngle) * environment.mGravityFieldStrength : 0.0;
    const double gravityLinearY = gravityLinear ? std::sin(environment.mGravityFieldAngle) * environment.mGravityFieldStrength : 0.0;
    const double densityLinearCos = densityLinear ? std::cos(environment.mDensityFieldAngle) : 0.0;
    const double densityLinearSin = densityLinear ? std::sin(environment.mDensityFieldAngle) : 0.0;
    const double densitySoftness = std::max(0.0, std::min(1.0, environment.mDensityFieldSoftness));

    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        CELLULARITY_PROFILE_IF(profile) {
            profile->cellForcesVisitedCells++;
        }
        if (mIsDead[cellIndex]) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->cellForcesDeadSkips++;
            }
            continue;
        }
        CELLULARITY_PROFILE_IF(profile) {
            profile->cellForcesLiveCells++;
        }

        double currentCellRadius = mRadius[cellIndex];
        double currentCellMass = mMass[cellIndex];
        double totalViscosity = currentCellRadius * environment.mViscosity;
        double effectiveViscosity = totalViscosity * (1.0 - 1.0 * deltaTime * totalViscosity * 0.5 / currentCellMass);

        mTorque[cellIndex] -= mAngularVelocity[cellIndex] * currentCellRadius * currentCellRadius * currentCellRadius * environment.mViscosity * 0.5;
        mForceX[cellIndex] -= mVelocityX[cellIndex] * effectiveViscosity;
        mForceY[cellIndex] -= mVelocityY[cellIndex] * effectiveViscosity;

        if (isPreviewMode) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->cellForcesPreviewSkips++;
            }
            continue;
        }

        double density;
        if (densityConstant) {
            density = environment.mDensityFieldBase;
        } else if (densityLinear) {
            double coordinate = (mPositionX[cellIndex] * radiusInv) * densityLinearCos
                    + (mPositionY[cellIndex] * radiusInv) * densityLinearSin
                    - environment.mDensityFieldOffset;
            coordinate = coordinate / (1.0 + densitySoftness * std::abs(coordinate) * 2.0);
            density = environment.mDensityFieldBase + environment.mDensityFieldStrength * coordinate;
        } else {
            density = environment.getDensity(mPositionX[cellIndex], mPositionY[cellIndex]);
        }

        double currentCellDensity;
        if (mActiveCellType[cellIndex] == CellType::BUOYOCYTE) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->cellForcesBuoyocytes++;
            }
            currentCellDensity = getValue(cellIndex, Gene::DENSITY_VALUE_INDEX, 0) - density;
        } else {
            currentCellDensity = 0.0f - density;
        }

        double gravityX;
        double gravityY;
        if (gravityLinear) {
            gravityX = gravityLinearX;
            gravityY = gravityLinearY;
        } else {
            environment.getGravityVector(mPositionX[cellIndex], mPositionY[cellIndex], gravityX, gravityY);
        }
        mForceX[cellIndex] += currentCellMass * currentCellDensity * gravityX;
        mForceY[cellIndex] += currentCellMass * currentCellDensity * gravityY;
    }
}

void CellData::updateCellsMetabolism(double deltaTime, Environment& environment, SimulationWorldProfile* profile) {
    const bool salinityConstant = environment.mSalinityFieldType == Environment::DENSITY_FIELD_CONSTANT;
    const bool salinityLinear = environment.mSalinityFieldType == Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL;
    const double salinityRadiusInv = environment.mRadius != 0.0 ? 1.0 / environment.mRadius : 1.0;
    const double salinityLinearCos = salinityLinear ? std::cos(environment.mSalinityFieldAngle) : 0.0;
    const double salinityLinearSin = salinityLinear ? std::sin(environment.mSalinityFieldAngle) : 0.0;
    const double salinitySoftness = std::max(0.0, std::min(1.0, environment.mSalinityFieldSoftness));
    const bool canMutate = environment.mRadiation != 0.0;

    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        cellularity::GeneView activeGene = getActiveGeneView(targetCellIndex);
        CELLULARITY_PROFILE_IF(profile) {
            profile->metabolismVisitedCells++;
        }

        mNewMass[targetCellIndex] = mMass[targetCellIndex];
        mNewToxin[targetCellIndex] = mToxin[targetCellIndex] - TOXIN_DECAY * mToxin[targetCellIndex] * static_cast<float>(deltaTime);
        mToxication[targetCellIndex] += (std::min(mToxin[targetCellIndex], 10.0f) - mToxication[targetCellIndex])
                * TOXICATION_SPEED * static_cast<float>(deltaTime);

        mNewNitrates[targetCellIndex] = mNitrates[targetCellIndex];
        if (mNitrates[targetCellIndex] < environment.mNitrates) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->metabolismNitrateUptakeCells++;
            }
            mNewNitrates[targetCellIndex] += 2.0 * MAX_NITRATE_FLOW * (environment.mNitrates - mNitrates[targetCellIndex]) * deltaTime;
        }

        if (environment.mIsPreviewMode) {
            if (isCellGrowsInPreview(activeGene.cellType())) {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismPreviewGrowthCells++;
                }
                mNewMass[targetCellIndex] += MASS_PER_AREA * Environment::PREVIEW_SUN
                        * mRadius[targetCellIndex] * mRadius[targetCellIndex] * deltaTime;
            }
        }

        CellType cellType = activeGene.cellType();
        CELLULARITY_PROFILE_IF(profile) {
            profile->metabolismTypeVisits[CellTypes::ordinal(cellType)]++;
        }
        mAge[targetCellIndex] += deltaTime;

        switch (cellType) {
            case CellType::FLAGELLOCYTE: {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismFlagellocytes++;
                }
                float swimForce = getValue(targetCellIndex, Gene::SWIM_FORCE_VALUE_INDEX, 0);
                mNewMass[targetCellIndex] -= environment.mTestFlagellocyteCostScale
                        * swimForce
                        * (0.008 + swimForce * 0.0409756)
                        * deltaTime;
                break;
            }
            case CellType::STEMOCYTE:
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismStemocytes++;
                }
                if (mAge[targetCellIndex] > AGE_OF_CONSENT) {
                    if (getValue(targetCellIndex, Gene::STEM_THRESHOLD_1_VALUE_INDEX, 0) > 0.5f) {
                        setActiveGeneIndex(
                                targetCellIndex,
                                activeGene.virusFromGeneIndex());
                        activeGene = getActiveGeneView(targetCellIndex);
                        cellType = mActiveCellType[targetCellIndex];
                        mAge[targetCellIndex] = 0.0;
                        CELLULARITY_PROFILE_IF(profile) {
                            profile->metabolismStemSwitches++;
                        }
                    } else if (getValue(targetCellIndex, Gene::STEM_THRESHOLD_2_VALUE_INDEX, 0) > 0.5f) {
                        setActiveGeneIndex(
                                targetCellIndex,
                                activeGene.virusToGeneIndex());
                        activeGene = getActiveGeneView(targetCellIndex);
                        cellType = mActiveCellType[targetCellIndex];
                        mAge[targetCellIndex] = 0.0;
                        CELLULARITY_PROFILE_IF(profile) {
                            profile->metabolismStemSwitches++;
                        }
                    }
                }
                break;
            case CellType::MYOCYTE:
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismMyocytes++;
                }
                if (deltaTime != 0.0) {
                    float myoContn = getValue(targetCellIndex, Gene::MYOCYTE_CONTRACTION_VALUE_INDEX, 0);
                    float myoBendn = getValue(targetCellIndex, Gene::MYOCYTE_BENDING_VALUE_INDEX, 0)
                            * (mIsMirrored[targetCellIndex] ? -1.0f : 1.0f);
                    float myoLiftn = getValue(targetCellIndex, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);
                    mNewMass[targetCellIndex] -= MYOCYTE_USE_COST / deltaTime
                            * ((myoContn - mMyocyteContraction[targetCellIndex]) * (myoContn - mMyocyteContraction[targetCellIndex])
                                    + (myoBendn - mMyocyteBending[targetCellIndex]) * (myoBendn - mMyocyteBending[targetCellIndex])
                                    + (myoLiftn - mMyocyteLifting[targetCellIndex]) * (myoLiftn - mMyocyteLifting[targetCellIndex]));
                    mMyocyteContraction[targetCellIndex] = myoContn;
                    mMyocyteBending[targetCellIndex] = myoBendn;
                    mMyocyteLifting[targetCellIndex] = myoLiftn;
                }
                break;
            case CellType::NITROCYTE:
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismNitrocytes++;
                }
                mNewNitrates[targetCellIndex] = MAX_NITRATES;
                break;
            case CellType::PHOTOCYTE:
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismPhotocytes++;
                }
                mNewMass[targetCellIndex] += MASS_PER_AREA * environment.getLight(mPositionX[targetCellIndex], mPositionY[targetCellIndex])
                        * mRadius[targetCellIndex] * mRadius[targetCellIndex] * deltaTime;
                [[fallthrough]];
            case CellType::BUOYOCYTE:
            case CellType::GAMETE:
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismCytoplasmViscosityCells++;
                }
                mAngularVelocityCytoplasm[targetCellIndex] += (mAngularVelocity[targetCellIndex] - mAngularVelocityCytoplasm[targetCellIndex])
                        * std::abs(mAngularVelocity[targetCellIndex] - mAngularVelocityCytoplasm[targetCellIndex])
                        * ANGULAR_VELOCITY_CYTOPLASM_VISCOSITY * deltaTime;
                break;
            case CellType::NEUROCYTE:
                break;
            case CellType::GLUEOCYTE: {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismGlueocytes++;
                }
                mHairRotation[targetCellIndex] += (-mAngularVelocity[targetCellIndex] * mRadius[targetCellIndex]
                                                          - mHairRotation[targetCellIndex] * HAIR_TAU * std::abs(mHairRotation[targetCellIndex]))
                        * deltaTime;
                double hairL = std::sqrt(hairX[targetCellIndex] * hairX[targetCellIndex]
                        + hairY[targetCellIndex] * hairY[targetCellIndex]);
                hairX[targetCellIndex] += (-mVelocityX[targetCellIndex] - hairX[targetCellIndex] * HAIR_TAU * hairL) * deltaTime;
                hairY[targetCellIndex] += (-mVelocityY[targetCellIndex] - hairY[targetCellIndex] * HAIR_TAU * hairL) * deltaTime;
                break;
            }
            case CellType::CILIOCYTE: {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismCiliocytes++;
                }
                double v1 = getValue(targetCellIndex, Gene::MYOCYTE_BENDING_VALUE_INDEX, 0) * deltaTime;
                double v2 = getValue(targetCellIndex, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0) * deltaTime;
                mNewMass[targetCellIndex] -= (std::abs(v1) + std::abs(v2)) * 0.001;
                mSwimAnimationTime[targetCellIndex] += static_cast<float>(v1 * 0.3);
                mSwimAnimationTime2[targetCellIndex] += static_cast<float>(v2 * 0.3);

                double animationSpeedFactor = 45.0 / 0.4;
                double animationPeriodThreshold = 20.0 * std::acos(-1.0);
                while (mSwimAnimationTime[targetCellIndex] * animationSpeedFactor > animationPeriodThreshold) {
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->metabolismCiliocyteAnimationWraps++;
                    }
                    mSwimAnimationTime[targetCellIndex] -= static_cast<float>(animationPeriodThreshold / animationSpeedFactor);
                }
                while (mSwimAnimationTime[targetCellIndex] * animationSpeedFactor < -animationPeriodThreshold) {
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->metabolismCiliocyteAnimationWraps++;
                    }
                    mSwimAnimationTime[targetCellIndex] += static_cast<float>(animationPeriodThreshold / animationSpeedFactor);
                }
                while (mSwimAnimationTime2[targetCellIndex] * animationSpeedFactor > animationPeriodThreshold) {
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->metabolismCiliocyteAnimationWraps++;
                    }
                    mSwimAnimationTime2[targetCellIndex] -= static_cast<float>(animationPeriodThreshold / animationSpeedFactor);
                }
                while (mSwimAnimationTime2[targetCellIndex] * animationSpeedFactor < -animationPeriodThreshold) {
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->metabolismCiliocyteAnimationWraps++;
                    }
                    mSwimAnimationTime2[targetCellIndex] += static_cast<float>(animationPeriodThreshold / animationSpeedFactor);
                }
                break;
            }
            case CellType::SECROCYTE:
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismSecrocytes++;
                }
                if (mNewNitrates[targetCellIndex] > 0.25) {
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->metabolismSecrocyteNitrateDrainCells++;
                    }
                    mNewNitrates[targetCellIndex] -= 0.75 * MAX_NITRATE_FLOW * deltaTime;
                    if (mNewNitrates[targetCellIndex] < 0.25) {
                        mNewNitrates[targetCellIndex] = 0.25;
                    }
                }
                if (mNewNitrates[targetCellIndex] >= MIN_SECRETE_NITRATES) {
                    mSwimAnimationTime[targetCellIndex] += static_cast<float>(deltaTime);
                }
                break;
            case CellType::PHAGOCYTE:
            case CellType::DEVOROCYTE:
            case CellType::LIPOCYTE:
            case CellType::KERATINOCYTE:
            case CellType::VIROCYTE:
            case CellType::STEREOCYTE:
            case CellType::SENSEOCYTE:
                break;
        }

        double finalCellIdleMetabolism = getCellMetabolismDrain(cellType)
                / (1.0 + (mTopology.linkCount(targetCellIndex) * environment.mTestMulticellularCoef));
        double salinity;
        if (salinityConstant) {
            salinity = environment.mSalinityFieldBase;
            salinity = std::max(0.0, std::min(1.0, salinity));
        } else if (salinityLinear) {
            double coordinate = (mPositionX[targetCellIndex] * salinityRadiusInv) * salinityLinearCos
                    + (mPositionY[targetCellIndex] * salinityRadiusInv) * salinityLinearSin
                    - environment.mSalinityFieldOffset;
            coordinate = coordinate / (1.0 + salinitySoftness * std::abs(coordinate) * 2.0);
            salinity = environment.mSalinityFieldBase + environment.mSalinityFieldStrength * coordinate;
            salinity = std::max(0.0, std::min(1.0, salinity));
        } else {
            salinity = environment.getSalinity(mPositionX[targetCellIndex], mPositionY[targetCellIndex]);
        }
        CELLULARITY_PROFILE_IF(profile) {
            profile->metabolismSalinitySamples++;
        }
        mNewMass[targetCellIndex] -= finalCellIdleMetabolism
                * environment.mTestIdleMetabolismScale
                * ((97.0 / 70.0) - (9.0 / 7.0) * salinity)
                * MASS_PER_AREA
                * (mRadius[targetCellIndex] + MAX_CELL_RADIUS * 0.25)
                * mRadius[targetCellIndex]
                * deltaTime;

        if (environment.aging && mAge[targetCellIndex] > MAX_AGE) {
            markCellAsDead(targetCellIndex);
            CELLULARITY_PROFILE_IF(profile) {
                profile->metabolismAgingDeaths++;
            }
        }

        mSeldomCounter[targetCellIndex]++;
        if (mSeldomCounter[targetCellIndex] == MUTATE_PERIOD) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->metabolismMutationPeriodHits++;
            }
            mSeldomCounter[targetCellIndex] = 0;
            if (canMutate
                    && environment.mRandom.nextUniform()
                            < environment.mRadiation * deltaTime
                                    * static_cast<double>(geneCount(targetCellIndex))
                                    * MUTATE_PERIOD / SIMULTANEOUS_MUTATIONS_COUNT) {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismMutationRollHits++;
                }
                for (int mutationIndex = 0; mutationIndex < SIMULTANEOUS_MUTATIONS_COUNT; mutationIndex++) {
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->metabolismMutationAttempts++;
                    }
                    bool mutationRecorded;
                    if (environment.mutationOnlyAfterSplit) {
                        mutationRecorded = recordDeferredGenomeMutation(
                                getGenomeView(targetCellIndex), environment);
                        if (mutationRecorded) {
                            mPendingMutationCount[targetCellIndex]++;
                        }
                    } else {
                        mutationRecorded = mutatePooledGenomeDuringMetabolism(
                                targetCellIndex, environment);
                    }
                    if (mutationRecorded) {
                        CELLULARITY_PROFILE_IF(profile) {
                            profile->metabolismMutationsRecorded++;
                        }
                        refreshActiveCellType(targetCellIndex);
                        activeGene = getActiveGeneView(targetCellIndex);
                        mMutationCount[targetCellIndex]++;
                    }
                }
            }
        }

        for (int colorIndex = 0; colorIndex < 3; colorIndex++) {
            mColor[targetCellIndex][colorIndex] += (activeGene.color(colorIndex) - mColor[targetCellIndex][colorIndex])
                    * COLOR_CHANGE_SPEED * deltaTime;
        }

        double scaledMass = getScaledMass(targetCellIndex);
        double priority = activeGene.massFlowPriority();
        if (activeGene.splitMass() > MAX_MASS
                && scaledMass > MAX_MASS - NO_PRIORITY_MASS_MARGIN) {
            priority = 0.0;
        }
        if (activeGene.staysAlive() && mMass[targetCellIndex] < PANIC_MASS) {
            priority += MAX_MASS_FLOW_PRIORITY / (PANIC_MASS - MIN_MASS) * (PANIC_MASS - mMass[targetCellIndex]);
        }
        mMetabolismScaledMass[targetCellIndex] = scaledMass;
        mMetabolismMassFlowPriority[targetCellIndex] = priority;
    }

    const CellTopology::EdgeView metabolismEdges = mTopology.edgeView();
#ifdef CELLULARITY_PROFILE
    auto metabolismTopologyStartedAt = std::chrono::steady_clock::now();
#endif
    for (int edgeIndex = 0; edgeIndex < metabolismEdges.count; edgeIndex++) {
        CELLULARITY_PROFILE_IF(profile) {
            profile->metabolismLinkIterations++;
            profile->metabolismLinkPairsProcessed++;
            profile->metabolismTopologyEdgeReads += 2;
        }
        const int targetCellIndex = metabolismEdges.cellA[edgeIndex];
        const int linkedCellIndex = metabolismEdges.cellB[edgeIndex];

        double targetCellScaledMass = mMetabolismScaledMass[targetCellIndex];
        double linkedCellScaledMass = mMetabolismScaledMass[linkedCellIndex];
        double targetCellMassFlowPriority = mMetabolismMassFlowPriority[targetCellIndex];
        double linkedCellMassFlowPriority = mMetabolismMassFlowPriority[linkedCellIndex];

        double deltaMass = targetCellScaledMass * linkedCellMassFlowPriority - linkedCellScaledMass * targetCellMassFlowPriority;
        if (deltaMass > MAX_MASS_FLOW) {
            deltaMass = MAX_MASS_FLOW;
        }
        if (deltaMass < -MAX_MASS_FLOW) {
            deltaMass = -MAX_MASS_FLOW;
        }
        double deltaMassStep = deltaMass * deltaTime;
        mNewMass[targetCellIndex] -= deltaMassStep;
        mNewMass[linkedCellIndex] += deltaMassStep;

        double deltaNitrates = mNitrates[targetCellIndex] - mNitrates[linkedCellIndex];
        if (deltaNitrates > MAX_NITRATE_FLOW) {
            deltaNitrates = MAX_NITRATE_FLOW;
        }
        if (deltaNitrates < -MAX_NITRATE_FLOW) {
            deltaNitrates = -MAX_NITRATE_FLOW;
        }
        double deltaNitratesStep = deltaNitrates * deltaTime;
        mNewNitrates[targetCellIndex] -= deltaNitratesStep;
        mNewNitrates[linkedCellIndex] += deltaNitratesStep;

        double toxinStep = TOXIN_FLOW * (mToxin[linkedCellIndex] - mToxin[targetCellIndex]) * deltaTime;
        mNewToxin[targetCellIndex] += toxinStep;
        mNewToxin[linkedCellIndex] -= toxinStep;
    }
#ifdef CELLULARITY_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->metabolismTopologyEdgeLoopNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - metabolismTopologyStartedAt)
                                                            .count();
    }
#endif

    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        if (getActiveGeneView(targetCellIndex).cellType() == CellType::LIPOCYTE) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->metabolismLipidCells++;
            }
            if (mNewMass[targetCellIndex] > MAX_MASS) {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismLipidStoreEvents++;
                }
                mLipids[targetCellIndex] += static_cast<float>(mNewMass[targetCellIndex] - MAX_MASS);
                mNewMass[targetCellIndex] = MAX_MASS;
                if (mLipids[targetCellIndex] > MAX_LIPIDS) {
                    mLipids[targetCellIndex] = MAX_LIPIDS;
                }
            } else if (mLipids[targetCellIndex] > 0.0f) {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismLipidMoveEvents++;
                }
                double move = std::min(static_cast<double>(mLipids[targetCellIndex]), MAX_MASS - mNewMass[targetCellIndex]);
                mNewMass[targetCellIndex] += move;
                mLipids[targetCellIndex] -= static_cast<float>(move);
            }
        } else {
            if (mNewMass[targetCellIndex] > MAX_MASS) {
                CELLULARITY_PROFILE_IF(profile) {
                    profile->metabolismMassClampEvents++;
                }
                mNewMass[targetCellIndex] = MAX_MASS;
            }
        }

        if (mNewNitrates[targetCellIndex] > MAX_NITRATES) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->metabolismNitrateClampEvents++;
            }
            mNewNitrates[targetCellIndex] = MAX_NITRATES;
        }

        if (mNitrates[targetCellIndex] < MIN_NITRATES) {
            markCellAsDead(targetCellIndex);
            CELLULARITY_PROFILE_IF(profile) {
                profile->metabolismNitrateDeaths++;
            }
        }
    }
}

void CellData::setSignalUpdateAlgorithm(
        CellSignalUpdateAlgorithm algorithm) noexcept {
    mSignalUpdateAlgorithm = algorithm;
}

CellSignalUpdateAlgorithm CellData::signalUpdateAlgorithm() const noexcept {
    return mSignalUpdateAlgorithm;
}

void CellData::updateSignals(double deltaTime, SimulationWorldProfile* profile) {
    const float localDeltaTime =
            static_cast<float>(deltaTime / SimulationWorld::SIGNAL_PASSES);
    const float flowStep = SIGNAL_SUBSTANCE_FLOW * localDeltaTime;

    bool hasNeurocytes = false;
    bool hasLipocytes = false;
    {
        CELLULARITY_PROFILE_COARSE_REGION(profile, signalFeatureScanNanos);
        for (int cellIndex = 0; cellIndex < mCellCount; ++cellIndex) {
            const CellType cellType = mActiveCellType[cellIndex];
            hasNeurocytes = hasNeurocytes || cellType == CellType::NEUROCYTE;
            hasLipocytes = hasLipocytes || cellType == CellType::LIPOCYTE;
        }
    }

#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    if (mSignalUpdateAlgorithm == CellSignalUpdateAlgorithm::CELL_OWNED_CSR) {
        bool rebuilt = false;
        {
            CELLULARITY_PROFILE_COARSE_REGION(profile, signalAdjacencyBuildNanos);
            rebuilt = mSignalAdjacency.ensureCurrent(mTopology, mCellCount);
        }
        CELLULARITY_PROFILE_IF(profile) {
            if (rebuilt) {
                profile->signalAdjacencyCacheRebuilds++;
            } else {
                profile->signalAdjacencyCacheHits++;
            }
        }
        (void)rebuilt;

        const CellSignalAdjacency::View adjacency = mSignalAdjacency.view();
        if (hasNeurocytes) {
            if (hasLipocytes) {
                runCellOwnedCsrSignalKernel<true, true>(
                        *this, adjacency, localDeltaTime, flowStep, profile);
            } else {
                runCellOwnedCsrSignalKernel<true, false>(
                        *this, adjacency, localDeltaTime, flowStep, profile);
            }
        } else if (hasLipocytes) {
            runCellOwnedCsrSignalKernel<false, true>(
                    *this, adjacency, localDeltaTime, flowStep, profile);
        } else {
            runCellOwnedCsrSignalKernel<false, false>(
                    *this, adjacency, localDeltaTime, flowStep, profile);
        }
        return;
    }
#endif

    if (hasNeurocytes) {
        if (hasLipocytes) {
            runGlobalEdgeSignalKernel<true, true>(
                    *this, localDeltaTime, flowStep, profile);
        } else {
            runGlobalEdgeSignalKernel<true, false>(
                    *this, localDeltaTime, flowStep, profile);
        }
    } else if (hasLipocytes) {
        runGlobalEdgeSignalKernel<false, true>(
                *this, localDeltaTime, flowStep, profile);
    } else {
        runGlobalEdgeSignalKernel<false, false>(
                *this, localDeltaTime, flowStep, profile);
    }
}

void CellData::handleCellSplit(SimulationWorld& simulationWorld) {
    int initialCellCount = mCellCount;
    for (int parentCellIndex = 0; parentCellIndex < initialCellCount; parentCellIndex++) {
        int child1Index = parentCellIndex;
        if (mIsDead[child1Index]) {
            continue;
        }

        int parentGeneIndex = activeGeneIndex(parentCellIndex).value();
        cellularity::GeneView parentGene = getActiveGeneView(parentCellIndex);
        double effectiveMass = mMass[child1Index];
        if (parentGene.cellType() == CellType::LIPOCYTE) {
            effectiveMass = MIN_MASS + (mMass[child1Index] + mLipids[child1Index] - MIN_MASS) / (MAX_LIPO_MASS - MIN_MASS) * (MAX_MASS - MIN_MASS);
        }

        bool isValidSplitMass = effectiveMass > parentGene.splitMass();
        bool isValidAge = mAge[child1Index] > AGE_OF_CONSENT;
        bool isValidLinksCount = mOldLinkCount[child1Index]
                <= parentGene.integerValue(
                        Gene::MAX_CONNECTION_COUNT_INDEX);
        bool isValidNitrates = mNitrates[child1Index] > 2.0 * MIN_NITRATES;
        bool isNotGamete = parentGene.cellType() != CellType::GAMETE;

        bool canSplit = isValidSplitMass && isValidAge && isValidLinksCount && isValidNitrates && isNotGamete;
        if (!canSplit) {
            continue;
        }

        int child2Index = allocateNewCell();
        if (child2Index == -1) {
            continue;
        }

        const OrganismId inheritedOrganismId = mOrganismIds[child1Index];
        mOrganismIds[child2Index] = inheritedOrganismId;
        mParentOrganismIds[child2Index] =
                mParentOrganismIds[child1Index];

        mPositionX[child2Index] = mPositionX[child1Index];
        mPositionY[child2Index] = mPositionY[child1Index];
        mPositionZ[child2Index] = mPositionZ[child1Index];
        mVelocityX[child2Index] = mVelocityX[child1Index];
        mVelocityY[child2Index] = mVelocityY[child1Index];
        mAngularVelocity[child2Index] = mAngularVelocity[child1Index];
        mAngleCytoplasm[child2Index] = mAngleCytoplasm[child1Index];
        mAngularVelocityCytoplasm[child2Index] = mAngularVelocityCytoplasm[child1Index];
        // Both daughters retain the parent radius until age-gated relaxation.
        mRadius[child2Index] = mRadius[child1Index];
        mRadiusSq[child2Index] = mRadiusSq[child1Index];
        mColor[child2Index] = mColor[child1Index];
        if (!copyGenomeHandle(child1Index, child2Index)) {
            releaseLastCellSlot();
            continue;
        }
        mOriginTag[child2Index] = mOriginTag[child1Index];
        mHairRotation[child2Index] = mHairRotation[child1Index];
        hairX[child2Index] = hairX[child1Index];
        hairY[child2Index] = hairY[child1Index];
        for (int signalIndex = 0; signalIndex < SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
            mSignalSubstance[child2Index][signalIndex] = mSignalSubstance[child1Index][signalIndex];
            mSignalSubstanceOut[child2Index][signalIndex] = mSignalSubstanceOut[child1Index][signalIndex];
        }
        mIsStuck[child2Index] = mIsStuck[child1Index];
        mMyocyteContraction[child2Index] = mMyocyteContraction[child1Index];
        mMyocyteBending[child2Index] = mMyocyteBending[child1Index];
        mMyocyteLifting[child2Index] = mMyocyteLifting[child1Index];
        mSwimAnimationTime[child2Index] = mSwimAnimationTime[child1Index];
        mSwimAnimationTime2[child2Index] = mSwimAnimationTime2[child1Index];
        mForceX[child2Index] = mForceX[child1Index];
        mForceY[child2Index] = mForceY[child1Index];
        mForceZ[child2Index] = mForceZ[child1Index];
        mMutationCount[child2Index] = mMutationCount[child1Index];
        mPendingMutationCount[child2Index] = mPendingMutationCount[child1Index];
        mTelomeresCount[child2Index] = mTelomeresCount[child1Index];

        mSeldomCounter[child2Index] = 0;
        mSeldomCounter[child1Index] = 0;
        mSelectTag[child2Index] = 0;

        double parentAngle = mAngle[child1Index];
        bool parentIsMirrored = mIsMirrored[child1Index];
        double parentRadius = mRadius[child1Index];
        double parentMass = mMass[child1Index];
        float parentLipids = mLipids[child1Index];
        float parentSplitRatio = parentGene.splitRatio();
        float parentToxin = mToxin[child1Index];
        float parentToxication = mToxication[child1Index];
        double parentNitrates = mNitrates[child1Index];
        int parentTelomeresCount = mTelomeresCount[child1Index];
        std::array<CellTopology::CellLinkSnapshot, MAX_NEIGHBORS> splitTempLinks{};
        int parentLinkCount = mTopology.collectLinksOfCell(child1Index, splitTempLinks);
        std::array<double, MAX_NEIGHBORS> splitTempGlobalAngles{};
        std::array<double, MAX_NEIGHBORS> splitTempAxisProjections{};

        double baseAngle = parentAngle
                + parentGene.splitAngle()
                        * (parentIsMirrored ? -1.0f : 1.0f);
        double distance = parentRadius * 0.05f * 2.0f;
        double splitVectorX = std::cos(baseAngle);
        double splitVectorY = std::sin(baseAngle);
        double epsilon = 0.0000001f;

        mPositionX[child2Index] += splitVectorX * distance + std::sin(mCellCount * 12.34f + 13.4f) * epsilon;
        mPositionY[child2Index] += splitVectorY * distance + std::sin(mCellCount * 19.34f + 63.0f) * epsilon;
        mPositionX[child1Index] -= splitVectorX * distance + std::sin(mCellCount * 10.34f + 43.0f) * epsilon;
        mPositionY[child1Index] -= splitVectorY * distance + std::sin(mCellCount * 17.34f + 137.0f) * epsilon;

        double totalMass = parentMass;
        if (parentGene.cellType() == CellType::LIPOCYTE) {
            totalMass += parentLipids;
        }

        mMass[child2Index] = totalMass * parentSplitRatio;
        mMass[child1Index] = totalMass * (1.0f - parentSplitRatio);

        const cellularity::GenomeView parentGenome =
                getGenomeView(child1Index);
        const cellularity::GeneView child1Gene =
                parentGenome.gene(parentGene.child1GeneIndex());
        const cellularity::GeneView child2Gene =
                parentGenome.gene(parentGene.child2GeneIndex());
        mMass[child2Index] -= std::max(
                                      0.0f,
                                      child2Gene.floatValue(
                                              Gene::FloatType::CYTOSKELETON)
                                              - 1.0f)
                * CYTOSKELETON_COST;
        mMass[child1Index] -= std::max(
                                      0.0f,
                                      child1Gene.floatValue(
                                              Gene::FloatType::CYTOSKELETON)
                                              - 1.0f)
                * CYTOSKELETON_COST;

        for (int signalIndex = 0; signalIndex < SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
            float parentSignal = mSignalSubstance[child1Index][signalIndex];
            mSignalSubstance[child2Index][signalIndex] = parentSignal * parentSplitRatio + SimulationWorld::SIGNAL_SUBSTANCE_SPLIT_NUDGE;
            mSignalSubstance[child1Index][signalIndex] = parentSignal * (1.0f - parentSplitRatio) + SimulationWorld::SIGNAL_SUBSTANCE_SPLIT_NUDGE;
        }

        mLipids[child2Index] = 0.0f;
        mLipids[child1Index] = 0.0f;
        mToxin[child2Index] = parentToxin * parentSplitRatio;
        mToxin[child1Index] = parentToxin * (1.0f - parentSplitRatio);
        mToxication[child2Index] = parentToxication * parentSplitRatio;
        mToxication[child1Index] = parentToxication * (1.0f - parentSplitRatio);
        mNitrates[child2Index] = parentNitrates * parentSplitRatio;
        mNitrates[child1Index] = parentNitrates * (1.0f - parentSplitRatio);
        mAge[child2Index] = 0.0f;
        mAge[child1Index] = 0.0f;
        mAngle[child2Index] = baseAngle
                + parentGene.child2Angle()
                        * (parentIsMirrored ? -1.0f : 1.0f);
        mAngle[child1Index] = baseAngle
                + parentGene.child1Angle()
                        * (parentIsMirrored ? -1.0f : 1.0f);
        mIsMirrored[child2Index] =
                parentIsMirrored != parentGene.child2Mirrored();
        mIsMirrored[child1Index] =
                parentIsMirrored != parentGene.child1Mirrored();
        mAngleCytoplasm[child2Index] -= 1.0f;
        mAngleCytoplasm[child1Index] += 1.0f;

        mTopology.removeAllLinksOfCell(child1Index);

        markCellAsAlive(child2Index);
        markCellAsAlive(child1Index);

        if (parentTelomeresCount != -1) {
            mTelomeresCount[child2Index]--;
            mTelomeresCount[child1Index]--;
            if (mTelomeresCount[child1Index] <= 0) {
                markCellAsDead(child2Index);
                markCellAsDead(child1Index);
            }
        }

        if (parentGene.child1KeepsLinks() || parentGene.child2KeepsLinks()) {
            int topmostRightIndex = -1;
            int topmostLeftIndex = -1;
            int bottommostLeftIndex = -1;
            int bottommostRightIndex = -1;
            double maxTopDistRight = -1.0;
            double maxBottomDistRight = -1.0;
            double maxTopDistLeft = 1.0;
            double maxBottomDistLeft = 1.0;
            double rightOffset = 2.0 * (0.5f - parentSplitRatio);

            for (int tempLinkIndex = 0; tempLinkIndex < parentLinkCount; tempLinkIndex++) {
                CellTopology::CellLinkSnapshot& tempLink = splitTempLinks[tempLinkIndex];
                if (tempLink.isBoundary) {
                    continue;
                }

                double globalAngle = tempLink.angleA + parentAngle;
                splitTempGlobalAngles[tempLinkIndex] = globalAngle;
                double cos = std::cos(globalAngle);
                double sin = std::sin(globalAngle);
                double axisProjection = (splitVectorX * cos + splitVectorY * sin) - rightOffset;
                splitTempAxisProjections[tempLinkIndex] = axisProjection;
                if (axisProjection > SimulationWorld::SPLIT_SEAM_TOLERANCE || axisProjection < -SimulationWorld::SPLIT_SEAM_TOLERANCE) {
                    continue;
                }

                double distAlongSplit = (mPositionX[tempLink.connectedCellIndex] - mPositionX[child1Index]) * splitVectorX
                        + (mPositionY[tempLink.connectedCellIndex] - mPositionY[child1Index]) * splitVectorY;
                double distAlongSeam = -splitVectorY * cos + splitVectorX * sin;
                if (distAlongSeam > 0.0) {
                    if (distAlongSplit > maxTopDistRight) {
                        maxTopDistRight = distAlongSplit;
                        topmostRightIndex = tempLinkIndex;
                    }
                    if (distAlongSplit < maxTopDistLeft) {
                        maxTopDistLeft = distAlongSplit;
                        topmostLeftIndex = tempLinkIndex;
                    }
                } else {
                    if (distAlongSplit < maxBottomDistLeft) {
                        maxBottomDistLeft = distAlongSplit;
                        bottommostLeftIndex = tempLinkIndex;
                    }
                    if (distAlongSplit > maxBottomDistRight) {
                        maxBottomDistRight = distAlongSplit;
                        bottommostRightIndex = tempLinkIndex;
                    }
                }
            }

            for (int tempLinkIndex = 0; tempLinkIndex < parentLinkCount; tempLinkIndex++) {
                CellTopology::CellLinkSnapshot& tempLink = splitTempLinks[tempLinkIndex];
                if (tempLink.isBoundary) {
                    continue;
                }

                double globalAngle = splitTempGlobalAngles[tempLinkIndex];
                double axisProjection = splitTempAxisProjections[tempLinkIndex];
                if (parentGene.child2KeepsLinks()) {
                    bool belongsToChild2 = axisProjection > SimulationWorld::SPLIT_SEAM_TOLERANCE
                            || (axisProjection > -SimulationWorld::SPLIT_SEAM_TOLERANCE
                                    && (tempLinkIndex == topmostRightIndex || tempLinkIndex == bottommostRightIndex));
                    if (belongsToChild2 && isCellCanMakeLink(child2Index) && isCellCanMakeLink(tempLink.connectedCellIndex)) {
                        addLink(child2Index,
                                tempLink.connectedCellIndex,
                                inheritedCellLinkReason(
                                        tempLink.creationReason),
                                globalAngle - mAngle[child2Index],
                                tempLink.angleB,
                                tempLink.torsionalStiffness,
                                tempLink.restLength);
                    }
                }

                if (parentGene.child1KeepsLinks()) {
                    bool belongsToChild1 = axisProjection < -SimulationWorld::SPLIT_SEAM_TOLERANCE
                            || (axisProjection < SimulationWorld::SPLIT_SEAM_TOLERANCE
                                    && (tempLinkIndex == topmostLeftIndex || tempLinkIndex == bottommostLeftIndex));
                    if (belongsToChild1 && isCellCanMakeLink(child1Index) && isCellCanMakeLink(tempLink.connectedCellIndex)) {
                        addLink(child1Index,
                                tempLink.connectedCellIndex,
                                inheritedCellLinkReason(
                                        tempLink.creationReason),
                                globalAngle - mAngle[child1Index],
                                tempLink.angleB,
                                tempLink.torsionalStiffness,
                                tempLink.restLength);
                    }
                }
            }
        }

        if (parentGene.makesLink()
                && isCellCanMakeLink(child1Index)
                && isCellCanMakeLink(child2Index)) {
            addLink(child1Index,
                    child2Index,
                    CellLinkCreationReason::SPLIT_SIBLING,
                    baseAngle - mAngle[child1Index],
                    std::acos(-1.0) + baseAngle - mAngle[child2Index],
                    parentGene.linkStrength(),
                    parentGene.floatValue(Gene::FloatType::ADHESIN_LENGTH));
        }

        resolveSplitOrganismIdentity(
                child1Index,
                child2Index,
                inheritedOrganismId,
                splitTempLinks,
                parentLinkCount);

        mGeneIndices[static_cast<std::size_t>(child2Index)] =
                parentGene.child2GeneIndex();
        mGeneIndices[static_cast<std::size_t>(child1Index)] =
                parentGene.child1GeneIndex();
        refreshActiveCellType(child1Index);
        refreshActiveCellType(child2Index);
        simulationWorld.recordSplitEvent(parentCellIndex,
                child1Index,
                child2Index,
                parentGeneIndex,
                activeGeneIndex(child1Index).value(),
                activeGeneIndex(child2Index).value());
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::CellSplit,
                cellularity::verification::traceI32(parentCellIndex),
                cellularity::verification::traceI32(child1Index),
                cellularity::verification::traceI32(child2Index),
                cellularity::verification::traceI32(parentGeneIndex),
                cellularity::verification::traceI32(
                        activeGeneIndex(child1Index).value()),
                cellularity::verification::traceI32(
                        activeGeneIndex(child2Index).value()),
                cellularity::verification::traceI32(mCellCount));

        const cellularity::GeneView child1ActiveGene =
                getActiveGeneView(child1Index);
        const cellularity::GeneView child2ActiveGene =
                getActiveGeneView(child2Index);

        if (mMass[child1Index] > MAX_MASS) {
            if (child1ActiveGene.cellType() == CellType::LIPOCYTE) {
                mLipids[child1Index] = static_cast<float>(mMass[child1Index] - MAX_MASS);
            }
            mMass[child1Index] = MAX_MASS;
        }

        if (mMass[child2Index] > MAX_MASS) {
            if (child2ActiveGene.cellType() == CellType::LIPOCYTE) {
                mLipids[child2Index] = static_cast<float>(mMass[child2Index] - MAX_MASS);
            }
            mMass[child2Index] = MAX_MASS;
        }

        if (child1ActiveGene.cellType() == CellType::FLAGELLOCYTE) {
            std::array<CellTopology::CellLinkSnapshot, MAX_NEIGHBORS> childLinks{};
            int childLinkCount = mTopology.collectLinksOfCell(child1Index, childLinks);
            for (int linkIndex = 0; linkIndex < childLinkCount; linkIndex++) {
                if (std::cos(childLinks[linkIndex].angleA) >= -0.8) {
                    continue;
                }
                markCellAsDead(child1Index);
            }
        }

        if (child2ActiveGene.cellType() == CellType::FLAGELLOCYTE) {
            std::array<CellTopology::CellLinkSnapshot, MAX_NEIGHBORS> childLinks{};
            int childLinkCount = mTopology.collectLinksOfCell(child2Index, childLinks);
            for (int linkIndex = 0; linkIndex < childLinkCount; linkIndex++) {
                if (std::cos(childLinks[linkIndex].angleA) >= -0.8) {
                    continue;
                }
                markCellAsDead(child2Index);
            }
        }

        if (mMass[child2Index] <= 0.0) {
            markCellAsDead(child2Index);
        }

        if (mMass[child1Index] <= 0.0) {
            markCellAsDead(child1Index);
        }

        if (simulationWorld.mEnvironment.mutationOnlyAfterSplit && mPendingMutationCount[child1Index] > 0) {
            int pendingMutationCount = mPendingMutationCount[child1Index];
            for (int mutationIndex = 0;
                    mutationIndex < pendingMutationCount;
                    ++mutationIndex) {
                mutatePooledGenomeDuringMetabolism(
                        child1Index, simulationWorld.mEnvironment);
            }
            const cellularity::GeneIndex child2ActiveIndex =
                    activeGeneIndex(child2Index);
            if (!copyGenomeHandle(child1Index, child2Index)) {
                throw std::runtime_error(
                        "Failed to share a mutated split Genome");
            }
            setActiveGeneIndex(child2Index, child2ActiveIndex);
            mPendingMutationCount[child1Index] = 0;
            mPendingMutationCount[child2Index] = 0;
            refreshActiveCellType(child1Index);
            refreshActiveCellType(child2Index);
        }
    }
}

bool CellData::applyCellFertilization(int spermIndex, int eggIndex, Environment& environment) {
    const std::size_t eggGeneCount = geneCount(eggIndex);
    const std::size_t spermGeneCount = geneCount(spermIndex);
    const auto eggMetadata = mGenomePool.metadata(
            mGenomeIds[static_cast<std::size_t>(eggIndex)]);
    if (!cellularity::isSupportedGeneCount(eggGeneCount)
            || !cellularity::isSupportedGeneCount(spermGeneCount)
            || !eggMetadata.has_value()) {
        return false;
    }

    const cellularity::GeneView eggActiveGene = getActiveGeneView(eggIndex);
    const cellularity::GeneIndex offspringActiveGeneIndex =
            eggActiveGene.virusFromGeneIndex();
    const int offspringTelomeres = Gene::TELOMERASE_LENGTHS[eggActiveGene.integerValue(Gene::TELOMERASE_LENGTH_INDEX)];
    cellularity::GenomeData eggGenome = materializeGenome(eggIndex);
    const cellularity::GenomeData spermGenome = materializeGenome(spermIndex);
    const cellularity::GenomeView spermGenomeView(
            spermGenome.data(), spermGenome.size());
    const std::size_t crossoverGeneCount =
            std::min(eggGeneCount, spermGeneCount);

    markCellAsDead(spermIndex);
    clearCellCollisionContacts(spermIndex);
    clearCellCollisionContacts(eggIndex);
    mAge[eggIndex] = 0.0;

    mMass[eggIndex] += mMass[spermIndex];
    if (mMass[eggIndex] > MAX_MASS) {
        mMass[eggIndex] = MAX_MASS;
    }
    mMass[spermIndex] = 0.0;

    mNitrates[eggIndex] += mNitrates[spermIndex];
    if (mNitrates[eggIndex] > MAX_NITRATES) {
        mNitrates[eggIndex] = MAX_NITRATES;
    }
    mNitrates[spermIndex] = 0.0;

    if (mOriginTag[spermIndex] != mOriginTag[eggIndex]) {
        mOriginTag[eggIndex] = SimulationWorld::HYBRID_CELL_ORIGIN;
    }

    mTelomeresCount[eggIndex] = offspringTelomeres;
    for (std::size_t geneIndex = 0;
            geneIndex < crossoverGeneCount;
            geneIndex++) {
        if (environment.mRandom.nextInt(2) != 1) {
            continue;
        }
        const cellularity::GeneIndex sourceOwner(
                static_cast<cellularity::GeneIndex::Value>(geneIndex));
        if (!copyGeneAcrossGenomeDomains(
                    eggGenome[geneIndex],
                    spermGenomeView.gene(geneIndex),
                    sourceOwner,
                    spermGeneCount,
                    eggGeneCount)) {
            throw std::logic_error(
                    "Validated fertilization reference remapping failed");
        }
    }
    if (!cellularity::applyGenomeMetadataToCompatibilityFlags(
                eggGenome.data(), eggGenome.size(), *eggMetadata)) {
        throw std::logic_error(
                "Validated fertilization metadata restoration failed");
    }
    if (!replaceGenome(
                eggIndex, eggGenome, offspringActiveGeneIndex)) {
        throw std::runtime_error("Failed to install a fertilized Genome");
    }
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::FertilizationApply,
            cellularity::verification::traceI32(spermIndex),
            cellularity::verification::traceI32(eggIndex),
            cellularity::verification::traceBool(mIsDead[spermIndex]),
            cellularity::verification::traceF64(mMass[spermIndex]),
            cellularity::verification::traceF64(mMass[eggIndex]),
            cellularity::verification::traceF64(mNitrates[spermIndex]),
            cellularity::verification::traceF64(mNitrates[eggIndex]),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[eggIndex])),
            cellularity::verification::traceI64(environment.mRandom.getSeed()));
    return true;
}

bool CellData::resolveFertilizationIntents(
        Environment& environment,
        SimulationWorldProfile* profile) {
    bool appliedFertilization = false;
    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        int victimCellIndex = mFertilizationTarget[targetCellIndex];

        if (victimCellIndex == -1) {
            continue;
        }

        if (mFertilizationTarget[victimCellIndex] != targetCellIndex) {
            CELLULARITY_SEMANTIC_EVENT(
                    cellularity::verification::TraceChannel::Apply,
                    cellularity::verification::SemanticEventKind::FertilizationResolve,
                    cellularity::verification::traceI32(targetCellIndex),
                    cellularity::verification::traceI32(victimCellIndex),
                    cellularity::verification::traceBool(false));
            continue;
        }

        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::FertilizationResolve,
                cellularity::verification::traceI32(targetCellIndex),
                cellularity::verification::traceI32(victimCellIndex),
                cellularity::verification::traceBool(true));

        int spermIndex;
        int eggIndex;
        if (environment.mRandom.nextInt(2) == 0) {
            spermIndex = targetCellIndex;
            eggIndex = victimCellIndex;
        } else {
            spermIndex = victimCellIndex;
            eggIndex = targetCellIndex;
        }

        appliedFertilization =
                applyCellFertilization(spermIndex, eggIndex, environment)
                || appliedFertilization;

        mFertilizationTarget[targetCellIndex] = -1;
        mFertilizationTarget[victimCellIndex] = -1;
    }
    if (appliedFertilization) {
        removeCollisionContactsTargetingDeadCells(profile);
    }
    return appliedFertilization;
}

void CellData::updateCellVelocities(const Environment& environment, double deltaTime) {
    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        if (mIsDead[targetCellIndex]) {
            continue;
        }

        double cellMass = mMass[targetCellIndex];
        double inertia = cellMass * mRadius[targetCellIndex] * mRadius[targetCellIndex] * 0.7;
        double deltaForceX = mForceX[targetCellIndex] / cellMass * deltaTime;
        double deltaForceY = mForceY[targetCellIndex] / cellMass * deltaTime;
        double deltaTorque = mTorque[targetCellIndex] * deltaTime / inertia;

        mPositionZ[targetCellIndex] += mForceZ[targetCellIndex] * deltaTime * 10.0f;

        double coef = 1.0 - mPositionZ[targetCellIndex] * 3.0f;
        if (coef < 0.0) {
            coef = 0.0;
        }
        if (coef > 4.0) {
            coef = 4.0;
        }

        double weight = cellMass * coef;
        if (mIsStuck[targetCellIndex]) {
            double frictionStatic = environment.mFrictionStatic * weight;
            if (deltaForceX * deltaForceX + deltaForceY * deltaForceY > frictionStatic * frictionStatic * deltaTime * deltaTime
                    || std::abs(deltaTorque) > deltaTime * frictionStatic * (2.0 / 3.0)) {
                mIsStuck[targetCellIndex] = false;
            }
        }

        if (!mIsStuck[targetCellIndex]) {
            double newVelocityX = mVelocityX[targetCellIndex] + deltaForceX;
            double newVelocityY = mVelocityY[targetCellIndex] + deltaForceY;
            double newAngularVelocity = mAngularVelocity[targetCellIndex] + deltaTorque;
            double velocityMagnitude = std::sqrt(newVelocityX * newVelocityX + newVelocityY * newVelocityY);
            double frictionForceFactor;
            double frictionTorqueFactor;
            if (mAngularVelocity[targetCellIndex] != 0.0) {
                double epsilon = velocityMagnitude / (mRadius[targetCellIndex] * std::abs(mAngularVelocity[targetCellIndex]));
                double epsilon2 = epsilon * epsilon;
                frictionForceFactor = (epsilon2 + 0.1 * epsilon) / (epsilon2 + 0.1 * epsilon + 0.15);
                frictionTorqueFactor = 0.65 * (1.0 - (epsilon2 + epsilon) / (epsilon + epsilon2 + 5.0));
                if (std::isnan(frictionForceFactor) || std::isnan(frictionTorqueFactor)) {
                    frictionForceFactor = 1.0;
                    frictionTorqueFactor = 0.0;
                }
            } else {
                frictionForceFactor = 1.0;
                frictionTorqueFactor = 0.0;
            }

            double frictionDynamic = frictionForceFactor * environment.mFrictionDynamic * weight;
            double angularFriction = frictionTorqueFactor * environment.mFrictionDynamic * cellMass;

            double frictionVelocity = frictionDynamic / cellMass * deltaTime;
            double angularFrictionVelocity = angularFriction / inertia * deltaTime;
            bool couldBeStuck = false;
            if (newVelocityX * newVelocityX + newVelocityY * newVelocityY <= frictionVelocity * frictionVelocity) {
                mVelocityX[targetCellIndex] = 0.0;
                mVelocityY[targetCellIndex] = 0.0;
                couldBeStuck = true;
            } else {
                mVelocityX[targetCellIndex] = newVelocityX - newVelocityX * frictionVelocity / velocityMagnitude;
                mVelocityY[targetCellIndex] = newVelocityY - newVelocityY * frictionVelocity / velocityMagnitude;
            }

            if (std::abs(newAngularVelocity) <= angularFrictionVelocity) {
                mAngularVelocity[targetCellIndex] = 0.0;
                if (couldBeStuck) {
                    mIsStuck[targetCellIndex] = true;
                }
            } else {
                mAngularVelocity[targetCellIndex] = newAngularVelocity - angularFrictionVelocity * signum(mAngularVelocity[targetCellIndex]);
            }
        }

        mForceX[targetCellIndex] = mVelocityX[targetCellIndex];
        mForceY[targetCellIndex] = mVelocityY[targetCellIndex];
        mTorque[targetCellIndex] = mAngularVelocity[targetCellIndex];
    }
}

void CellData::updateCellsKinematics(int targetCellIndex, double deltaTime) {
    if (mIsDead[targetCellIndex]) {
        if (mSelectTag[targetCellIndex] < CELL_AUX2_DRAG_START) {
            mPositionX[targetCellIndex] += deltaTime * mVelocityX[targetCellIndex];
            mPositionY[targetCellIndex] += deltaTime * mVelocityY[targetCellIndex];
        }
        return;
    }

    if (mSelectTag[targetCellIndex] < CELL_AUX2_DRAG_START) {
        mPositionX[targetCellIndex] += deltaTime * mVelocityX[targetCellIndex];
        mPositionY[targetCellIndex] += deltaTime * mVelocityY[targetCellIndex];
        mAngle[targetCellIndex] += mAngularVelocity[targetCellIndex] * deltaTime;
    }

    if (mAge[targetCellIndex] > deltaTime * 5.0) {
        mRadius[targetCellIndex] += (mMass[targetCellIndex] / MASS_PER_AREA / mRadius[targetCellIndex] - mRadius[targetCellIndex])
                * 0.5 * RADIUS_CHANGE_SPEED * deltaTime;
        if (mRadius[targetCellIndex] > MAX_CELL_RADIUS) {
            mRadius[targetCellIndex] = MAX_CELL_RADIUS;
        }
        refreshRadiusSq(targetCellIndex);
    }

    if (mRadius[targetCellIndex] > MAX_CELL_RADIUS
            || mRadius[targetCellIndex] < MIN_CELL_RADIUS
            || mMass[targetCellIndex] < MIN_MASS
            || mToxication[targetCellIndex] > 1.0f) {
        markCellAsDead(targetCellIndex);
    }

    mAngleCytoplasm[targetCellIndex] += mAngularVelocityCytoplasm[targetCellIndex] * deltaTime;

    if (mAngle[targetCellIndex] > PI) {
        mAngle[targetCellIndex] -= TWO_PI;
    }

    if (mAngle[targetCellIndex] < -PI) {
        mAngle[targetCellIndex] += TWO_PI;
    }

    if (mAngleCytoplasm[targetCellIndex] > TWO_PI) {
        mAngleCytoplasm[targetCellIndex] -= FOUR_PI;
    }

    if (mAngleCytoplasm[targetCellIndex] < -TWO_PI) {
        mAngleCytoplasm[targetCellIndex] += FOUR_PI;
    }

    mOldLinkCount[targetCellIndex] = mTopology.linkCount(targetCellIndex);
}

void CellData::updateCellsKinematics(double deltaTime) {
    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        updateCellsKinematics(targetCellIndex, deltaTime);
    }
}

void CellData::updateCellAngleDirectionCache(int cellIndex) {
    double angle = mAngle[cellIndex];
    if (mCachedAngle[cellIndex] == angle) {
        return;
    }
    mCachedAngle[cellIndex] = angle;
    mCachedAngleCos[cellIndex] = std::cos(angle);
    mCachedAngleSin[cellIndex] = std::sin(angle);
}

void CellData::updateAllCellAngleDirectionCaches() {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        updateCellAngleDirectionCache(cellIndex);
    }
}

void CellData::updateAllCellLinkInteractions(double deltaTime, SimulationWorldProfile* profile, bool updateRenderOffsets) {
    CELLULARITY_PROFILE_IF(profile) {
        auto angleCacheStartedAt = std::chrono::steady_clock::now();
        updateAllCellAngleDirectionCaches();
        auto angleCacheNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - angleCacheStartedAt);
        profile->linkAngleCacheNanos += angleCacheNanos.count();
    }
    else {
        updateAllCellAngleDirectionCaches();
    }

    CELLULARITY_PROFILE_IF(profile) {
        auto countScanStartedAt = std::chrono::steady_clock::now();
        const int* linkCounts = mTopology.cellLinkCounts();
        for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
            const int targetLinkCount = linkCounts[cellIndex];
            profile->linkTopologyLinkCountQueries++;
            profile->linkTargetCells++;
            profile->linkTotalLinks += targetLinkCount;
            if (targetLinkCount > 0) {
                profile->linkCellsWithLinks++;
            }
        }
        profile->linkTopologyCountScanNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - countScanStartedAt)
                                                       .count();
    }

#ifdef CELLULARITY_PROFILE
    auto boundaryLoopStartedAt = std::chrono::steady_clock::now();
#endif
    const CellTopology::BoundaryView boundaries = mTopology.boundaryView();
    for (int boundaryIndex = 0; boundaryIndex < mTopology.boundaryCount(); boundaryIndex++) {
        const int targetCellIndex = boundaries.cell[boundaryIndex];
        CELLULARITY_PROFILE_IF(profile) {
            profile->linkBoundaryLinks++;
        }

        const double targetCellRadius = mRadius[targetCellIndex];
        const double targetCellAngleCos = mCachedAngleCos[targetCellIndex];
        const double targetCellAngleSin = mCachedAngleSin[targetCellIndex];
        const double angleOffsetCos = boundaries.angleACos[boundaryIndex];
        const double angleOffsetSin = boundaries.angleASin[boundaryIndex];
        const double attachDirX = targetCellAngleCos * angleOffsetCos - targetCellAngleSin * angleOffsetSin;
        const double attachDirY = targetCellAngleSin * angleOffsetCos + targetCellAngleCos * angleOffsetSin;

        if (updateRenderOffsets) {
            const float renderStartOffsetX = static_cast<float>(attachDirX * (targetCellRadius - MIN_CELL_RADIUS) * 0.8);
            const float renderStartOffsetY = static_cast<float>(attachDirY * (targetCellRadius - MIN_CELL_RADIUS) * 0.8);
            mTopology.setBoundaryRenderStartOffset(boundaryIndex, renderStartOffsetX, renderStartOffsetY);
            CELLULARITY_PROFILE_IF(profile) {
                profile->linkRenderOffsetWrites++;
            }
        }

        const double targetCellAttachOffsetX = attachDirX * (targetCellRadius * (1.0 - LINK_ATTACH_DEPTH));
        const double targetCellAttachOffsetY = attachDirY * (targetCellRadius * (1.0 - LINK_ATTACH_DEPTH));
        const double distanceToAnchorX = boundaries.anchorX[boundaryIndex] - targetCellAttachOffsetX - mPositionX[targetCellIndex];
        const double distanceToAnchorY = boundaries.anchorY[boundaryIndex] - targetCellAttachOffsetY - mPositionY[targetCellIndex];
        const double sqrDistance = distanceToAnchorX * distanceToAnchorX + distanceToAnchorY * distanceToAnchorY;
        const double maxTearDistanceSqr = targetCellRadius * targetCellRadius * LINK_MARGIN * LINK_MARGIN;
        if (sqrDistance > maxTearDistanceSqr) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->linkRemovedBoundaryLinks++;
            }
            mTopology.removeBoundaryLink(boundaryIndex);
            boundaryIndex--;
            continue;
        }

        const double targetCellVelocityAtPointX = mVelocityX[targetCellIndex] + mAngularVelocity[targetCellIndex] * -targetCellAttachOffsetY;
        const double targetCellVelocityAtPointY = mVelocityY[targetCellIndex] + mAngularVelocity[targetCellIndex] * targetCellAttachOffsetX;
        const double baseDamping = LINK_SUBSTRATE_DAMPING * targetCellRadius;
        const double safeDamping = baseDamping * (1.0 - 1.0 * deltaTime * baseDamping * 0.5 / mMass[targetCellIndex]);
        const double forceX = distanceToAnchorX * LINK_SPRING_COEFFICIENT * 3.0
                - targetCellVelocityAtPointX * safeDamping;
        const double forceY = distanceToAnchorY * LINK_SPRING_COEFFICIENT * 3.0
                - targetCellVelocityAtPointY * safeDamping;
        mForceX[targetCellIndex] += forceX;
        mForceY[targetCellIndex] += forceY;
        mTorque[targetCellIndex] += -forceX * targetCellAttachOffsetY + forceY * targetCellAttachOffsetX;
    }
#ifdef CELLULARITY_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->linkBoundaryLoopNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - boundaryLoopStartedAt)
                                                  .count();
    }
#endif

#ifdef CELLULARITY_PROFILE
    auto normalLoopStartedAt = std::chrono::steady_clock::now();
#endif
    const CellTopology::EdgeView edges = mTopology.edgeView();
    const int* linkCounts = mTopology.cellLinkCounts();
    for (int edgeIndex = 0; edgeIndex < mTopology.edgeCount(); edgeIndex++) {
        const int targetCellIndex = edges.cellA[edgeIndex];
        const int linkedCellIndex = edges.cellB[edgeIndex];
        CELLULARITY_PROFILE_IF(profile) {
            profile->linkNormalLinks++;
        }

        const CellType targetCellType = mActiveCellType[targetCellIndex];
        const CellType linkedCellType = mActiveCellType[linkedCellIndex];
        const double targetCellRadius = mRadius[targetCellIndex];
        const double linkedCellRadius = mRadius[linkedCellIndex];
        const double minRadius = std::min(targetCellRadius, linkedCellRadius);
        const double maxRadius = std::max(targetCellRadius, linkedCellRadius);
        double penetrationDepth = minRadius * LINK_ATTACH_DEPTH;
        double targetAngleOffset = edges.angleA[edgeIndex];
        double linkedAngleOffset = edges.angleB[edgeIndex];
        double forceZ = -mPositionZ[targetCellIndex] + mPositionZ[linkedCellIndex];

        const double baseTargetAngleCos = edges.angleACos[edgeIndex];
        const double baseTargetAngleSin = edges.angleASin[edgeIndex];
        const double baseLinkedAngleCos = edges.angleBCos[edgeIndex];
        const double baseLinkedAngleSin = edges.angleBSin[edgeIndex];

        if (targetCellType == CellType::MYOCYTE) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->linkTargetMyocyteBranches++;
            }
            forceZ -= baseTargetAngleCos * mMyocyteLifting[targetCellIndex];
            penetrationDepth += baseTargetAngleCos * baseTargetAngleCos * targetCellRadius * mMyocyteContraction[targetCellIndex];
            targetAngleOffset += baseTargetAngleCos * mMyocyteBending[targetCellIndex];
        }
        if (linkedCellType == CellType::MYOCYTE) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->linkLinkedMyocyteBranches++;
            }
            forceZ += baseLinkedAngleCos * mMyocyteLifting[linkedCellIndex];
            penetrationDepth += baseLinkedAngleCos * baseLinkedAngleCos * linkedCellRadius * mMyocyteContraction[linkedCellIndex];
            linkedAngleOffset += baseLinkedAngleCos * mMyocyteBending[linkedCellIndex];
        }
        if (penetrationDepth < 0.0) {
            penetrationDepth = 0.0;
        }
        if (penetrationDepth > minRadius) {
            penetrationDepth = minRadius;
        }

        const double targetOffsetCos = targetCellType == CellType::MYOCYTE ? std::cos(targetAngleOffset) : baseTargetAngleCos;
        const double targetOffsetSin = targetCellType == CellType::MYOCYTE ? std::sin(targetAngleOffset) : baseTargetAngleSin;
        const double linkedOffsetCos = linkedCellType == CellType::MYOCYTE ? std::cos(linkedAngleOffset) : baseLinkedAngleCos;
        const double linkedOffsetSin = linkedCellType == CellType::MYOCYTE ? std::sin(linkedAngleOffset) : baseLinkedAngleSin;

        double targetCellAttachDirX = mCachedAngleCos[targetCellIndex] * targetOffsetCos - mCachedAngleSin[targetCellIndex] * targetOffsetSin;
        double targetCellAttachDirY = mCachedAngleSin[targetCellIndex] * targetOffsetCos + mCachedAngleCos[targetCellIndex] * targetOffsetSin;
        double linkedCellAttachDirX = mCachedAngleCos[linkedCellIndex] * linkedOffsetCos - mCachedAngleSin[linkedCellIndex] * linkedOffsetSin;
        double linkedCellAttachDirY = mCachedAngleSin[linkedCellIndex] * linkedOffsetCos + mCachedAngleCos[linkedCellIndex] * linkedOffsetSin;

        if (updateRenderOffsets) {
            const float startOffsetX = static_cast<float>(targetCellAttachDirX * (targetCellRadius - MIN_CELL_RADIUS) * 0.8);
            const float startOffsetY = static_cast<float>(targetCellAttachDirY * (targetCellRadius - MIN_CELL_RADIUS) * 0.8);
            const float endOffsetX = static_cast<float>(linkedCellAttachDirX * (linkedCellRadius - MIN_CELL_RADIUS) * 0.8);
            const float endOffsetY = static_cast<float>(linkedCellAttachDirY * (linkedCellRadius - MIN_CELL_RADIUS) * 0.8);
            mTopology.setEdgeRenderOffsets(edgeIndex, startOffsetX, startOffsetY, endOffsetX, endOffsetY);
            CELLULARITY_PROFILE_IF(profile) {
                profile->linkRenderOffsetWrites++;
            }
        }

        const float restLength = edges.restLength[edgeIndex];
        targetCellAttachDirX *= targetCellRadius - penetrationDepth + restLength;
        targetCellAttachDirY *= targetCellRadius - penetrationDepth + restLength;
        linkedCellAttachDirX *= linkedCellRadius - penetrationDepth + restLength;
        linkedCellAttachDirY *= linkedCellRadius - penetrationDepth + restLength;

        const double distanceX = mPositionX[linkedCellIndex] + linkedCellAttachDirX - mPositionX[targetCellIndex] - targetCellAttachDirX;
        const double distanceY = mPositionY[linkedCellIndex] + linkedCellAttachDirY - mPositionY[targetCellIndex] - targetCellAttachDirY;
        const double sqrDistance = distanceX * distanceX + distanceY * distanceY;
        const double maxTearDistanceSqr = maxRadius * maxRadius * LINK_MARGIN * LINK_MARGIN;
        if (sqrDistance > maxTearDistanceSqr) {
            CELLULARITY_PROFILE_IF(profile) {
                profile->linkRemovedNormalLinks++;
            }
            mTopology.removeCellLink(edgeIndex);
            edgeIndex--;
            continue;
        }

        const double xv1 = mVelocityX[targetCellIndex] + mAngularVelocity[targetCellIndex] * -targetCellAttachDirY;
        const double yv1 = mVelocityY[targetCellIndex] + mAngularVelocity[targetCellIndex] * targetCellAttachDirX;
        const double xv2 = mVelocityX[linkedCellIndex] + mAngularVelocity[linkedCellIndex] * -linkedCellAttachDirY;
        const double yv2 = mVelocityY[linkedCellIndex] + mAngularVelocity[linkedCellIndex] * linkedCellAttachDirX;
        const double strainStiffnessMultiplier = 1.0 + 2.0 * sqrDistance / (MAX_CELL_RADIUS * MAX_CELL_RADIUS * LINK_MARGIN * LINK_MARGIN);
        const double baseStiffness = LINK_SPRING_COEFFICIENT * MIN_CELL_RADIUS / (MIN_CELL_RADIUS + 2.0 * restLength);
        const int maxLinkCount = std::max(linkCounts[targetCellIndex], linkCounts[linkedCellIndex]);
        const double reducedMassDamping = (LINK_INTERCELL_DAMPING * mMass[targetCellIndex] * mMass[linkedCellIndex]
                                                  / (mMass[targetCellIndex] + mMass[linkedCellIndex]))
                / maxLinkCount;
        const double forceX = distanceX * strainStiffnessMultiplier * baseStiffness + (xv2 - xv1) * reducedMassDamping;
        const double forceY = distanceY * strainStiffnessMultiplier * baseStiffness + (yv2 - yv1) * reducedMassDamping;

        mForceX[targetCellIndex] += forceX;
        mForceY[targetCellIndex] += forceY;
        mForceZ[targetCellIndex] += forceZ;
        mForceX[linkedCellIndex] -= forceX;
        mForceY[linkedCellIndex] -= forceY;
        mForceZ[linkedCellIndex] -= forceZ;

        const double midPointX = 0.5 * (mPositionX[targetCellIndex] + targetCellAttachDirX + mPositionX[linkedCellIndex] + linkedCellAttachDirX);
        const double midPointY = 0.5 * (mPositionY[targetCellIndex] + targetCellAttachDirY + mPositionY[linkedCellIndex] + linkedCellAttachDirY);
        mTorque[targetCellIndex] += -forceX * (midPointY - mPositionY[targetCellIndex]) + forceY * (midPointX - mPositionX[targetCellIndex]);
        mTorque[linkedCellIndex] -= -forceX * (midPointY - mPositionY[linkedCellIndex]) + forceY * (midPointX - mPositionX[linkedCellIndex]);

        const double torsionCrossProduct = targetCellAttachDirY * linkedCellAttachDirX - targetCellAttachDirX * linkedCellAttachDirY;
        const double torsionTorque = signum(torsionCrossProduct) * std::sqrt(std::abs(torsionCrossProduct)) * minRadius * edges.torsionalStiffness[edgeIndex];
        mTorque[targetCellIndex] += torsionTorque;
        mTorque[linkedCellIndex] -= torsionTorque;
    }
#ifdef CELLULARITY_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->linkNormalLoopNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - normalLoopStartedAt)
                                                .count();
    }
#endif
}

void CellData::resetCellForces() {
    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        if (mIsDead[targetCellIndex]) {
            mTorque[targetCellIndex] = 0.0;
            continue;
        }
        mForceX[targetCellIndex] = 0.0;
        mForceY[targetCellIndex] = 0.0;
        mTorque[targetCellIndex] = 0.0;
        mForceZ[targetCellIndex] = -mPositionZ[targetCellIndex] * 0.3f;
    }
}

void CellData::updateFlagellocytesForce(double deltaTime, const Environment& environment) {
    for (int targetCellIndex = 0; targetCellIndex < mCellCount; targetCellIndex++) {
        if (mActiveCellType[targetCellIndex] != CellType::FLAGELLOCYTE) {
            continue;
        }

        float swimForce = getValue(targetCellIndex, Gene::SWIM_FORCE_VALUE_INDEX, 0);
        mSwimAnimationTime[targetCellIndex] += static_cast<float>(swimForce * deltaTime);

        float animationSpeedFactor = 45.0f / 0.4f;
        float animationPeriodThreshold = 10.0f * TWO_PI_F;

        while (mSwimAnimationTime[targetCellIndex] * animationSpeedFactor > animationPeriodThreshold) {
            mSwimAnimationTime[targetCellIndex] -= animationPeriodThreshold / animationSpeedFactor;
        }

        if (environment.mIsPreviewMode) {
            continue;
        }

        mForceX[targetCellIndex] += std::cos(mAngle[targetCellIndex]) * swimForce;
        mForceY[targetCellIndex] += std::sin(mAngle[targetCellIndex]) * swimForce;
    }
}

void CellData::handleCellCollisionEffects(int currentCellIndex) {
    if (mIsDead[currentCellIndex]) {
        return;
    }

    int contactBegin = cellCollisionContactBegin(currentCellIndex);
    int contactEnd = cellCollisionContactEnd(currentCellIndex);
    if (contactBegin == contactEnd) {
        return;
    }

    CellCollisionGeometry geometry;
    for (int contactIndex = contactBegin; contactIndex < contactEnd; contactIndex++) {
        int collidedCellIndex = cellCollisionContactTarget(contactIndex);
        getCellCollisionContactGeometry(contactIndex, geometry);

        applyCellCollisionForce(currentCellIndex, collidedCellIndex, geometry);
        handleCiliocyteCollision(currentCellIndex, collidedCellIndex, geometry.normalX, geometry.normalY);
    }
}

void CellData::handleAllCellCollisionEffects() {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        handleCellCollisionEffects(cellIndex);
    }
}

void CellData::handleCellCollisionBiologicalSideEffects(int currentCellIndex, double deltaTime, Environment& environment) {
    if (mIsDead[currentCellIndex]) {
        return;
    }

    int contactBegin = cellCollisionContactBegin(currentCellIndex);
    int contactEnd = cellCollisionContactEnd(currentCellIndex);
    if (contactBegin == contactEnd) {
        return;
    }

    for (int contactIndex = contactBegin; contactIndex < contactEnd; contactIndex++) {
        int collidedCellIndex = cellCollisionContactTarget(contactIndex);
        if (!isValidCellCollisionTarget(currentCellIndex, collidedCellIndex)) {
            continue;
        }

        handleVirocyteCollision(currentCellIndex, collidedCellIndex, deltaTime, environment);
        handleGlueocyteCollision(currentCellIndex, collidedCellIndex);
        handleDevorocyteCollision(currentCellIndex, collidedCellIndex, deltaTime);
        handleSecrocyteCollision(currentCellIndex, collidedCellIndex, deltaTime, environment);
    }
}

void CellData::handleAllCellCollisionBiologicalSideEffects(double deltaTime, Environment& environment) {
    for (int cellIndex = 0; cellIndex < mCellCount; cellIndex++) {
        handleCellCollisionBiologicalSideEffects(cellIndex, deltaTime, environment);
    }
}

void CellData::addCiliocyteInteraction(int currentCellIndex, int collidedCellIndex) {
    if (static_cast<std::size_t>(mCiliocyteInteractionCount)
            >= mCiliocyteInteractionCellIndex1.size()) {
        throw std::overflow_error(
                "Ciliocyte interaction count exceeded its proven per-Cell bound");
    }
    mCiliocyteInteractionCellIndex1[mCiliocyteInteractionCount] = currentCellIndex;
    mCiliocyteInteractionCellIndex2[mCiliocyteInteractionCount] = collidedCellIndex;
    mCiliocyteInteractionCount++;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::CiliocyteQueued,
            cellularity::verification::traceI32(mCiliocyteInteractionCount - 1),
            cellularity::verification::traceI32(currentCellIndex),
            cellularity::verification::traceI32(collidedCellIndex),
            cellularity::verification::traceI32(mCiliocyteInteractionCount),
            cellularity::verification::traceU64(mCiliocyteInteractionCellIndex1.size()));
}

void CellData::handleCiliocyteCollision(int currentCellIndex, int collidedCellIndex, double normalX, double normalY) {
    if (currentCellIndex < 0
            || currentCellIndex >= mCellCount
            || collidedCellIndex < 0
            || collidedCellIndex >= mCellCount
            || mIsDead[currentCellIndex]
            || mIsDead[collidedCellIndex]
            || !(mMass[currentCellIndex] > 0.0)
            || !(mMass[collidedCellIndex] > 0.0)
            || !std::isfinite(mMass[currentCellIndex])
            || !std::isfinite(mMass[collidedCellIndex])) {
        return;
    }
    CellType currentCellType = mActiveCellType[currentCellIndex];
    if (currentCellType != CellType::CILIOCYTE) {
        return;
    }

    double effectiveMass = mMass[currentCellIndex] * mMass[collidedCellIndex]
            / (mMass[currentCellIndex] + mMass[collidedCellIndex]);

    double velocityDiffX = mVelocityX[collidedCellIndex] - mVelocityX[currentCellIndex];
    double velocityDiffY = mVelocityY[collidedCellIndex] - mVelocityY[currentCellIndex];

    double forceX = velocityDiffX * effectiveMass / SimulationWorld::DELTA_TIME * 0.2;
    double forceY = velocityDiffY * effectiveMass / SimulationWorld::DELTA_TIME * 0.2;

    double projection = forceX * normalX + forceY * normalY;

    mForceX[currentCellIndex] += normalX * projection;
    mForceY[currentCellIndex] += normalY * projection;

    if (!isNotLinked(currentCellIndex, collidedCellIndex)) {
        return;
    }

    addCiliocyteInteraction(currentCellIndex, collidedCellIndex);
}

void CellData::handleCiliocyteToBoundaryCollision(int currentCellIndex) {
    CellType currentCellType = mActiveCellType[currentCellIndex];
    if (currentCellType != CellType::CILIOCYTE) {
        return;
    }

    if (!isNotLinked(currentCellIndex, BOUNDARY_CONNECTION_INDEX)) {
        return;
    }

    addCiliocyteInteraction(currentCellIndex, BOUNDARY_CONNECTION_INDEX);
}

void CellData::updateCiliocyteInteractions(const Environment& environment) {
    if (mCiliocyteInteractionCount == 0) {
        return;
    }

    for (int interactionIndex = 0; interactionIndex < mCiliocyteInteractionCount; interactionIndex++) {
        int firstCellIndex = mCiliocyteInteractionCellIndex1[interactionIndex];
        int secondCellIndex = mCiliocyteInteractionCellIndex2[interactionIndex];
        bool isBoundaryInteraction = secondCellIndex == BOUNDARY_CONNECTION_INDEX;

        if (firstCellIndex < 0
                || firstCellIndex >= mCellCount
                || mIsDead[firstCellIndex]
                || !(mMass[firstCellIndex] > 0.0)
                || !std::isfinite(mMass[firstCellIndex])) {
            continue;
        }
        if (!isBoundaryInteraction
                && (secondCellIndex < 0
                        || secondCellIndex >= mCellCount
                        || mIsDead[secondCellIndex]
                        || !(mMass[secondCellIndex] > 0.0)
                        || !std::isfinite(mMass[secondCellIndex]))) {
            continue;
        }

        double firstPositionX = mPositionX[firstCellIndex];
        double firstPositionY = mPositionY[firstCellIndex];
        double firstRadius = mRadius[firstCellIndex];
        double firstMass = mMass[firstCellIndex];
        double firstInertia = firstMass * firstRadius * firstRadius * 0.7;

        double secondContactVelocityX = 0.0;
        double secondContactVelocityY = 0.0;

        double contactOffsetX;
        double contactOffsetY;
        double normalX;
        double normalY;
        double distance;
        double contactDistanceFromFirstCenter;
        double secondMass = 0.0;
        double secondInertia = 0.0;

        if (isBoundaryInteraction) {
            const auto boundary = environment.geometry().boundaryLocation(firstPositionX, firstPositionY);
            distance = boundary.centerDistance;
            contactDistanceFromFirstCenter = boundary.clearance;
            normalX = boundary.normalX;
            normalY = boundary.normalY;
            contactOffsetX = contactDistanceFromFirstCenter * normalX;
            contactOffsetY = contactDistanceFromFirstCenter * normalY;
        } else {
            double secondPositionX = mPositionX[secondCellIndex];
            double secondPositionY = mPositionY[secondCellIndex];
            double secondRadius = mRadius[secondCellIndex];

            double deltaX = firstPositionX - secondPositionX;
            double deltaY = firstPositionY - secondPositionY;
            double distanceSq = deltaX * deltaX + deltaY * deltaY;

            distance = std::sqrt(distanceSq);
            contactDistanceFromFirstCenter = (distance * distance - secondRadius * secondRadius + firstRadius * firstRadius) / (2 * distance);
            normalX = (secondPositionX - firstPositionX) / distance;
            normalY = (secondPositionY - firstPositionY) / distance;
            contactOffsetX = contactDistanceFromFirstCenter * normalX;
            contactOffsetY = contactDistanceFromFirstCenter * normalY;

            secondContactVelocityX = mForceX[secondCellIndex] - mTorque[secondCellIndex] * (contactOffsetY + firstPositionY - secondPositionY);
            secondContactVelocityY = mForceY[secondCellIndex] + mTorque[secondCellIndex] * (contactOffsetX + firstPositionX - secondPositionX);
            secondMass = mMass[secondCellIndex];
            secondInertia = secondMass * secondRadius * secondRadius * 0.7;
        }

        double relativeVelocityX = mForceX[firstCellIndex] - mTorque[firstCellIndex] * contactOffsetY - secondContactVelocityX;
        double relativeVelocityY = mForceY[firstCellIndex] + mTorque[firstCellIndex] * contactOffsetX - secondContactVelocityY;

        double normalVelocity = normalX * relativeVelocityX + normalY * relativeVelocityY;
        relativeVelocityX -= normalX * normalVelocity;
        relativeVelocityY -= normalY * normalVelocity;

        double desiredTangentialVelocity = 0.0;
        const cellularity::GeneView firstGene =
                getActiveGeneView(firstCellIndex);
        if (firstGene.cellType() == CellType::CILIOCYTE) {
            double directionX = std::cos(mAngle[firstCellIndex]);
            double directionY = std::sin(mAngle[firstCellIndex]);
            double cross = normalY * directionX - normalX * directionY;
            if (cross < -0.2 || cross > 0.2) {
                if ((0 < cross) != mIsMirrored[firstCellIndex]) {
                    desiredTangentialVelocity += cross * SimulationWorld::MAX_TAX_SPEED * getValue(firstCellIndex, Gene::MYOCYTE_BENDING_VALUE_INDEX, 0);
                } else {
                    desiredTangentialVelocity += cross * SimulationWorld::MAX_TAX_SPEED * getValue(firstCellIndex, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);
                }
            }
        }

        if (!isBoundaryInteraction) {
            const cellularity::GeneView secondGene =
                    getActiveGeneView(secondCellIndex);
            if (secondGene.cellType() == CellType::CILIOCYTE) {
                double directionX = std::cos(mAngle[secondCellIndex]);
                double directionY = std::sin(mAngle[secondCellIndex]);
                double cross = -normalY * directionX + normalX * directionY;
                if (cross < -0.2 || cross > 0.2) {
                    if ((0 < cross) != mIsMirrored[secondCellIndex]) {
                        desiredTangentialVelocity += cross * SimulationWorld::MAX_TAX_SPEED * getValue(secondCellIndex, Gene::MYOCYTE_BENDING_VALUE_INDEX, 0);
                    } else {
                        desiredTangentialVelocity += cross * SimulationWorld::MAX_TAX_SPEED * getValue(secondCellIndex, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);
                    }
                }
            }
        }

        relativeVelocityX += normalY * desiredTangentialVelocity;
        relativeVelocityY -= normalX * desiredTangentialVelocity;

        double ratio = 1.0;
        double effectiveImpulseMass;
        if (isBoundaryInteraction) {
            effectiveImpulseMass = 1.0 / (1.0 / firstMass + ratio * (contactDistanceFromFirstCenter * contactDistanceFromFirstCenter / firstInertia))
                    / SimulationWorld::DELTA_TIME * 0.9;
        } else {
            double contactDistanceFromSecondCenter = distance - contactDistanceFromFirstCenter;
            effectiveImpulseMass = 1.0 / (1.0 / firstMass + 1.0 / secondMass + ratio * (contactDistanceFromFirstCenter * contactDistanceFromFirstCenter / firstInertia + contactDistanceFromSecondCenter * contactDistanceFromSecondCenter / secondInertia))
                    / SimulationWorld::DELTA_TIME * 0.9;
        }

        double impulseX = -relativeVelocityX * effectiveImpulseMass;
        double impulseY = -relativeVelocityY * effectiveImpulseMass;

        mVelocityX[firstCellIndex] += impulseX * SimulationWorld::DELTA_TIME / firstMass;
        mVelocityY[firstCellIndex] += impulseY * SimulationWorld::DELTA_TIME / firstMass;
        mAngularVelocity[firstCellIndex] += ratio * contactDistanceFromFirstCenter * (-impulseX * normalY + impulseY * normalX)
                * SimulationWorld::DELTA_TIME / firstInertia;

        if (!isBoundaryInteraction) {
            double contactDistanceFromSecondCenter = distance - contactDistanceFromFirstCenter;
            mVelocityX[secondCellIndex] -= impulseX * SimulationWorld::DELTA_TIME / secondMass;
            mVelocityY[secondCellIndex] -= impulseY * SimulationWorld::DELTA_TIME / secondMass;
            mAngularVelocity[secondCellIndex] += ratio * contactDistanceFromSecondCenter * (-impulseX * normalY + impulseY * normalX)
                    * SimulationWorld::DELTA_TIME / secondInertia;
        }
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::CiliocyteApplied,
                cellularity::verification::traceI32(interactionIndex),
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceF64(mVelocityX[firstCellIndex]),
                cellularity::verification::traceF64(mVelocityY[firstCellIndex]),
                cellularity::verification::traceF64(mAngularVelocity[firstCellIndex]),
                cellularity::verification::traceF64(
                        isBoundaryInteraction ? 0.0 : mVelocityX[secondCellIndex]),
                cellularity::verification::traceF64(
                        isBoundaryInteraction ? 0.0 : mVelocityY[secondCellIndex]),
                cellularity::verification::traceF64(
                        isBoundaryInteraction ? 0.0 : mAngularVelocity[secondCellIndex]));
    }
}

void CellData::handleGlueocyteCollision(int currentCellIndex, int victimCellIndex) {
    CellType currentCellType = mActiveCellType[currentCellIndex];
    if (currentCellType != CellType::GLUEOCYTE) {
        return;
    }

    bool isCollidedCellSafeByKeratinocyte = isCellSafeByKeratinocyte(victimCellIndex);

    if (mTopology.linkCount(currentCellIndex) >= MAX_GLUEOCYTE_LINK_COUNT) {
        return;
    }

    if (!mTopology.canMakeLink(currentCellIndex) || !mTopology.canMakeLink(victimCellIndex)) {
        return;
    }

    double minAge = AGE_OF_CONSENT * 0.2;

    if (mAge[currentCellIndex] < minAge || mAge[victimCellIndex] < minAge) {
        return;
    }

    if (!isNotLinked(currentCellIndex, victimCellIndex)) {
        return;
    }

    if (isCollidedCellSafeByKeratinocyte) {
        return;
    }

    double currentCellPositionX = mPositionX[currentCellIndex];
    double currentCellPositionY = mPositionY[currentCellIndex];

    double collidedCellPositionX = mPositionX[victimCellIndex];
    double collidedCellPositionY = mPositionY[victimCellIndex];

    double an = std::atan2(collidedCellPositionY - currentCellPositionY, collidedCellPositionX - currentCellPositionX);
    double pi = std::acos(-1.0);
    double currentAngle = an - mAngle[currentCellIndex];
    double victimAngle = an + pi - mAngle[victimCellIndex];
    int edgeIndex = mTopology.addCellLink(
            currentCellIndex,
            victimCellIndex,
            CellLinkCreationReason::GLUEOCYTE_COLLISION,
            currentAngle,
            victimAngle,
            0.5f,
            0.0f);
    if (edgeIndex < 0) {
        return;
    }
}

void CellData::handleDevorocyteCollision(int currentCellIndex, int victimCellIndex, double deltaTime) {
    CellType currentCellType = mActiveCellType[currentCellIndex];

    if (currentCellType != CellType::DEVOROCYTE) {
        return;
    }

    bool isCollidedCellSafeByKeratinocyte = isCellSafeByKeratinocyte(victimCellIndex);

    if (isCollidedCellSafeByKeratinocyte) {
        return;
    }

    double deltaMass = DEVOROCYTE_SUCK_SPEED * deltaTime;

    if (deltaMass > mMass[victimCellIndex] - MIN_MASS * 0.5) {
        deltaMass = mMass[victimCellIndex] - MIN_MASS * 0.5;
    }

    mMass[currentCellIndex] += deltaMass;
    mMass[victimCellIndex] -= deltaMass;

    mNitrates[currentCellIndex] += deltaMass * mNitrates[victimCellIndex] / mMass[victimCellIndex];
    mNitrates[victimCellIndex] -= deltaMass * mNitrates[victimCellIndex] / mMass[victimCellIndex];

    mToxin[currentCellIndex] = static_cast<float>(mToxin[currentCellIndex] + deltaMass * mToxin[victimCellIndex] / mMass[victimCellIndex]);
    mToxin[victimCellIndex] = static_cast<float>(mToxin[victimCellIndex] - deltaMass * mToxin[victimCellIndex] / mMass[victimCellIndex]);
}

void CellData::handleVirocyteCollision(int currentCellIndex, int victimCellIndex, double deltaTime, Environment& environment) {
    if (environment.mIsPreviewMode) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::VirocyteOutcome,
                cellularity::verification::traceI32(currentCellIndex),
                cellularity::verification::traceI32(victimCellIndex),
                cellularity::verification::traceI32(0));
        return;
    }

    if (mActiveCellType[currentCellIndex] != CellType::VIROCYTE) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::VirocyteOutcome,
                cellularity::verification::traceI32(currentCellIndex),
                cellularity::verification::traceI32(victimCellIndex),
                cellularity::verification::traceI32(1));
        return;
    }

    const cellularity::GeneView currentCellActiveGene =
            getActiveGeneView(currentCellIndex);
    if (environment.mRandom.nextUniform() >= deltaTime * 5.0) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::VirocyteOutcome,
                cellularity::verification::traceI32(currentCellIndex),
                cellularity::verification::traceI32(victimCellIndex),
                cellularity::verification::traceI32(2));
        return;
    }

    if (isCellSafeByKeratinocyte(victimCellIndex)) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::VirocyteOutcome,
                cellularity::verification::traceI32(currentCellIndex),
                cellularity::verification::traceI32(victimCellIndex),
                cellularity::verification::traceI32(3));
        return;
    }

    const std::size_t sourceGeneCount = geneCount(currentCellIndex);
    const std::size_t victimGeneCount = geneCount(victimCellIndex);
    const auto victimMetadata = mGenomePool.metadata(
            mGenomeIds[static_cast<std::size_t>(victimCellIndex)]);
    if (!cellularity::isSupportedGeneCount(sourceGeneCount)
            || !cellularity::isSupportedGeneCount(victimGeneCount)
            || !victimMetadata.has_value()) {
        return;
    }

    cellularity::GenomeData victimGenome = materializeGenome(victimCellIndex);
    const cellularity::GenomeData currentGenome =
            materializeGenome(currentCellIndex);

    const cellularity::GenomeView currentGenomeView(
            currentGenome.data(), currentGenome.size());
    const cellularity::GeneIndex virusFromGeneIndex =
            currentCellActiveGene.virusFromGeneIndex();
    const cellularity::GeneIndex virusToGeneIndex(
            static_cast<cellularity::GeneIndex::Value>(
                    static_cast<std::size_t>(
                            currentCellActiveGene.virusToGeneIndex().value())
                    % victimGeneCount));
    const cellularity::GeneView virusFromGene =
            currentGenomeView.gene(virusFromGeneIndex);
    const cellularity::GeneIndex virusChild1Index =
            virusFromGene.child1GeneIndex();
    const cellularity::GeneIndex virusChild2Index =
            virusFromGene.child2GeneIndex();

    const cellularity::GeneView virusFromChild1Gene =
            currentGenomeView.gene(virusChild1Index);
    const cellularity::GeneIndex child1Child1Index =
            virusFromChild1Gene.child1GeneIndex();
    const cellularity::GeneIndex child1Child2Index =
            virusFromChild1Gene.child2GeneIndex();

    const cellularity::GeneView virusFromChild2Gene =
            currentGenomeView.gene(virusChild2Index);
    const cellularity::GeneIndex child2Child1Index =
            virusFromChild2Gene.child1GeneIndex();
    const cellularity::GeneIndex child2Child2Index =
            virusFromChild2Gene.child2GeneIndex();

    // Keep the legacy breadth-two traversal and write order. It is observable
    // when a smaller victim folds several source offsets onto the same slot.
    const std::array<cellularity::GeneIndex, 7> transferredSourceIndices{
            virusFromGeneIndex,
            virusChild1Index,
            virusChild2Index,
            child1Child1Index,
            child1Child2Index,
            child2Child1Index,
            child2Child2Index
    };
    for (const cellularity::GeneIndex sourceIndex :
            transferredSourceIndices) {
        const cellularity::GeneIndex destinationIndex =
                viralTransferDestination(
                        sourceIndex,
                        virusFromGeneIndex,
                        sourceGeneCount,
                        virusToGeneIndex,
                        victimGeneCount);
        if (!copyGeneAcrossGenomeDomains(
                    victimGenome[destinationIndex.value()],
                    currentGenomeView.gene(sourceIndex),
                    sourceIndex,
                    sourceGeneCount,
                    victimGeneCount)) {
            return;
        }
    }
    if (!cellularity::applyGenomeMetadataToCompatibilityFlags(
                victimGenome.data(), victimGenome.size(), *victimMetadata)) {
        return;
    }
    if (!replaceGenome(
                victimCellIndex,
                victimGenome,
                activeGeneIndex(victimCellIndex))) {
        throw std::runtime_error("Failed to install a transferred viral Genome");
    }
    if (mOriginTag[victimCellIndex] == SimulationWorld::USER_CELL_ORIGIN) {
        mOriginTag[victimCellIndex] = SimulationWorld::INFECTED_USER_CELL_ORIGIN;
    }
    if (mOriginTag[victimCellIndex] == SimulationWorld::RANDOM_CELL_ORIGIN) {
        mOriginTag[victimCellIndex] = SimulationWorld::INFECTED_RANDOM_CELL_ORIGIN;
    }
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::VirocyteOutcome,
            cellularity::verification::traceI32(currentCellIndex),
            cellularity::verification::traceI32(victimCellIndex),
            cellularity::verification::traceI32(4),
            cellularity::verification::traceI32(CellTypes::ordinal(mActiveCellType[victimCellIndex])),
            cellularity::verification::traceI32(mOriginTag[victimCellIndex]));
}

void CellData::handleSecrocyteCollision(int currentCellIndex, int victimCellIndex, double deltaTime, Environment& environment) {
    CellType currentCellType = mActiveCellType[currentCellIndex];
    if (currentCellType != CellType::SECROCYTE) {
        return;
    }

    const cellularity::GeneView currentCellActiveGene =
            getActiveGeneView(currentCellIndex);
    if (mNitrates[currentCellIndex] < MIN_SECRETE_NITRATES) {
        return;
    }

    bool isVictimSafeByKeratinocyte = isCellSafeByKeratinocyte(victimCellIndex);

    const cellularity::GeneView victimCellActiveGene =
            getActiveGeneView(victimCellIndex);

    int currentCellSecreteType = currentCellActiveGene.integerValue(
            Gene::SECRETE_TYPE_INTEGER_INDEX);
    int victimCellSecreteType = victimCellActiveGene.integerValue(
            Gene::SECRETE_TYPE_INTEGER_INDEX);

    bool isCurrentCellAgeOfConsent = mAge[currentCellIndex] > AGE_OF_CONSENT;
    double currentCellPositionX = mPositionX[currentCellIndex];
    double currentCellPositionY = mPositionY[currentCellIndex];

    CellType victimCellType = mActiveCellType[victimCellIndex];
    double currentCellRadius = mRadius[currentCellIndex];

    if (currentCellSecreteType == Gene::SECRETE_TYPE_CYANIDE) {
        if (!isVictimSafeByKeratinocyte) {
            bool isImmuneToCyanide = victimCellType == CellType::SECROCYTE
                    && victimCellSecreteType == Gene::SECRETE_TYPE_CYANIDE;
            if (!isImmuneToCyanide) {
                mToxin[victimCellIndex] += static_cast<float>(TOXIN_SPEED * deltaTime);
            }
        }
    }

    if (currentCellSecreteType == Gene::SECRETE_TYPE_LIPASE) {
        if (victimCellType != CellType::KERATINOCYTE) {
            bool isImmuneToDefensin = victimCellType == CellType::SECROCYTE
                    && victimCellSecreteType == Gene::SECRETE_TYPE_LIPASE;
            if (!isImmuneToDefensin) {
                mToxication[victimCellIndex] += static_cast<float>(DEFENSIN_SPEED * deltaTime);
            }
        }
    }

    if (currentCellSecreteType == Gene::SECRETE_TYPE_PROTEASE) {
        if (isCurrentCellAgeOfConsent) {
            std::array<CellTopology::CellLinkSnapshot, MAX_NEIGHBORS> victimLinks{};
            const int victimLinkCount = mTopology.collectLinksOfCell(victimCellIndex, victimLinks);
            bool removedLink = false;
            for (int linkIndex = 0; linkIndex < victimLinkCount; linkIndex++) {
                const CellTopology::CellLinkSnapshot& victimLink = victimLinks[linkIndex];
                int victimLinkedCellIndex = victimLink.connectedCellIndex;
                if (victimLink.isBoundary) {
                    const auto geometry = environment.geometry();
                    if (geometry.cellClearsBoundary(currentCellPositionX, currentCellPositionY, currentCellRadius)) {
                        continue;
                    }
                    if (geometry.isRing()) {
                        const bool anchorIsInner = victimLink.boundaryAnchorX * victimLink.boundaryAnchorX
                                        + victimLink.boundaryAnchorY * victimLink.boundaryAnchorY
                                < geometry.innerRadius() * geometry.innerRadius();
                        const bool contactIsInner = geometry.boundaryLocation(currentCellPositionX, currentCellPositionY).normalDirection < 0.0;
                        if (anchorIsInner != contactIsInner) {
                            continue;
                        }
                    }

                    removedLink = mTopology.removeBoundaryLinkOfCell(victimCellIndex) || removedLink;
                } else {
                    if (victimLinkedCellIndex == victimCellIndex) {
                        continue;
                    } else {
                        double victimLinkedCellRadius = mRadius[victimLinkedCellIndex];
                        double victimLinkedCellPositionX = mPositionX[victimLinkedCellIndex];
                        double victimLinkedCellPositionY = mPositionY[victimLinkedCellIndex];

                        double distanceX = currentCellPositionX - victimLinkedCellPositionX;
                        double distanceY = currentCellPositionY - victimLinkedCellPositionY;
                        double distanceSq = distanceX * distanceX + distanceY * distanceY;
                        double contactDist = currentCellRadius + victimLinkedCellRadius;
                        double contactDistSq = contactDist * contactDist;

                        if (distanceSq > contactDistSq) {
                            continue;
                        }
                    }

                    removedLink = mTopology.removeCellLinkBetween(victimCellIndex, victimLinkedCellIndex) || removedLink;
                }
            }
        }
    }

    if (victimCellType == CellType::LIPOCYTE) {
        return;
    }

    if (Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT <= currentCellSecreteType) {
        if (currentCellSecreteType < SIGNAL_SUBSTANCE_COUNT + Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT) {
            mSignalSubstance[victimCellIndex][currentCellSecreteType - Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT] -= static_cast<float>(TOXIN_SPEED * deltaTime);
        }
    }

    if (SIGNAL_SUBSTANCE_COUNT + Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT <= currentCellSecreteType) {
        if (currentCellSecreteType < 2 * SIGNAL_SUBSTANCE_COUNT + Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT) {
            mSignalSubstance[victimCellIndex][currentCellSecreteType - Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT - SIGNAL_SUBSTANCE_COUNT] += static_cast<float>(TOXIN_SPEED * deltaTime);
        }
    }
}

int CellData::allocateNewCell() {
    if (mCellCount >= MAX_CELL_COUNT) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::CellAllocate,
                cellularity::verification::traceI32(-1),
                cellularity::verification::traceI32(mCellCount),
                cellularity::verification::traceBool(false));
        return -1;
    }
    const int newCellIndex = mCellCount;
    mRuntimeIds[static_cast<std::size_t>(newCellIndex)] = allocateRuntimeId();
    mOrganismIds[static_cast<std::size_t>(newCellIndex)] =
            kUnknownOrganismId;
    mParentOrganismIds[static_cast<std::size_t>(newCellIndex)] =
            kUnknownOrganismId;
    ++mCellCount;
    if (!initializeDefaultGenomeHandle(newCellIndex)) {
        --mCellCount;
        mRuntimeIds[static_cast<std::size_t>(newCellIndex)] =
                kInvalidCellRuntimeId;
        mOrganismIds[static_cast<std::size_t>(newCellIndex)] =
                kUnknownOrganismId;
        mParentOrganismIds[static_cast<std::size_t>(newCellIndex)] =
                kUnknownOrganismId;
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::CellAllocate,
                cellularity::verification::traceI32(-1),
                cellularity::verification::traceI32(mCellCount),
                cellularity::verification::traceBool(false));
        return -1;
    }
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::CellAllocate,
            cellularity::verification::traceI32(newCellIndex),
            cellularity::verification::traceI32(mCellCount),
            cellularity::verification::traceBool(true));
    return newCellIndex;
}

void CellData::resizeActiveCellCount(int newCellCount) {
    if (newCellCount < 0 || newCellCount > MAX_CELL_COUNT) {
        throw std::length_error("CellData active Cell count is out of range");
    }

    if (newCellCount > mCellCount) {
        const std::size_t addedCount = static_cast<std::size_t>(
                newCellCount - mCellCount);
        ensureRuntimeIdsAvailable(addedCount);
        for (int cellIndex = mCellCount;
                cellIndex < newCellCount;
                ++cellIndex) {
            mRuntimeIds[static_cast<std::size_t>(cellIndex)] =
                    allocateRuntimeId();
            mOrganismIds[static_cast<std::size_t>(cellIndex)] =
                    kUnknownOrganismId;
            mParentOrganismIds[static_cast<std::size_t>(cellIndex)] =
                    kUnknownOrganismId;
            if (!initializeDefaultGenomeHandle(cellIndex)) {
                for (int rollback = mCellCount;
                        rollback <= cellIndex;
                        ++rollback) {
                    releaseGenomeHandle(rollback);
                    mRuntimeIds[static_cast<std::size_t>(rollback)] =
                            kInvalidCellRuntimeId;
                    mOrganismIds[static_cast<std::size_t>(rollback)] =
                            kUnknownOrganismId;
                    mParentOrganismIds[static_cast<std::size_t>(rollback)] =
                            kUnknownOrganismId;
                }
                throw std::bad_alloc();
            }
        }
    } else {
        for (int cellIndex = newCellCount;
                cellIndex < mCellCount;
                ++cellIndex) {
            releaseGenomeHandle(cellIndex);
        }
        std::fill(
                mRuntimeIds.begin() + newCellCount,
                mRuntimeIds.begin() + mCellCount,
                kInvalidCellRuntimeId);
        std::fill(
                mOrganismIds.begin() + newCellCount,
                mOrganismIds.begin() + mCellCount,
                kUnknownOrganismId);
        std::fill(
                mParentOrganismIds.begin() + newCellCount,
                mParentOrganismIds.begin() + mCellCount,
                kUnknownOrganismId);
    }
    mCellCount = newCellCount;
}

void CellData::replaceActiveCellCount(int newCellCount) {
    if (newCellCount < 0 || newCellCount > MAX_CELL_COUNT) {
        throw std::length_error("CellData replacement Cell count is out of range");
    }

    ensureRuntimeIdsAvailable(static_cast<std::size_t>(newCellCount));
    const int previousCellCount = mCellCount;
    if (newCellCount > previousCellCount) {
        int initializedEnd = previousCellCount;
        for (; initializedEnd < newCellCount; ++initializedEnd) {
            if (initializeDefaultGenomeHandle(initializedEnd)) {
                continue;
            }
            for (int rollback = previousCellCount;
                    rollback < initializedEnd;
                    ++rollback) {
                releaseGenomeHandle(rollback);
            }
            throw std::bad_alloc();
        }
    } else {
        for (int cellIndex = newCellCount;
                cellIndex < previousCellCount;
                ++cellIndex) {
            releaseGenomeHandle(cellIndex);
        }
    }

    for (int cellIndex = 0; cellIndex < newCellCount; ++cellIndex) {
        mRuntimeIds[static_cast<std::size_t>(cellIndex)] =
                allocateRuntimeId();
    }
    for (int cellIndex = previousCellCount;
            cellIndex < newCellCount;
            ++cellIndex) {
        mOrganismIds[static_cast<std::size_t>(cellIndex)] =
                kUnknownOrganismId;
        mParentOrganismIds[static_cast<std::size_t>(cellIndex)] =
                kUnknownOrganismId;
    }
    if (newCellCount < previousCellCount) {
        std::fill(
                mRuntimeIds.begin() + newCellCount,
                mRuntimeIds.begin() + previousCellCount,
                kInvalidCellRuntimeId);
        std::fill(
                mOrganismIds.begin() + newCellCount,
                mOrganismIds.begin() + previousCellCount,
                kUnknownOrganismId);
        std::fill(
                mParentOrganismIds.begin() + newCellCount,
                mParentOrganismIds.begin() + previousCellCount,
                kUnknownOrganismId);
    }
    mCellCount = newCellCount;
}

void CellData::releaseLastCellSlot() {
    if (mCellCount <= 0) {
        throw std::logic_error("CellData cannot release an empty Cell range");
    }
    releaseGenomeHandle(mCellCount - 1);
    --mCellCount;
    mRuntimeIds[static_cast<std::size_t>(mCellCount)] =
            kInvalidCellRuntimeId;
    mOrganismIds[static_cast<std::size_t>(mCellCount)] =
            kUnknownOrganismId;
    mParentOrganismIds[static_cast<std::size_t>(mCellCount)] =
            kUnknownOrganismId;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::CellRelease,
            cellularity::verification::traceI32(mCellCount),
            cellularity::verification::traceI32(mCellCount));
}

CellRuntimeId CellData::runtimeId(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mRuntimeIds[static_cast<std::size_t>(cellIndex)];
}

OrganismId CellData::organismId(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mOrganismIds[static_cast<std::size_t>(cellIndex)];
}

OrganismId CellData::parentOrganismId(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mCellCount);
    return mParentOrganismIds[static_cast<std::size_t>(cellIndex)];
}

OrganismId CellData::nextOrganismId() const noexcept {
    return mNextOrganismId;
}

OrganismId CellData::assignNewOrganism(
        int cellIndex,
        OrganismId parentId) {
    if (cellIndex < 0 || cellIndex >= mCellCount
            || (parentId != kUnknownOrganismId
                    && (!isKnownOrganismId(parentId)
                            || parentId >= mNextOrganismId))) {
        throw std::invalid_argument("Invalid Cell organism assignment");
    }
    const OrganismId id = allocateOrganismId();
    mOrganismIds[static_cast<std::size_t>(cellIndex)] = id;
    mParentOrganismIds[static_cast<std::size_t>(cellIndex)] = parentId;
    return id;
}

void CellData::restoreNextOrganismId(OrganismId nextId) {
    if (nextId == kUnknownOrganismId
            || nextId > kMaximumOrganismId + OrganismId{1}) {
        throw std::invalid_argument("Invalid next Organism identity");
    }
    const OrganismId previousNextId = mNextOrganismId;
    mNextOrganismId = nextId;
    if (!validateOrganismIdentities()) {
        mNextOrganismId = previousNextId;
        throw std::invalid_argument("Invalid restored Organism identities");
    }
}

bool CellData::validateOrganismIdentities() const {
    if (mNextOrganismId == kUnknownOrganismId
            || mNextOrganismId > kMaximumOrganismId + OrganismId{1}) {
        return false;
    }
    std::unordered_map<OrganismId, OrganismId> parentByOrganism;
    parentByOrganism.reserve(static_cast<std::size_t>(mCellCount));
    for (int cellIndex = 0; cellIndex < mCellCount; ++cellIndex) {
        const std::size_t index = static_cast<std::size_t>(cellIndex);
        const OrganismId id = mOrganismIds[index];
        const OrganismId parentId = mParentOrganismIds[index];
        if (id == kUnknownOrganismId) {
            if (parentId != kUnknownOrganismId) {
                return false;
            }
            continue;
        }
        if (!isKnownOrganismId(id) || id >= mNextOrganismId
                || parentId == id
                || (parentId != kUnknownOrganismId
                        && (!isKnownOrganismId(parentId)
                                || parentId >= id))) {
            return false;
        }
        const auto [found, inserted] = parentByOrganism.emplace(id, parentId);
        if (!inserted && found->second != parentId) {
            return false;
        }
    }
    return true;
}

CellData::OrganismIdentitySummary
CellData::organismIdentitySummary() const {
    OrganismIdentitySummary result;
    std::unordered_set<OrganismId> knownIds;
    knownIds.reserve(static_cast<std::size_t>(mCellCount));
    for (int cellIndex = 0; cellIndex < mCellCount; ++cellIndex) {
        if (mIsDead[static_cast<std::size_t>(cellIndex)]) {
            continue;
        }
        const OrganismId id =
                mOrganismIds[static_cast<std::size_t>(cellIndex)];
        if (id == kUnknownOrganismId) {
            result.complete = false;
        } else {
            knownIds.insert(id);
        }
    }
    result.knownOrganismCount = knownIds.size();
    return result;
}

CellRuntimeId CellData::allocateRuntimeId() {
    ensureRuntimeIdsAvailable(1);
    return mNextRuntimeId++;
}

void CellData::ensureRuntimeIdsAvailable(std::size_t count) const {
    const CellRuntimeId available = kBoundaryCellRuntimeId - mNextRuntimeId;
    if (count > available) {
        throw std::overflow_error("CellData runtime Cell identity space is exhausted");
    }
}

OrganismId CellData::allocateOrganismId() {
    if (mNextOrganismId > kMaximumOrganismId) {
        throw std::overflow_error(
                "CellData Organism identity space is exhausted");
    }
    return mNextOrganismId++;
}

bool CellData::hasInheritedOrganismConnection(
        int cellIndex,
        int siblingCellIndex,
        OrganismId inheritedOrganismId) const {
    const int linkCount = mTopology.linkCount(cellIndex);
    for (int slot = 0; slot < linkCount; ++slot) {
        const int adjacent = mTopology.adjacentCell(cellIndex, slot);
        if (adjacent < 0 || adjacent == siblingCellIndex
                || adjacent >= mCellCount || mIsDead[adjacent]) {
            continue;
        }
        const int edgeIndex =
                mTopology.incidentEdgeIndexAtValidSlot(cellIndex, slot);
        if (edgeIndex < 0
                || mTopology.edgeCreationReason(edgeIndex)
                        != CellLinkCreationReason::SPLIT_INHERITANCE) {
            continue;
        }
        if (inheritedOrganismId == kUnknownOrganismId
                        ? mOrganismIds[adjacent] == kUnknownOrganismId
                        : mOrganismIds[adjacent] == inheritedOrganismId) {
            return true;
        }
    }
    return false;
}

bool CellData::hasSiblingOrganismConnection(
        int firstCellIndex,
        int secondCellIndex) const {
    const int linkCount = mTopology.linkCount(firstCellIndex);
    for (int slot = 0; slot < linkCount; ++slot) {
        if (mTopology.adjacentCell(firstCellIndex, slot) != secondCellIndex) {
            continue;
        }
        const int edgeIndex =
                mTopology.incidentEdgeIndexAtValidSlot(firstCellIndex, slot);
        return edgeIndex >= 0
                && mTopology.edgeCreationReason(edgeIndex)
                == CellLinkCreationReason::SPLIT_SIBLING;
    }
    return false;
}

bool CellData::hasOrganismFormingConnection(
        int firstCellIndex,
        int secondCellIndex) const {
    const int linkCount = mTopology.linkCount(firstCellIndex);
    for (int slot = 0; slot < linkCount; ++slot) {
        if (mTopology.adjacentCell(firstCellIndex, slot) != secondCellIndex) {
            continue;
        }
        const int edgeIndex =
                mTopology.incidentEdgeIndexAtValidSlot(firstCellIndex, slot);
        return edgeIndex >= 0
                && isOrganismFormingCellLink(
                        mTopology.edgeCreationReason(edgeIndex));
    }
    return false;
}

int CellData::collectOrganismComponent(
        int startCellIndex,
        OrganismId organismId) {
    assert(startCellIndex >= 0 && startCellIndex < mCellCount);
    assert(organismId != kUnknownOrganismId);

    if (mOrganismTraversalGeneration
            == std::numeric_limits<std::uint32_t>::max()) {
        std::fill(
                mOrganismTraversalMarks.begin(),
                mOrganismTraversalMarks.end(),
                0);
        mOrganismTraversalGeneration = 1;
    } else {
        ++mOrganismTraversalGeneration;
    }

    int readIndex = 0;
    int componentSize = 1;
    mOrganismTraversalMarks[static_cast<std::size_t>(startCellIndex)] =
            mOrganismTraversalGeneration;
    mOrganismTraversalQueue[0] = startCellIndex;

    while (readIndex < componentSize) {
        const int cellIndex = mOrganismTraversalQueue[static_cast<std::size_t>(readIndex++)];
        const int linkCount = mTopology.linkCount(cellIndex);
        for (int slot = 0; slot < linkCount; ++slot) {
            const int adjacent = mTopology.adjacentCell(cellIndex, slot);
            if (adjacent < 0 || adjacent >= mCellCount
                    || mIsDead[static_cast<std::size_t>(adjacent)]
                    || mOrganismIds[static_cast<std::size_t>(adjacent)]
                            != organismId
                    || mOrganismTraversalMarks[static_cast<std::size_t>(adjacent)]
                            == mOrganismTraversalGeneration) {
                continue;
            }
            const int edgeIndex =
                    mTopology.incidentEdgeIndexAtValidSlot(cellIndex, slot);
            if (edgeIndex < 0
                    || !isOrganismFormingCellLink(
                            mTopology.edgeCreationReason(edgeIndex))) {
                continue;
            }
            mOrganismTraversalMarks[static_cast<std::size_t>(adjacent)] =
                    mOrganismTraversalGeneration;
            mOrganismTraversalQueue[static_cast<std::size_t>(componentSize++)] = adjacent;
        }
    }
    return componentSize;
}

void CellData::assignNewOrganismToCollectedComponent(
        int componentSize,
        OrganismId parentOrganismId) {
    const OrganismId childOrganismId = allocateOrganismId();
    for (int componentIndex = 0;
            componentIndex < componentSize;
            ++componentIndex) {
        const int cellIndex = mOrganismTraversalQueue[static_cast<std::size_t>(componentIndex)];
        mOrganismIds[static_cast<std::size_t>(cellIndex)] = childOrganismId;
        mParentOrganismIds[static_cast<std::size_t>(cellIndex)] =
                parentOrganismId;
    }
}

void CellData::resolveSplitOrganismIdentity(
        int child1Index,
        int child2Index,
        OrganismId inheritedOrganismId,
        const std::array<CellTopology::CellLinkSnapshot, MAX_NEIGHBORS>&
                previousLinks,
        int previousLinkCount) {
    const bool siblingsConnected =
            hasSiblingOrganismConnection(child1Index, child2Index);
    const bool child1Continues = hasInheritedOrganismConnection(
            child1Index, child2Index, inheritedOrganismId);
    const bool child2Continues = hasInheritedOrganismConnection(
            child2Index, child1Index, inheritedOrganismId);

    // Unknown IDs belong to legacy worlds. Keep the deliberately incremental
    // behavior: a split may identify a new descendant, but it must not infer
    // identities for an existing legacy component.
    if (inheritedOrganismId == kUnknownOrganismId
            || mIsDead[static_cast<std::size_t>(child1Index)]
            || mIsDead[static_cast<std::size_t>(child2Index)]) {
        if (siblingsConnected) {
            return;
        }
        if (child1Continues && !child2Continues) {
            (void)assignNewOrganism(child2Index, inheritedOrganismId);
        } else if (!child1Continues && child2Continues) {
            (void)assignNewOrganism(child1Index, inheritedOrganismId);
        } else if (!child1Continues && !child2Continues) {
            (void)assignNewOrganism(child2Index, inheritedOrganismId);
        }
        return;
    }

    std::array<int, MAX_NEIGHBORS> previousOrganismNeighbors{};
    int previousOrganismNeighborCount = 0;
    int droppedBranchCount = 0;
    for (int linkIndex = 0; linkIndex < previousLinkCount; ++linkIndex) {
        const CellTopology::CellLinkSnapshot& previousLink =
                previousLinks[static_cast<std::size_t>(linkIndex)];
        if (previousLink.isBoundary
                || !isOrganismFormingCellLink(previousLink.creationReason)) {
            continue;
        }
        const int adjacent = previousLink.connectedCellIndex;
        if (adjacent < 0 || adjacent >= mCellCount
                || mIsDead[static_cast<std::size_t>(adjacent)]
                || mOrganismIds[static_cast<std::size_t>(adjacent)]
                        != inheritedOrganismId) {
            continue;
        }
        previousOrganismNeighbors[static_cast<std::size_t>(previousOrganismNeighborCount++)] =
                adjacent;
        if (!hasOrganismFormingConnection(child1Index, adjacent)
                && !hasOrganismFormingConnection(child2Index, adjacent)) {
            ++droppedBranchCount;
        }
    }

    // Fast paths cover ordinary division without scanning the organism.
    if (droppedBranchCount == 0) {
        if (siblingsConnected) {
            return;
        }
        if (child1Continues && !child2Continues) {
            (void)assignNewOrganism(child2Index, inheritedOrganismId);
            return;
        }
        if (!child1Continues && child2Continues) {
            (void)assignNewOrganism(child1Index, inheritedOrganismId);
            return;
        }
        if (!child1Continues && !child2Continues) {
            (void)assignNewOrganism(child2Index, inheritedOrganismId);
            return;
        }

        // Both daughters inherited old branches. They still require component
        // classification because those branches may not have an alternate
        // path between them.
    }

    // Only components containing a daughter or one of the parent's former
    // organism-forming neighbors can have changed here. Classify those local
    // components, preserving the inherited ID on the component containing
    // the largest number of pre-split Cells. This keeps the existing body as
    // the parent when a newly linked daughter pair buds away from it.
    constexpr std::uint32_t traversalGenerationHeadroom =
            static_cast<std::uint32_t>(2 * MAX_NEIGHBORS + 4);
    if (mOrganismTraversalGeneration
            > std::numeric_limits<std::uint32_t>::max()
                    - traversalGenerationHeadroom) {
        std::fill(
                mOrganismTraversalMarks.begin(),
                mOrganismTraversalMarks.end(),
                0);
        mOrganismTraversalGeneration = 0;
    }

    struct SplitComponent final {
        int representative = -1;
        int size = 0;
        int preSplitCellCount = 0;
        bool containsChild1 = false;
        std::uint32_t traversalGeneration = 0;
    };

    std::array<SplitComponent, MAX_NEIGHBORS + 2> components{};
    int componentCount = 0;

    std::array<int, MAX_NEIGHBORS + 2> seeds{};
    int seedCount = 0;
    for (int neighborIndex = 0;
            neighborIndex < previousOrganismNeighborCount;
            ++neighborIndex) {
        seeds[static_cast<std::size_t>(seedCount++)] =
                previousOrganismNeighbors[static_cast<std::size_t>(neighborIndex)];
    }
    seeds[static_cast<std::size_t>(seedCount++)] = child1Index;
    seeds[static_cast<std::size_t>(seedCount++)] = child2Index;

    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex) {
        const int seed = seeds[static_cast<std::size_t>(seedIndex)];
        bool alreadyClassified = false;
        for (int componentIndex = 0;
                componentIndex < componentCount;
                ++componentIndex) {
            if (mOrganismTraversalMarks[static_cast<std::size_t>(seed)]
                    == components[static_cast<std::size_t>(componentIndex)]
                            .traversalGeneration) {
                alreadyClassified = true;
                break;
            }
        }
        if (alreadyClassified) {
            continue;
        }

        const int size = collectOrganismComponent(
                seed, inheritedOrganismId);
        const bool containsChild1 =
                mOrganismTraversalMarks[static_cast<std::size_t>(child1Index)]
                == mOrganismTraversalGeneration;
        const bool containsChild2 =
                mOrganismTraversalMarks[static_cast<std::size_t>(child2Index)]
                == mOrganismTraversalGeneration;
        components[static_cast<std::size_t>(componentCount++)] =
                SplitComponent{
                        seed,
                        size,
                        size - static_cast<int>(containsChild1)
                                - static_cast<int>(containsChild2),
                        containsChild1,
                        mOrganismTraversalGeneration
                };
    }

    int continuingComponent = -1;
    for (int componentIndex = 0;
            componentIndex < componentCount;
            ++componentIndex) {
        const SplitComponent& candidate =
                components[static_cast<std::size_t>(componentIndex)];
        if (continuingComponent < 0) {
            continuingComponent = componentIndex;
            continue;
        }
        const SplitComponent& current = components[static_cast<std::size_t>(continuingComponent)];
        if (candidate.preSplitCellCount > current.preSplitCellCount
                || (candidate.preSplitCellCount == current.preSplitCellCount
                        && candidate.size > current.size)
                || (candidate.preSplitCellCount == current.preSplitCellCount
                        && candidate.size == current.size
                        && candidate.containsChild1
                        && !current.containsChild1)) {
            continuingComponent = componentIndex;
        }
    }

    for (int componentIndex = 0;
            componentIndex < componentCount;
            ++componentIndex) {
        if (componentIndex == continuingComponent) {
            continue;
        }
        const int representative = components[static_cast<std::size_t>(componentIndex)].representative;
        if (mOrganismIds[static_cast<std::size_t>(representative)]
                != inheritedOrganismId) {
            continue;
        }
        const int componentSize = collectOrganismComponent(
                representative, inheritedOrganismId);
        assignNewOrganismToCollectedComponent(componentSize,
                inheritedOrganismId);
    }
}

bool CellData::initializeDefaultGenomeHandle(int cellIndex) {
    cellularity::GenomeId defaultId = mDefaultGenomeId;
    bool createdTemplate = false;
    if (!mGenomePool.isLive(defaultId)) {
        const cellularity::GenomePoolResult created =
                mGenomePool.createDefault();
        if (!created.hasValue()) {
            return false;
        }
        defaultId = created.id;
        mDefaultGenomeId = defaultId;
        createdTemplate = true;
    }

    // createDefault() contributes the reference owned by CellData. Every Cell
    // receives another reference, including the first Cell using the template.
    if (mGenomePool.retain(defaultId)
            != cellularity::GenomePoolError::None) {
        if (createdTemplate) {
            const cellularity::GenomePoolError released =
                    mGenomePool.release(defaultId);
            (void)released;
            assert(released == cellularity::GenomePoolError::None);
            mDefaultGenomeId = cellularity::INVALID_GENOME_ID;
        }
        return false;
    }
    installOwnedGenomeHandle(cellIndex, defaultId, cellularity::GeneIndex(0));
    const std::size_t index = static_cast<std::size_t>(cellIndex);
    const cellularity::GeneView activeGene =
            *mGenomePool.geneView(defaultId, cellularity::GeneIndex(0));
    mActiveCellType[index] = activeGene.cellType();
    mActiveCellCollisionElasticity[index] =
            0.5 / activeGene.floatValue(Gene::FloatType::CYTOSKELETON);
    return true;
}

void CellData::releaseGenomeHandle(int cellIndex) noexcept {
    const std::size_t index = static_cast<std::size_t>(cellIndex);
    const cellularity::GenomeId id = mGenomeIds[index];
    if (id != cellularity::INVALID_GENOME_ID) {
        const cellularity::GenomePoolError released = mGenomePool.release(id);
        (void)released;
        assert(released == cellularity::GenomePoolError::None);
    }
    mGenomeIds[index] = cellularity::INVALID_GENOME_ID;
    mGeneIndices[index] = cellularity::INVALID_GENE_INDEX;
    mActiveGeneIds[index] = cellularity::INVALID_GENE_ID;
    mActiveGeneData[index] = nullptr;
    mActiveGeneInitialFlags[index] = 0;
}

bool CellData::copyGenomeHandle(
        int fromCellIndex,
        int toCellIndex) noexcept {
    if (fromCellIndex == toCellIndex) {
        return true;
    }
    const std::size_t from = static_cast<std::size_t>(fromCellIndex);
    const std::size_t to = static_cast<std::size_t>(toCellIndex);
    const cellularity::GenomeId source = mGenomeIds[from];
    if (source == cellularity::INVALID_GENOME_ID
            || mGenomePool.retain(source)
                    != cellularity::GenomePoolError::None) {
        return false;
    }
    releaseGenomeHandle(toCellIndex);
    mGenomeIds[to] = source;
    mGeneIndices[to] = mGeneIndices[from];
    mActiveGeneIds[to] = mActiveGeneIds[from];
    mActiveGeneData[to] = mActiveGeneData[from];
    mActiveGeneInitialFlags[to] = mActiveGeneInitialFlags[from];
    mActiveCellType[to] = mActiveCellType[from];
    mActiveCellCollisionElasticity[to] =
            mActiveCellCollisionElasticity[from];
    return true;
}

void CellData::moveGenomeHandle(
        int fromCellIndex,
        int toCellIndex) noexcept {
    if (fromCellIndex == toCellIndex) {
        return;
    }
    const std::size_t from = static_cast<std::size_t>(fromCellIndex);
    const std::size_t to = static_cast<std::size_t>(toCellIndex);
    releaseGenomeHandle(toCellIndex);
    mGenomeIds[to] = mGenomeIds[from];
    mGeneIndices[to] = mGeneIndices[from];
    mActiveGeneIds[to] = mActiveGeneIds[from];
    mActiveGeneData[to] = mActiveGeneData[from];
    mActiveGeneInitialFlags[to] = mActiveGeneInitialFlags[from];
    mActiveCellType[to] = mActiveCellType[from];
    mActiveCellCollisionElasticity[to] =
            mActiveCellCollisionElasticity[from];
    mGenomeIds[from] = cellularity::INVALID_GENOME_ID;
    mGeneIndices[from] = cellularity::INVALID_GENE_INDEX;
    mActiveGeneIds[from] = cellularity::INVALID_GENE_ID;
    mActiveGeneData[from] = nullptr;
    mActiveGeneInitialFlags[from] = 0;
}

void CellData::installOwnedGenomeHandle(
        int cellIndex,
        cellularity::GenomeId genomeId,
        cellularity::GeneIndex activeGeneIndex) noexcept {
    releaseGenomeHandle(cellIndex);
    const std::size_t index = static_cast<std::size_t>(cellIndex);
    mGenomeIds[index] = genomeId;
    mGeneIndices[index] = activeGeneIndex;
    refreshActiveGeneCache(cellIndex);
}

void CellData::refreshActiveGeneCache(int cellIndex) noexcept {
    const std::size_t index = static_cast<std::size_t>(cellIndex);
    const auto activeId = mGenomePool.geneId(
            mGenomeIds[index], mGeneIndices[index]);
    const auto metadata = mGenomePool.metadata(mGenomeIds[index]);
    if (!activeId.has_value() || !metadata.has_value()) {
        mActiveGeneIds[index] = cellularity::INVALID_GENE_ID;
        mActiveGeneData[index] = nullptr;
        mActiveGeneInitialFlags[index] = 0;
        return;
    }
    mActiveGeneIds[index] = *activeId;
    mActiveGeneData[index] = mGenePool.dataPointer(*activeId);
    mActiveGeneInitialFlags[index] = static_cast<std::uint8_t>(
            mGeneIndices[index] == metadata->initialGeneIndex);
    assert(mActiveGeneData[index] != nullptr);
}

void CellData::addRandomCell(double positionX, double positionY, Environment& environment) {
    int newCellIndex = allocateNewCell();
    if (newCellIndex == -1) {
        return;
    }

    (void)assignNewOrganism(newCellIndex);

    mPositionX[newCellIndex] = positionX;
    mPositionY[newCellIndex] = positionY;
    mMass[newCellIndex] = MAX_MASS;
    mRadius[newCellIndex] = std::sqrt(mMass[newCellIndex] / MASS_PER_AREA);
    refreshRadiusSq(newCellIndex);
    mOriginTag[newCellIndex] = SimulationWorld::RANDOM_CELL_ORIGIN;
    mSelectTag[newCellIndex] = 0;
    mNitrates[newCellIndex] = MAX_NITRATES;
    mTelomeresCount[newCellIndex] = -1;
    mIsMirrored[newCellIndex] = environment.mRandom.nextInt(2) == 1;

    bool hasNeuralGenePool = environment.mGenePool[CellTypes::ordinal(CellType::NEUROCYTE)]
            || environment.mGenePool[CellTypes::ordinal(CellType::SENSEOCYTE)]
            || environment.mGenePool[CellTypes::ordinal(CellType::STEREOCYTE)];
    const int randomGenomeGeneCount = MIN_RANDOM_FOUNDER_GENE_COUNT
            + environment.mRandom.nextInt(
                    MAX_RANDOM_FOUNDER_GENE_COUNT
                    - MIN_RANDOM_FOUNDER_GENE_COUNT + 1);
    const int activeSeedGeneCount = randomGenomeGeneCount;
    cellularity::GenomeData randomGenome(
            static_cast<std::size_t>(randomGenomeGeneCount));
    for (int geneIndex = 0;
            geneIndex < static_cast<int>(randomGenome.size());
            geneIndex++) {
        GeneData& gene = randomGenome[geneIndex];
        cellularity::GeneBuilder geneBuilder(gene);
        geneBuilder.setSplitMass(randomSeedSplitMass(environment.mRandom));
        geneBuilder.setColor(
                0, static_cast<float>(environment.mRandom.nextUniform()));
        geneBuilder.setColor(
                1, static_cast<float>(environment.mRandom.nextUniform()));
        geneBuilder.setColor(
                2, static_cast<float>(environment.mRandom.nextUniform()));
        geneBuilder.setSplitAngle(static_cast<float>(
                environment.mRandom.nextUniform() * 2.0 * std::acos(-1.0)));
        geneBuilder.setSplitRatio(static_cast<float>(
                0.5 + environment.mRandom.nextUniformSymmetric() * 0.2));
        geneBuilder.setChild1Angle(static_cast<float>(
                environment.mRandom.nextUniform() * 2.0 * std::acos(-1.0)));
        geneBuilder.setChild2Angle(static_cast<float>(
                environment.mRandom.nextUniform() * 2.0 * std::acos(-1.0)));
        geneBuilder.setMassFlowPriority(static_cast<float>(
                MIN_MASS_FLOW_PRIORITY
                + environment.mRandom.nextUniform()
                        * (MAX_MASS_FLOW_PRIORITY - MIN_MASS_FLOW_PRIORITY)));

        CellType cellType;
        do {
            cellType = CellTypes::values[environment.mRandom.nextInt(CellTypes::length)];
        } while (!environment.mGenePool[CellTypes::ordinal(cellType)]);
        geneBuilder.setCellType(cellType);

        geneBuilder.setChild1GeneIndex(cellularity::GeneIndex(
                seedTreeChildGeneIndex(
                        geneIndex, 1, activeSeedGeneCount, environment.mRandom)));
        geneBuilder.setChild2GeneIndex(cellularity::GeneIndex(
                seedTreeChildGeneIndex(
                        geneIndex, 2, activeSeedGeneCount, environment.mRandom)));
        geneBuilder.setMakesLink(environment.mRandom.nextInt(2) == 1);
        geneBuilder.setChild1KeepsLinks(environment.mRandom.nextInt(2) == 1);
        geneBuilder.setChild2KeepsLinks(environment.mRandom.nextInt(2) == 1);
        geneBuilder.setStaysAlive(environment.mRandom.nextInt(2) == 1);
        geneBuilder.setChild1Mirrored(environment.mRandom.nextInt(2) == 1);
        geneBuilder.setChild2Mirrored(environment.mRandom.nextInt(2) == 1);
        geneBuilder.setLinkStrength(
                static_cast<float>(environment.mRandom.nextUniform())
                * MAX_LINK_STIFFNESS);

        for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
            if (hasNeuralGenePool) {
                geneBuilder.setParametricFormulaType(
                        valueIndex, 1 + environment.mRandom.nextInt(3));
                geneBuilder.setParametricSignalChannel(
                        valueIndex,
                        environment.mRandom.nextInt(
                                SIGNAL_SUBSTANCE_COUNT
                                + AUX_SIGNAL_SUBSTANCE_COUNT));
                geneBuilder.setParametricVariableB(
                        valueIndex,
                        static_cast<float>(
                                environment.mRandom.nextUniformSymmetric())
                                * VALUE_VARIABLE_MAX);
                geneBuilder.setParametricVariableA(
                        valueIndex,
                        static_cast<float>(
                                environment.mRandom.nextUniformSymmetric())
                                * VALUE_VARIABLE_MAX);
                geneBuilder.setParametricVariableC(
                        valueIndex,
                        static_cast<float>(
                                environment.mRandom.nextUniformSymmetric())
                                * VALUE_VARIABLE_MAX);
            } else {
                geneBuilder.setParametricFormulaType(valueIndex, 0);
                geneBuilder.setParametricSignalChannel(
                        valueIndex,
                        environment.mRandom.nextInt(
                                SIGNAL_SUBSTANCE_COUNT
                                + AUX_SIGNAL_SUBSTANCE_COUNT));
                geneBuilder.setParametricVariableB(
                        valueIndex,
                        static_cast<float>(
                                environment.mRandom.nextUniformSymmetric()));
                geneBuilder.setParametricVariableA(valueIndex, 0.0f);
                geneBuilder.setParametricVariableC(valueIndex, 0.0f);
            }
        }

        for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
            const int integerMaximum =
                    integerIndex == Gene::VIRUS_FROM_INTEGER_INDEX
                            || integerIndex == Gene::VIRUS_TO_INTEGER_INDEX
                    ? static_cast<int>(randomGenome.size())
                    : Gene::INTEGER_MAX[integerIndex];
            geneBuilder.setIntegerValue(
                    integerIndex,
                    environment.mRandom.nextInt(integerMaximum));
        }
        if (cellType != CellType::GAMETE) {
            geneBuilder.setIntegerValue(Gene::TELOMERASE_LENGTH_INDEX, 0);
        }

        const auto setRandomFloat = [&](Gene::FloatType type) {
            const cellularity::GeneView view = geneBuilder.view();
            const float minimum = view.floatMinimum(type);
            const float maximum = view.floatMaximum(type);
            geneBuilder.setFloatValue(
                    type,
                    minimum
                            + static_cast<float>(
                                      environment.mRandom.nextUniform())
                                    * (maximum - minimum));
        };
        setRandomFloat(Gene::FloatType::SMELL_AMPLITUDE);
        setRandomFloat(Gene::FloatType::SMELL_RED);
        setRandomFloat(Gene::FloatType::SMELL_GREEN);
        setRandomFloat(Gene::FloatType::SMELL_BLUE);
        setRandomFloat(Gene::FloatType::SMELL_THRESHOLD);
        setRandomFloat(Gene::FloatType::ADHESIN_LENGTH);
        setRandomFloat(Gene::FloatType::CYTOSKELETON);
    }
    randomizeReceiverSignalInputs(randomGenome, environment.mRandom);
    randomizeGameteCompatibilityTargets(randomGenome, environment.mRandom);
    addInternalSeedTreeCycle(randomGenome.data(), activeSeedGeneCount, environment.mRandom);
    cellularity::GeneBuilder(randomGenome[0]).setInitialCompatibilityFlag(true);

    const cellularity::GeneView generatedActiveGene(randomGenome[0]);
    if (generatedActiveGene.splitMass() >= MAX_MASS) {
        cellularity::GeneBuilder(randomGenome[0]).setSplitMass(randomSplittableSeedSplitMass(environment.mRandom));
    }
    if (!replaceGenome(newCellIndex, randomGenome, cellularity::GeneIndex(0))) {
        releaseLastCellSlot();
        return;
    }
    const cellularity::GeneView activeGene = getActiveGeneView(newCellIndex);

    if (activeGene.cellType() == CellType::LIPOCYTE) {
        mLipids[newCellIndex] = MAX_LIPIDS * static_cast<float>(environment.mRandom.nextUniform());
    }

    mColor[newCellIndex][0] = activeGene.color(0);
    mColor[newCellIndex][1] = activeGene.color(1);
    mColor[newCellIndex][2] = activeGene.color(2);
    mColor[newCellIndex][3] = 1.0f;
    mVelocityX[newCellIndex] = 0.0;
    mVelocityY[newCellIndex] = 0.0;
    mAngularVelocity[newCellIndex] = 0.0;
    mAngularVelocityCytoplasm[newCellIndex] = 0.0;
    mAngle[newCellIndex] = environment.mRandom.nextUniformSymmetric() * std::acos(-1.0);
    mAngleCytoplasm[newCellIndex] = environment.mRandom.nextUniformSymmetric() * std::acos(-1.0);
    mAge[newCellIndex] = 0.0;
    mMutationCount[newCellIndex] = 0;
    markCellAsAlive(newCellIndex);
}

void CellData::addRandomCells(Environment& environment) {
    if (!environment.addRandom) {
        return;
    }

    bool fine = false;
    for (bool allowed : environment.mGenePool) {
        if (allowed) {
            fine = true;
            break;
        }
    }
    if (!fine) {
        return;
    }

    while (mCellCount < RANDOM_CELLS_MAINTAIN_COUNT) {
        const auto point = environment.geometry().samplePoint(environment.mRandom);
        addRandomCell(point[0], point[1], environment);
    }

    if (environment.mRandom.nextInt(15) == 0) {
        const auto point = environment.geometry().samplePoint(environment.mRandom);
        addRandomCell(point[0], point[1], environment);
    }
}

int CellData::addLink(int currentCellIndex,
        int victimCellIndex,
        CellLinkCreationReason creationReason,
        double angleA,
        double angleB,
        float stiffness,
        float length) {
    if (victimCellIndex == BOUNDARY_CONNECTION_INDEX) {
        if (!mTopology.hasBoundaryLink(currentCellIndex)) {
            int boundaryIndex = mTopology.addBoundaryLink(currentCellIndex, angleA, 0.0, 0.0, stiffness, length);
            if (boundaryIndex < 0) {
                return -1;
            }
        }
    } else if (!mTopology.isLinked(currentCellIndex, victimCellIndex)) {
        int edgeIndex = mTopology.addCellLink(
                currentCellIndex,
                victimCellIndex,
                creationReason,
                angleA,
                angleB,
                stiffness,
                length);
        if (edgeIndex < 0) {
            return -1;
        }
    }

    return 0;
}

void CellData::markCellAsAlive(int newCellIndex) {
    refreshActiveCellType(newCellIndex);
    mIsDead[newCellIndex] = false;
}

void CellData::markCellAsDead(int clickedCellIndex) {
#if defined(CELLULARITY_SEMANTIC_TRACE)
    const bool wasDead = mIsDead[clickedCellIndex];
#endif
    mIsDead[clickedCellIndex] = true;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::CellDeath,
            cellularity::verification::traceI32(clickedCellIndex),
            cellularity::verification::traceBool(wasDead));
}

void CellData::applySupportTool(int clickedCellIndex) {
    mMass[clickedCellIndex] = MAX_MASS;
    mColor[clickedCellIndex][0] = 1.0f;
    mColor[clickedCellIndex][1] = 0.0f;
    mColor[clickedCellIndex][2] = 1.0f;
    mNitrates[clickedCellIndex] = 1.0;
    mAge[clickedCellIndex] = 0.0;
    if (mActiveCellType[clickedCellIndex] == CellType::LIPOCYTE) {
        mLipids[clickedCellIndex] = MAX_LIPIDS;
    }
}
