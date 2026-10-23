#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "core/CanonicalGene.h"
#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/GeneAccess.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity;
using namespace cellularity::tests;

namespace {

GeneIndex indexOf(std::size_t value) {
    return GeneIndex(static_cast<GeneIndex::Value>(value));
}

std::size_t circularIndex(
        std::size_t index,
        std::size_t offset,
        std::size_t count) {
    return (index + offset) % count;
}

GenomeData makeDynamicGenome(std::size_t geneCount) {
    GenomeData genome(geneCount);
    for (std::size_t index = 0; index < geneCount; ++index) {
        const auto gene = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                indexOf(index),
                geneCount,
                index + 1 == geneCount);
        assertTrue(gene.has_value(), "Dynamic operation Gene materializes");
        genome[index] = *gene;
        GeneBuilder builder(genome[index]);
        builder.setCellType(CellType::PHAGOCYTE);
        builder.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        builder.setSplitRatio(0.5f);
        builder.setChild1GeneIndex(indexOf(index));
        builder.setChild2GeneIndex(indexOf(index));
        builder.setVirusFromGeneIndex(indexOf(index));
        builder.setVirusToGeneIndex(indexOf(index));
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }
    return genome;
}

void prepareSplit(CellData& cells, int cell) {
    makeCellSplitReady(cells, cell);
    cells.mOldLinkCount[cell] = 0;
}

std::int64_t seedThatCopiesIndex(
        std::int64_t seed,
        std::size_t crossoverCount,
        std::size_t requiredIndex) {
    for (int attempt = 0; attempt < 4096; ++attempt, ++seed) {
        SimulationRandom probe(seed);
        for (std::size_t index = 0; index < crossoverCount; ++index) {
            const bool copied = probe.nextInt(2) == 1;
            if (index == requiredIndex && copied) {
                return seed;
            }
        }
    }
    throw std::runtime_error("Could not find deterministic crossover seed");
}

void verifyCrossSizeFertilization(
        std::size_t eggCount,
        std::size_t spermCount,
        std::int64_t seedHint) {
    const std::size_t crossoverCount = std::min(eggCount, spermCount);
    const std::size_t eggInitial = eggCount - 1;
    const std::size_t spermInitial = spermCount - 1;
    const std::int64_t seed =
            eggInitial < crossoverCount && eggInitial != spermInitial
            ? seedThatCopiesIndex(seedHint, crossoverCount, eggInitial)
            : seedHint;

    Environment environment = Environment::constructDefaultEnvironment(
            2, 0, 1.0);
    environment.mRandom.setSeed(seed);
    SimulationRandom expectedRandom(seed);
    CellData cells(2);
    const int egg = addAliveCell(cells, CellType::GAMETE);
    const int sperm = addAliveCell(cells, CellType::GAMETE);
    GenomeData eggGenome = makeDynamicGenome(eggCount);
    GenomeData spermGenome = makeDynamicGenome(spermCount);

    for (GeneData& gene : eggGenome) {
        GeneBuilder builder(gene);
        builder.setCellType(CellType::PHAGOCYTE);
        builder.setColor(0, 0.125f);
    }
    for (GeneData& gene : spermGenome) {
        GeneBuilder builder(gene);
        builder.setCellType(CellType::STEREOCYTE);
        builder.setColor(0, 0.875f);
        builder.setChild1GeneIndex(indexOf(spermInitial));
        builder.setChild2GeneIndex(indexOf(spermInitial));
        builder.setVirusFromGeneIndex(indexOf(spermInitial));
        builder.setVirusToGeneIndex(indexOf(spermInitial));
    }
    GeneBuilder eggActive(eggGenome[0]);
    eggActive.setVirusFromGeneIndex(indexOf(eggInitial));
    eggActive.setIntegerValue(Gene::TELOMERASE_LENGTH_INDEX, 2);

    assertTrue(cells.replaceGenome(egg, eggGenome, GeneIndex(0)),
            "Cross-size fertilization installs egg");
    assertTrue(cells.replaceGenome(sperm, spermGenome, GeneIndex(0)),
            "Cross-size fertilization installs sperm");

    std::vector<bool> copied(crossoverCount, false);
    for (std::size_t index = 0; index < crossoverCount; ++index) {
        copied[index] = expectedRandom.nextInt(2) == 1;
    }
    assertTrue(cells.applyCellFertilization(sperm, egg, environment),
            "Cross-size fertilization succeeds");

    assertTrue(cells.mIsDead[sperm], "Fertilization consumes sperm");
    assertTrue(cells.geneCount(egg) == eggCount,
            "Fertilization keeps egg length");
    assertTrue(cells.activeGeneIndex(egg) == indexOf(eggInitial),
            "Fertilization keeps egg-domain active target");
    assertTrue(cells.mTelomeresCount[egg] == Gene::TELOMERASE_LENGTHS[2],
            "Fertilization keeps egg telomerase behavior");

    const std::size_t remappedSpermReference = spermInitial % eggCount;
    const GenomeData result = cells.materializeGenome(egg);
    for (std::size_t index = 0; index < eggCount; ++index) {
        const GeneView gene(result[index]);
        const bool wasCopied = index < crossoverCount && copied[index];
        const std::size_t ordinaryReference = wasCopied
                ? remappedSpermReference
                : index;
        const std::size_t virusFromReference = wasCopied
                ? remappedSpermReference
                : (index == 0 ? eggInitial : index);
        assertTrue(gene.cellType()
                        == (wasCopied
                                        ? CellType::STEREOCYTE
                                        : CellType::PHAGOCYTE),
                "Fertilization copies only overlap-domain payloads");
        assertNear(wasCopied ? 0.875 : 0.125,
                gene.color(0),
                0.0,
                "Fertilization crossover is deterministic");
        assertTrue(gene.child1GeneIndex() == indexOf(ordinaryReference)
                        && gene.child2GeneIndex()
                                == indexOf(ordinaryReference)
                        && gene.virusFromGeneIndex()
                                == indexOf(virusFromReference)
                        && gene.virusToGeneIndex()
                                == indexOf(ordinaryReference),
                "Fertilization remaps every copied reference");
        assertTrue(gene.initialCompatibilityFlag()
                        == (index == eggInitial),
                "Fertilization preserves egg initial metadata");
    }
}

void testCrossSizeFertilization() {
    verifyCrossSizeFertilization(1, 256, 0x8010);
    verifyCrossSizeFertilization(2, 1, 0x8020);
    verifyCrossSizeFertilization(40, 40, 0x8030);
    verifyCrossSizeFertilization(150, 40, 0x8040);
    verifyCrossSizeFertilization(2, 150, 0x8050);
}

void verifyCrossSizeVirus(
        std::size_t sourceCount,
        std::size_t victimCount,
        std::size_t sourceStart,
        std::size_t targetStart) {
    Environment environment = Environment::constructDefaultEnvironment(
            2, 0, 1.0);
    environment.mRandom.setSeed(static_cast<std::int64_t>(
            0x9000 + sourceCount * 7 + victimCount));
    CellData cells(2);
    const int virus = addAliveCell(cells, CellType::VIROCYTE);
    const int victim = addAliveCell(cells, CellType::PHAGOCYTE);
    GenomeData virusGenome = makeDynamicGenome(sourceCount);
    GenomeData victimGenome = makeDynamicGenome(victimCount);

    for (std::size_t index = 0; index < sourceCount; ++index) {
        GeneBuilder builder(virusGenome[index]);
        builder.setCellType(index == 0
                        ? CellType::VIROCYTE
                        : CellType::SECROCYTE);
        builder.setColor(
                0,
                static_cast<float>(index + 1)
                        / static_cast<float>(
                                MAX_SUPPORTED_GENES_PER_GENOME + 1));
        builder.setChild1GeneIndex(indexOf(
                circularIndex(index, 1, sourceCount)));
        builder.setChild2GeneIndex(indexOf(
                circularIndex(index, sourceCount - 1, sourceCount)));
        builder.setVirusFromGeneIndex(indexOf(
                circularIndex(index, 3, sourceCount)));
        builder.setVirusToGeneIndex(indexOf(
                circularIndex(index, 4, sourceCount)));
    }
    GeneBuilder activeVirus(virusGenome[0]);
    activeVirus.setVirusFromGeneIndex(indexOf(sourceStart));
    activeVirus.setVirusToGeneIndex(indexOf(targetStart));
    for (GeneData& gene : victimGenome) {
        GeneBuilder builder(gene);
        builder.setCellType(CellType::PHAGOCYTE);
        builder.setColor(0, 0.0f);
    }

    assertTrue(cells.replaceGenome(virus, virusGenome, GeneIndex(0)),
            "Cross-size virus installs source");
    assertTrue(cells.replaceGenome(victim, victimGenome, GeneIndex(0)),
            "Cross-size virus installs victim");
    cells.mOriginTag[victim] = SimulationWorld::RANDOM_CELL_ORIGIN;

    const std::size_t child1 = circularIndex(sourceStart, 1, sourceCount);
    const std::size_t child2 = circularIndex(
            sourceStart, sourceCount - 1, sourceCount);
    const std::array<std::size_t, 7> traversal{
            sourceStart,
            child1,
            child2,
            circularIndex(child1, 1, sourceCount),
            circularIndex(child1, sourceCount - 1, sourceCount),
            circularIndex(child2, 1, sourceCount),
            circularIndex(child2, sourceCount - 1, sourceCount)
    };
    const std::size_t normalizedTarget = targetStart % victimCount;
    std::vector<int> expectedSource(victimCount, -1);
    for (const std::size_t source : traversal) {
        const std::size_t offset =
                (source + sourceCount - sourceStart) % sourceCount;
        expectedSource[(normalizedTarget + offset) % victimCount] =
                static_cast<int>(source);
    }

    cells.handleVirocyteCollision(virus, victim, 1.0, environment);

    assertTrue(cells.mOriginTag[victim]
                    == SimulationWorld::INFECTED_RANDOM_CELL_ORIGIN,
            "Virus commits origin after transfer");
    assertTrue(cells.geneCount(victim) == victimCount,
            "Virus keeps victim length");
    const GenomeData result = cells.materializeGenome(victim);
    for (std::size_t destination = 0;
            destination < victimCount;
            ++destination) {
        const GeneView gene(result[destination]);
        const int source = expectedSource[destination];
        if (source < 0) {
            assertTrue(gene.cellType() == CellType::PHAGOCYTE,
                    "Virus preserves untouched victim Gene");
            assertNear(0.0, gene.color(0), 0.0,
                    "Virus preserves untouched victim payload");
        } else {
            const std::size_t sourceIndex = static_cast<std::size_t>(source);
            const float expectedColor =
                    static_cast<float>(sourceIndex + 1)
                    / static_cast<float>(
                            MAX_SUPPORTED_GENES_PER_GENOME + 1);
            const std::size_t sourceVirusFrom = sourceIndex == 0
                    ? sourceStart
                    : circularIndex(sourceIndex, 3, sourceCount);
            const std::size_t sourceVirusTo = sourceIndex == 0
                    ? targetStart
                    : circularIndex(sourceIndex, 4, sourceCount);
            assertNear(expectedColor, gene.color(0), 0.0,
                    "Virus folded write order is deterministic");
            assertTrue(
                    gene.child1GeneIndex()
                                    == indexOf(circularIndex(
                                                       sourceIndex,
                                                       1,
                                                       sourceCount)
                                            % victimCount)
                            && gene.child2GeneIndex()
                                    == indexOf(circularIndex(
                                                       sourceIndex,
                                                       sourceCount - 1,
                                                       sourceCount)
                                            % victimCount)
                            && gene.virusFromGeneIndex()
                                    == indexOf(sourceVirusFrom % victimCount)
                            && gene.virusToGeneIndex()
                                    == indexOf(sourceVirusTo % victimCount),
                    "Virus remaps every copied reference");
        }
        assertTrue(gene.child1GeneIndex().value() < victimCount
                        && gene.child2GeneIndex().value() < victimCount
                        && gene.virusFromGeneIndex().value() < victimCount
                        && gene.virusToGeneIndex().value() < victimCount,
                "Virus leaves all victim references in bounds");
        assertTrue(gene.initialCompatibilityFlag()
                        == (destination + 1 == victimCount),
                "Virus preserves victim initial metadata");
    }
}

void testCrossSizeVirus() {
    verifyCrossSizeVirus(1, 256, 0, 0);
    verifyCrossSizeVirus(2, 1, 1, 1);
    verifyCrossSizeVirus(40, 2, 39, 39);
    verifyCrossSizeVirus(150, 40, 149, 149);
    verifyCrossSizeVirus(256, 150, 255, 255);
}

void testDeferredMutationDetachesSplittingLineage() {
    Environment environment = Environment::constructDefaultEnvironment(
            3, 0, 1.0);
    environment.mutationOnlyAfterSplit = true;
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(0xA081);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;
    const int parent = addAliveCell(cells, CellType::PHAGOCYTE);
    GenomeData genome = makeDynamicGenome(150);
    GeneBuilder root(genome[0]);
    root.setChild1GeneIndex(GeneIndex(0));
    root.setChild2GeneIndex(GeneIndex(0));
    assertTrue(cells.replaceGenome(parent, genome, GeneIndex(0)),
            "Deferred mutation installs dynamic Genome");
    prepareSplit(cells, parent);
    cells.handleCellSplit(world);

    assertTrue(cells.mCellCount == 2
                    && cells.genomeId(0) == cells.genomeId(1),
            "First split establishes shared sibling lineages");
    const GenomeData untouched = cells.materializeGenome(1);
    const GenomeId untouchedId = cells.genomeId(1);
    cells.mAge[1] = 0.0;
    prepareSplit(cells, 0);
    cells.mPendingMutationCount[0] = 100;
    cells.mMutationCount[0] = 100;
    cells.handleCellSplit(world);

    assertTrue(cells.mCellCount == 3,
            "Only ready shared lineage splits");
    assertTrue(cells.genomeId(1) == untouchedId
                    && sameGenome(untouched, cells, 1),
            "Deferred mutation does not cross shared lineage");
    assertTrue(cells.genomeId(0) == cells.genomeId(2),
            "Deferred mutation shares the mutated daughter record");
    assertTrue(cells.genomeId(0) != cells.genomeId(1),
            "Deferred mutation detaches from the untouched lineage");
    assertTrue(cells.genomeReferenceCount(0) == 2
                    && cells.genomeReferenceCount(1) == 1,
            "Deferred mutation balances daughter and sibling ownership");
    assertTrue(!sameGenome(untouched, cells, 0)
                    && sameGenome(
                            cells.materializeGenome(0),
                            cells.materializeGenome(2)),
            "Deferred mutation changes both intended daughters");
    assertTrue(cells.genomePool().validateInvariants().isValid()
                    && cells.genePool().validateInvariants().isValid(),
            "Deferred mutation preserves pool ownership");
}

void assertDynamicGenomeReferencesAreValid(
        const CellData& cells,
        int cell,
        const char* context) {
    const GenomeData genome = cells.materializeGenome(cell);
    int initialCount = 0;
    for (const GeneData& data : genome) {
        const GeneView gene(data);
        initialCount += gene.initialCompatibilityFlag() ? 1 : 0;
        assertTrue(gene.child1GeneIndex().value() < genome.size()
                        && gene.child2GeneIndex().value() < genome.size()
                        && gene.virusFromGeneIndex().value() < genome.size()
                        && gene.virusToGeneIndex().value() < genome.size(),
                context);
    }
    assertTrue(initialCount == 1,
            "Dynamic length mutation preserves one initial Gene");
    assertTrue(cells.activeGeneIndex(cell).value() < genome.size(),
            "Dynamic length mutation preserves active Gene");
}

void triggerImmediateMutation(
        CellData& cells,
        int cell,
        Environment& environment) {
    cells.mMass[cell] = CellData::INITIAL_MASS;
    cells.mRadius[cell] = std::sqrt(
            cells.mMass[cell] / CellData::MASS_PER_AREA);
    cells.refreshRadiusSq(cell);
    cells.mNitrates[cell] = CellData::MAX_NITRATES;
    cells.mSeldomCounter[cell] = CellData::MUTATE_PERIOD - 1;
    cells.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);
}

void testEvolutionAddsAndDeletesIndividualGenes() {
    Environment environment = Environment::constructDefaultEnvironment(
            1, 0, 1.0);
    environment.aging = false;
    environment.mRadiation = 1000.0;
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(0xD1A6);
    environment.mMutationWeights.fill(0.0);
    environment.mMutationWeights[Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX] = 1.0;

    CellData cells(1);
    const int cell = addAliveCell(cells, CellType::PHAGOCYTE);
    assertTrue(cells.replaceGenome(
                       cell, makeDynamicGenome(1), GeneIndex(0)),
            "Dynamic length mutation installs one-Gene founder");

    triggerImmediateMutation(cells, cell, environment);

    assertTrue(cells.geneCount(cell)
                    == 1U + CellData::SIMULTANEOUS_MUTATIONS_COUNT,
            "Evolution adds one Gene per selected insertion mutation");
    assertDynamicGenomeReferencesAreValid(
            cells,
            cell,
            "Evolutionary insertion keeps every local reference in range");

    environment.mMutationWeights.fill(0.0);
    environment.mMutationWeights[Environment::MUTATION_DELETE_GENE_WEIGHT_INDEX] = 1.0;
    triggerImmediateMutation(cells, cell, environment);

    assertTrue(cells.geneCount(cell) == 1,
            "Evolution deletes one Gene per selected deletion mutation");
    assertDynamicGenomeReferencesAreValid(
            cells,
            cell,
            "Evolutionary deletion keeps every local reference in range");

    const int mutationsAtMinimum = cells.mMutationCount[cell];
    triggerImmediateMutation(cells, cell, environment);
    assertTrue(cells.geneCount(cell) == 1
                    && cells.mMutationCount[cell] == mutationsAtMinimum,
            "Evolution cannot delete the final Gene");

    assertTrue(cells.replaceGenome(
                       cell,
                       makeDynamicGenome(MAX_SUPPORTED_GENES_PER_GENOME),
                       GeneIndex(0)),
            "Dynamic length mutation installs maximum-size Genome");
    environment.mMutationWeights.fill(0.0);
    environment.mMutationWeights[Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX] = 1.0;
    const int mutationsAtMaximum = cells.mMutationCount[cell];
    triggerImmediateMutation(cells, cell, environment);
    assertTrue(cells.geneCount(cell) == MAX_SUPPORTED_GENES_PER_GENOME
                    && cells.mMutationCount[cell] == mutationsAtMaximum,
            "Evolution cannot exceed the Genome safety limit");
}

} // namespace

int main() {
    testCrossSizeFertilization();
    testCrossSizeVirus();
    testDeferredMutationDetachesSplittingLineage();
    testEvolutionAddsAndDeletesIndividualGenes();
    std::cout << "dynamic Genome operation tests passed\n";
    return 0;
}
