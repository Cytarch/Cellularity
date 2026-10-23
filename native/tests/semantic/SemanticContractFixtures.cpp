#include "tests/semantic/SemanticContractFixtures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/SimulationRandom.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "tests/support/StableDigest.h"
#include "verification/SemanticTrace.h"

namespace cellularity::semantic_tests {
namespace {

constexpr std::int64_t kFixtureSeed = 0x00A0'0206'0919LL;
constexpr double kDefaultRadius = 0.02;

using verification::ScopedSemanticTrace;
using verification::SemanticTraceResult;
using verification::SemanticTraceSink;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

Environment makeEnvironment(int maxCellCount = 96,
        double radius = 1.0,
        bool previewMode = false) {
    Environment environment = Environment::constructDefaultEnvironment(
            maxCellCount, 16, radius);
    environment.mIsPreviewMode = previewMode;
    environment.mRandom.setSeed(kFixtureSeed);
    environment.mFoodSpeed = 0.0;
    environment.mLightSpeed = 0.0;
    environment.mRadiation = 0.0;
    environment.mViscosity = 0.0;
    environment.mDensity = 0.0;
    environment.mGravity = 0.0;
    environment.mTestFoodDecay = 0.0;
    environment.mTestIdleMetabolismScale = 0.0;
    environment.mTestFlagellocyteCostScale = 0.0;
    environment.mTestPhagocyteEatSpeedScale = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.aging = false;
    environment.addRandom = false;
    environment.mobileFood = false;
    environment.dieEdge = false;
    return environment;
}

void initializeGene(Gene& gene, CellType type, int geneIndex) {
    GeneBuilder builder(gene);
    builder.setCellType(type);
    builder.setSplitMass(CellData::MAX_SPLIT_MASS);
    builder.setSplitRatio(0.5f);
    builder.setMassFlowPriority(1.0f);
    builder.setLinkStrength(0.5f);
    const GeneIndex self(static_cast<GeneIndex::Value>(geneIndex));
    builder.setChild1GeneIndex(self);
    builder.setChild2GeneIndex(self);
    builder.setStaysAlive(true);
    builder.setInitialCompatibilityFlag(geneIndex == 0);
    builder.setIntegerValue(
            Gene::MAX_CONNECTION_COUNT_INDEX, CellData::MAX_NEIGHBORS - 1);
    builder.setVirusFromGeneIndex(self);
    builder.setVirusToGeneIndex(self);
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
}

int addCell(SimulationWorld& world,
        CellType type,
        double x,
        double y,
        double radius = kDefaultRadius) {
    CellData& cells = world.mCellData;
    const int index = cells.allocateNewCell();
    require(index >= 0, "semantic fixture exceeded Cell capacity");
    FixedGenome genome;
    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
        initializeGene(genome[geneIndex], type, geneIndex);
    }
    require(cells.replaceGenome(index, genome, GeneIndex(0)),
            "semantic fixture Genome import failed");
    cells.mPositionX[index] = x;
    cells.mPositionY[index] = y;
    cells.mPositionZ[index] = 0.0;
    cells.mRadius[index] = radius;
    cells.refreshRadiusSq(index);
    cells.mMass[index] = CellData::MASS_PER_AREA * radius * radius;
    cells.mNitrates[index] = CellData::MAX_NITRATES;
    cells.mAge[index] = CellData::AGE_OF_CONSENT + 0.25;
    cells.mOriginTag[index] = SimulationWorld::USER_CELL_ORIGIN;
    cells.mTelomeresCount[index] = -1;
    cells.mOldLinkCount[index] = 0;
    cells.mSeldomCounter[index] = 0;
    cells.markCellAsAlive(index);
    return index;
}

void addDirectedContact(CellData& cells, int actor, int target) {
    const double dx = cells.mPositionX[actor] - cells.mPositionX[target];
    const double dy = cells.mPositionY[actor] - cells.mPositionY[target];
    const double distanceSq = dx * dx + dy * dy;
    require(cells.addKnownCellCollisionContactFast(actor, target, dx, dy, distanceSq),
            "semantic fixture could not add directed contact");
}

template <typename Integer>
void appendLittleEndian(std::vector<std::uint8_t>& bytes, Integer value) {
    using Unsigned = std::make_unsigned_t<Integer>;
    const Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t byte = 0; byte < sizeof(Integer); byte++) {
        bytes.push_back(static_cast<std::uint8_t>(bits >> (byte * 8U)));
    }
}

void appendBool(std::vector<std::uint8_t>& bytes, bool value) {
    bytes.push_back(value ? 1U : 0U);
}

void appendDouble(std::vector<std::uint8_t>& bytes, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    appendLittleEndian(bytes, bits);
}

std::vector<std::uint8_t> normalizedWorldState(const SimulationWorld& world) {
    std::vector<std::uint8_t> bytes;
    const std::string persistedStateDigest =
            save_format::release::substrateSemanticDigest(world);
    bytes.reserve(persistedStateDigest.size() + 4096);
    appendLittleEndian(bytes, std::uint32_t{0x41303253}); // "A02S"
    appendLittleEndian(bytes, std::uint16_t{2});
    appendLittleEndian(
            bytes,
            static_cast<std::uint64_t>(persistedStateDigest.size()));
    bytes.insert(
            bytes.end(),
            persistedStateDigest.begin(),
            persistedStateDigest.end());

    const CellData& cells = world.mCellData;
    appendLittleEndian(bytes, static_cast<std::int32_t>(world.getUpdateStage()));
    appendBool(bytes, world.cellContactCacheIsFresh());
    appendLittleEndian(bytes, SimulationRandom::global.getSeed());
    appendLittleEndian(bytes, static_cast<std::int32_t>(cells.mCellCount));
    for (int cell = 0; cell < cells.mCellCount; cell++) {
        appendDouble(bytes, cells.mRadiusSq[cell]);
        appendLittleEndian(bytes, static_cast<std::int32_t>(CellTypes::ordinal(cells.mActiveCellType[cell])));
        appendDouble(bytes, cells.mActiveCellCollisionElasticity[cell]);
        appendLittleEndian(bytes, static_cast<std::int32_t>(cells.mFertilizationTarget[cell]));
        const int begin = cells.cellCollisionContactBegin(cell);
        const int end = cells.cellCollisionContactEnd(cell);
        appendLittleEndian(bytes, static_cast<std::int32_t>(end - begin));
        for (int contact = begin; contact < end; contact++) {
            appendLittleEndian(bytes, static_cast<std::int32_t>(cells.mCellContactBuffer.collidedCellIndex[contact]));
            const auto& geometry = cells.mCellContactBuffer.geometry[contact];
            appendDouble(bytes, geometry.distanceX);
            appendDouble(bytes, geometry.distanceY);
            appendDouble(bytes, geometry.distanceSq);
        }
    }

    const auto& intents = cells.mCellCollisionIntentBuffer;
    appendLittleEndian(bytes, static_cast<std::int32_t>(intents.count));
    for (int intent = 0; intent < intents.count; intent++) {
        appendLittleEndian(bytes, static_cast<std::int32_t>(intents.currentCellIndex[intent]));
        appendLittleEndian(bytes, static_cast<std::int32_t>(intents.collidedCellIndex[intent]));
        appendDouble(bytes, intents.distanceX[intent]);
        appendDouble(bytes, intents.distanceY[intent]);
        appendDouble(bytes, intents.distance[intent]);
        appendDouble(bytes, intents.contactChordLengthSquared[intent]);
    }

    appendLittleEndian(bytes, static_cast<std::int32_t>(cells.mCiliocyteInteractionCount));
    for (int interaction = 0; interaction < cells.mCiliocyteInteractionCount; interaction++) {
        appendLittleEndian(bytes, static_cast<std::int32_t>(cells.mCiliocyteInteractionCellIndex1[interaction]));
        appendLittleEndian(bytes, static_cast<std::int32_t>(cells.mCiliocyteInteractionCellIndex2[interaction]));
    }
    return bytes;
}

FixtureResult finishFixture(const std::string& name,
        SemanticTraceSink& sink,
        const SimulationWorld& world) {
    const std::vector<std::uint8_t> state = normalizedWorldState(world);
    FixtureResult result;
    result.name = name;
    result.trace = sink.finish();
    result.normalizedStateDigest = test_support::stableDigest(state);
    result.normalizedStateBytes = state.size();
    return result;
}

FixtureResult pairGeometryFixture() {
    constexpr const char* name = "pair_geometry";
    SimulationWorld world(false, makeEnvironment(24));
    const int right = addCell(world, CellType::PHAGOCYTE, -0.23, 0.12);
    const int equalXFirst = addCell(world, CellType::PHAGOCYTE, -0.26, 0.12);
    const int equalXSecond = addCell(world, CellType::PHAGOCYTE, -0.26, 0.13);
    const int lowerRow = addCell(world, CellType::PHAGOCYTE, 0.0, -1e-12);
    const int upperRow = addCell(world, CellType::PHAGOCYTE, 0.01, 1e-12);
    const int zeroA = addCell(world, CellType::PHAGOCYTE, 0.25, 0.12);
    const int zeroB = addCell(world, CellType::PHAGOCYTE, 0.25, 0.12);
    constexpr double tangentRadius = 0.015625; // exact binary 1/64
    const int tangentA = addCell(
            world, CellType::PHAGOCYTE, 0.125, -0.25, tangentRadius);
    const int tangentB = addCell(
            world, CellType::PHAGOCYTE, 0.15625, -0.25, tangentRadius);
    (void)right;
    (void)equalXFirst;
    (void)equalXSecond;
    (void)lowerRow;
    (void)upperRow;

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.updateGrids();
        world.processCellOverlaps();
        world.mCellData.handleAllCellToCellCollisionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    require(world.mCellData.cellCollisionContactCount(zeroA) == 0
                    && world.mCellData.cellCollisionContactCount(zeroB) == 0,
            "zero-distance pair must not become a contact");
    require(world.mCellData.cellCollisionContactCount(tangentA) == 0
                    && world.mCellData.cellCollisionContactCount(tangentB) == 0,
            "exact tangency must remain outside the strict overlap contract");
    return finishFixture(name, sink, world);
}

FixtureResult endpointCapacityFixture() {
    constexpr const char* name = "endpoint_capacity";
    SimulationWorld world(false, makeEnvironment(8));
    const int first = addCell(world, CellType::PHAGOCYTE, 0.0, 0.0);
    const int second = addCell(world, CellType::PHAGOCYTE, 0.01, 0.0);
    CellData& cells = world.mCellData;
    for (int index = 0; index < CellData::MAX_COLLISIONS_PER_CELL; index++) {
        require(cells.addKnownCellCollisionContactFast(first, second, -0.01, 0.0, 0.0001),
                "first endpoint saturation setup failed");
    }

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.processKnownCellOverlapPair(
                first, second, -0.01, 0.0, 0.0001, kDefaultRadius, kDefaultRadius);
        require(cells.cellCollisionContactCount(first) == CellData::MAX_COLLISIONS_PER_CELL
                        && cells.cellCollisionContactCount(second) == 1,
                "one-sided endpoint admission must be independent");
        while (cells.cellCollisionContactCount(second) < CellData::MAX_COLLISIONS_PER_CELL) {
            require(cells.addKnownCellCollisionContactFast(second, first, 0.01, 0.0, 0.0001),
                    "second endpoint saturation setup failed");
        }
        world.processKnownCellOverlapPair(
                first, second, -0.01, 0.0, 0.0001, kDefaultRadius, kDefaultRadius);
        require(cells.cellCollisionContactCount(first) == CellData::MAX_COLLISIONS_PER_CELL
                        && cells.cellCollisionContactCount(second) == CellData::MAX_COLLISIONS_PER_CELL,
                "two-sided endpoint overflow must retain both first-32 prefixes");
        cells.handleAllCellToCellCollisionsPartial(SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    return finishFixture(name, sink, world);
}

FixtureResult containmentCleanupFixture() {
    constexpr const char* name = "containment_cleanup";
    SimulationWorld world(false, makeEnvironment(16));
    const int peer = addCell(world, CellType::PHAGOCYTE, -0.030, 0.0, 0.012);
    const int small = addCell(world, CellType::PHAGOCYTE, -0.010, 0.0, 0.012);
    const int large = addCell(world, CellType::PHAGOCYTE, 0.005, 0.0, 0.030);
    const int competingLarge = addCell(world, CellType::PHAGOCYTE, 0.018, 0.0, 0.030);
    (void)competingLarge;

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.updateGrids();
        world.processCellOverlaps();
        world.mCellData.handleAllCellToCellCollisionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    require(world.mCellData.mIsDead[small], "contained Cell must be marked dead");
    for (int offset = 0; offset < world.mCellData.cellCollisionContactCount(peer); offset++) {
        const int contact = world.mCellData.cellCollisionContactBegin(peer) + offset;
        require(world.mCellData.cellCollisionContactTarget(contact) != small,
                "cleanup must remove contacts that point to a dead Cell");
    }
    require(!world.mCellData.mIsDead[large], "larger containment participant must survive");
    return finishFixture(name, sink, world);
}

FixtureResult fertilizationOverwriteFixture() {
    constexpr const char* name = "fertilization_overwrite";
    SimulationWorld world(false, makeEnvironment(16));
    addCell(world, CellType::GAMETE, -0.0005, 0.0);
    addCell(world, CellType::GAMETE, 0.000, 0.0);
    addCell(world, CellType::GAMETE, 0.0005, 0.0);
    world.mEnvironment.mRandom.setSeed(kFixtureSeed);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.updateGrids();
        world.processCellOverlaps();
        const bool applied = world.mCellData.resolveFertilizationIntents(world.mEnvironment);
        require(applied, "mutual final gamete targets must resolve exactly once");
        world.invalidateCellContactCache();
    }
    int deadCount = 0;
    for (int cell = 0; cell < world.mCellData.mCellCount; cell++) {
        deadCount += world.mCellData.mIsDead[cell] ? 1 : 0;
    }
    require(deadCount == 1, "three-gamete overwrite fixture must fertilize one mutual pair");
    return finishFixture(name, sink, world);
}

FixtureResult fertilizationBeforeDeathFixture() {
    constexpr const char* name = "fertilization_before_death";
    SimulationWorld world(false, makeEnvironment(8));
    addCell(world, CellType::GAMETE, 0.000, 0.0, 0.014);
    addCell(world, CellType::GAMETE, 0.001, 0.0, 0.030);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.updateGrids();
        world.processCellOverlaps();
        require(world.mCellData.resolveFertilizationIntents(world.mEnvironment),
                "fertilization must take precedence over containment death");
        world.invalidateCellContactCache();
    }
    require(world.mCellData.mIsDead[0] != world.mCellData.mIsDead[1],
            "fertilization must consume exactly one unequal gamete");
    return finishFixture(name, sink, world);
}

FixtureResult fertilizationIncomingDeadTargetFixture() {
    constexpr const char* name = "fertilization_incoming_dead_target";
    SimulationWorld world(true, makeEnvironment(8, 1.0, true));
    const int first = addCell(world, CellType::GAMETE, 0.0, 0.0);
    const int second = addCell(world, CellType::GAMETE, 0.001, 0.0);
    SimulationRandom expectedRandom;
    expectedRandom.setSeed(kFixtureSeed);
    const bool firstIsSperm = expectedRandom.nextInt(2) == 0;
    const int sperm = firstIsSperm ? first : second;
    const int egg = firstIsSperm ? second : first;
    const double spermX = firstIsSperm ? 0.0 : 0.001;
    const int ciliocyte = addCell(
            world,
            CellType::CILIOCYTE,
            spermX + (firstIsSperm ? -0.03 : 0.03),
            0.0);
    world.mCellData.editActiveGene(
            ciliocyte,
            [](GeneBuilder& ciliaGene) {
                ciliaGene.setParametricFormulaType(
                        Gene::MYOCYTE_BENDING_VALUE_INDEX, 0);
                ciliaGene.setParametricVariableB(
                        Gene::MYOCYTE_BENDING_VALUE_INDEX, 0.0f);
                ciliaGene.setParametricFormulaType(
                        Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);
                ciliaGene.setParametricVariableB(
                        Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0.0f);
            });
    world.mEnvironment.mRandom.setSeed(kFixtureSeed);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.update1Partial(false, 1.0);
    }

    require(world.mCellData.mIsDead[sperm]
                    && !world.mCellData.mIsDead[egg],
            "three-Cell fertilization fixture must select the predicted sperm");
    for (int actor = 0; actor < world.mCellData.mCellCount; actor++) {
        const int begin = world.mCellData.cellCollisionContactBegin(actor);
        const int end = world.mCellData.cellCollisionContactEnd(actor);
        for (int contact = begin; contact < end; contact++) {
            require(world.mCellData.cellCollisionContactTarget(contact) != sperm,
                    "fertilization cleanup must remove incoming dead-target contacts");
        }
    }
    for (int intent = 0;
            intent < world.mCellData.mCellCollisionIntentBuffer.count;
            intent++) {
        require(world.mCellData.mCellCollisionIntentBuffer.collidedCellIndex[intent]
                        != sperm,
                "dead sperm must not reach ordinary or typed collision apply");
    }
    for (int interaction = 0;
            interaction < world.mCellData.mCiliocyteInteractionCount;
            interaction++) {
        require(world.mCellData.mCiliocyteInteractionCellIndex2[interaction]
                        != sperm,
                "dead sperm must not reach the Ciliocyte queue");
    }
    for (int cell = 0; cell < world.mCellData.mCellCount; cell++) {
        require(std::isfinite(world.mCellData.mVelocityX[cell])
                        && std::isfinite(world.mCellData.mVelocityY[cell])
                        && std::isfinite(world.mCellData.mAngularVelocity[cell]),
                "fertilization/Ciliocyte state must remain finite");
    }
    return finishFixture(name, sink, world);
}

FixtureResult virocyteVisibilityFixture() {
    constexpr const char* name = "typed_virocyte_visibility";
    SimulationWorld world(false, makeEnvironment(12));
    const int virus = addCell(world, CellType::VIROCYTE, 0.00, 0.0);
    const int convertedActor = addCell(world, CellType::PHAGOCYTE, 0.02, 0.0);
    const int victim = addCell(world, CellType::PHAGOCYTE, 0.04, 0.0);
    CellData& cells = world.mCellData;
    cells.editGene(virus, GeneIndex(1), [](GeneBuilder& gene) {
        GeneData initialized;
        initializeGene(initialized, CellType::DEVOROCYTE, 1);
        gene.copyFrom(GeneView(initialized));
    });
    cells.editActiveGene(virus, [](GeneBuilder& virusGene) {
        virusGene.setVirusFromGeneIndex(GeneIndex(1));
        virusGene.setVirusToGeneIndex(GeneIndex(0));
    });
    const double victimMassBefore = cells.mMass[victim];
    cells.clearCellCollisionContacts();
    addDirectedContact(cells, virus, convertedActor);
    addDirectedContact(cells, convertedActor, victim);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        cells.handleAllCellToCellCollisionsPartial(1.0, world.mEnvironment);
    }
    require(cells.mActiveCellType[convertedActor] == CellType::DEVOROCYTE,
            "virocyte must change the next intent's active handler");
    require(cells.mMass[victim] < victimMassBefore,
            "converted actor's later intent must observe the virocyte change");
    const GenomeData convertedGenome = cells.materializeGenome(convertedActor);
    const std::size_t initialGeneCount = static_cast<std::size_t>(std::count_if(
            convertedGenome.begin(),
            convertedGenome.end(),
            [](const GeneData& gene) {
                return GeneView(gene).initialCompatibilityFlag();
            }));
    require(initialGeneCount == 1
                    && GeneView(convertedGenome[0]).initialCompatibilityFlag(),
            "viral transfer must retain exactly one Genome-owned initial Gene");
    return finishFixture(name, sink, world);
}

FixtureResult virocyteSafetyRngFixture() {
    constexpr const char* name = "virocyte_rng_before_safety";
    SimulationWorld world(false, makeEnvironment(12));
    const int virus = addCell(world, CellType::VIROCYTE, 0.00, 0.0);
    const int victim = addCell(world, CellType::PHAGOCYTE, 0.02, 0.0);
    const int protector = addCell(world, CellType::KERATINOCYTE, 0.04, 0.0);
    require(world.mCellData.addLink(
                    victim,
                    protector,
                    CellLinkCreationReason::UNSPECIFIED,
                    0.0,
                    0.0,
                    0.5f,
                    0.0f)
                    >= 0,
            "virocyte safety fixture protector link failed");
    world.mCellData.clearCellCollisionContacts();
    addDirectedContact(world.mCellData, virus, victim);
    const CellType victimTypeBefore =
            world.mCellData.getActiveGeneView(victim).cellType();
    const std::int64_t seedBefore = world.mEnvironment.mRandom.getSeed();

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.mCellData.handleAllCellToCellCollisionsPartial(1.0, world.mEnvironment);
    }
    require(world.mEnvironment.mRandom.getSeed() != seedBefore,
            "protected virocyte target must still consume RNG before safety");
    require(world.mCellData.getActiveGeneView(victim).cellType()
                    == victimTypeBefore,
            "keratinocyte protection must prevent infection after RNG");
    return finishFixture(name, sink, world);
}

FixtureResult glueocyteFixture() {
    constexpr const char* name = "typed_glueocyte";
    SimulationWorld world(false, makeEnvironment(8));
    const int glue = addCell(world, CellType::GLUEOCYTE, 0.0, 0.0);
    const int victim = addCell(world, CellType::PHAGOCYTE, 0.02, 0.0);
    world.mCellData.clearCellCollisionContacts();
    addDirectedContact(world.mCellData, glue, victim);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.mCellData.handleAllCellToCellCollisionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    require(world.mCellData.mTopology.isLinked(glue, victim),
            "glueocyte typed intent must create its topology edge");
    return finishFixture(name, sink, world);
}

FixtureResult devorocyteFixture() {
    constexpr const char* name = "typed_devorocyte";
    SimulationWorld world(false, makeEnvironment(8));
    const int devorocyte = addCell(world, CellType::DEVOROCYTE, 0.0, 0.0);
    const int victim = addCell(world, CellType::PHAGOCYTE, 0.02, 0.0);
    world.mCellData.mToxin[victim] = 0.4f;
    const double actorMassBefore = world.mCellData.mMass[devorocyte];
    world.mCellData.clearCellCollisionContacts();
    addDirectedContact(world.mCellData, devorocyte, victim);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.mCellData.handleAllCellToCellCollisionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    require(world.mCellData.mMass[devorocyte] > actorMassBefore,
            "devorocyte typed intent must transfer mass");
    return finishFixture(name, sink, world);
}

FixtureResult secrocyteFixture() {
    constexpr const char* name = "typed_secrocyte";
    SimulationWorld world(false, makeEnvironment(8));
    const int secrocyte = addCell(world, CellType::SECROCYTE, 0.0, 0.0);
    const int victim = addCell(world, CellType::PHAGOCYTE, 0.02, 0.0);
    world.mCellData.editActiveGene(
            secrocyte,
            [](GeneBuilder& gene) {
                gene.setIntegerValue(
                        Gene::SECRETE_TYPE_INTEGER_INDEX,
                        Gene::SECRETE_TYPE_CYANIDE);
            });
    world.mCellData.mNitrates[secrocyte] = CellData::MAX_NITRATES;
    world.mCellData.clearCellCollisionContacts();
    addDirectedContact(world.mCellData, secrocyte, victim);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.mCellData.handleAllCellToCellCollisionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    require(world.mCellData.mToxin[victim] > 0.0f,
            "secrocyte typed intent must apply its configured secretion");
    return finishFixture(name, sink, world);
}

FixtureResult ciliocyteCapacityFixture() {
    constexpr const char* name = "typed_ciliocyte_capacity";
    constexpr int neighborCount = CellData::MAX_COLLISIONS_PER_CELL;
    SimulationWorld world(false, makeEnvironment(neighborCount + 8, 1.0));
    const int ciliocyte = addCell(world, CellType::CILIOCYTE, 0.979, 0.0, 0.022);
    CellData& cells = world.mCellData;
    cells.editActiveGene(ciliocyte, [](GeneBuilder& ciliaGene) {
        ciliaGene.setParametricFormulaType(
                Gene::MYOCYTE_BENDING_VALUE_INDEX, 0);
        ciliaGene.setParametricVariableB(
                Gene::MYOCYTE_BENDING_VALUE_INDEX, 0.0f);
        ciliaGene.setParametricFormulaType(
                Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);
        ciliaGene.setParametricVariableB(
                Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0.0f);
    });
    for (int index = 0; index < neighborCount; index++) {
        const double angle = 2.0 * Environment::PI * index / neighborCount;
        const int neighbor = addCell(
                world,
                CellType::PHAGOCYTE,
                0.979 + 0.015 * std::cos(angle),
                0.015 * std::sin(angle),
                0.008);
        addDirectedContact(cells, ciliocyte, neighbor);
    }
    require(cells.mCiliocyteInteractionCellIndex1.size()
                    == static_cast<std::size_t>(cells.MAX_CELL_COUNT)
                            * CellData::MAX_CILIOCYTE_INTERACTIONS_PER_CELL,
            "ciliocyte storage must use the proved 33-per-Cell capacity");

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        cells.collectAllCollisionInteractionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
        require(cells.mCiliocyteInteractionCount
                        == CellData::MAX_CILIOCYTE_INTERACTIONS_PER_CELL,
                "32 Cell contacts plus one boundary contact must all be retained");
        require(cells.mCiliocyteInteractionCellIndex2[cells.mCiliocyteInteractionCount - 1]
                        == CellData::BOUNDARY_CONNECTION_INDEX,
                "ciliocyte boundary interaction must follow Cell interactions");
        cells.applyCollectedCiliocyteInteractionsPartial(world.mEnvironment);
    }
    return finishFixture(name, sink, world);
}

FixtureResult topologyCapacityFixture() {
    constexpr const char* name = "topology_capacity_swap_remove";
    SimulationWorld world(false, makeEnvironment(CellData::MAX_NEIGHBORS + 8));
    const int center = addCell(world, CellType::GLUEOCYTE, 0.0, 0.0);
    std::vector<int> neighbors;
    for (int index = 0; index < CellData::MAX_NEIGHBORS + 1; index++) {
        neighbors.push_back(addCell(
                world, CellType::PHAGOCYTE, 0.1 + index * 0.01, 0.0));
    }

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        for (int index = 0; index < CellData::MAX_NEIGHBORS; index++) {
            require(world.mCellData.mTopology.addCellLink(
                            center,
                            neighbors[index],
                            CellLinkCreationReason::UNSPECIFIED,
                            0.01 * index,
                            -0.01 * index,
                            0.5f, 0.0f)
                            >= 0,
                    "topology capacity setup add failed");
        }
        require(world.mCellData.mTopology.addCellLink(
                        center,
                        neighbors.back(),
                        CellLinkCreationReason::UNSPECIFIED,
                        0.0,
                        0.0,
                        0.5f,
                        0.0f)
                        < 0,
                "topology must reject the 21st adjacency");
        const int movedA = world.mCellData.mTopology.edgeCellA(
                world.mCellData.mTopology.edgeCount() - 1);
        const int movedB = world.mCellData.mTopology.edgeCellB(
                world.mCellData.mTopology.edgeCount() - 1);
        world.mCellData.mTopology.removeCellLink(3);
        require(world.mCellData.mTopology.edgeCellA(3) == movedA
                        && world.mCellData.mTopology.edgeCellB(3) == movedB,
                "topology removal must preserve swap-remove order");
        require(world.mCellData.mTopology.addBoundaryLink(
                        center, 0.0, 1.0, 0.0, 0.5f, 0.0f)
                        >= 0,
                "freed topology slot must admit a boundary link");
        world.mCellData.mTopology.removeBoundaryLink(0);
        require(world.mCellData.mTopology.validate(),
                "topology must remain valid after capacity and swap-remove fixture");
    }
    return finishFixture(name, sink, world);
}

FixtureResult renderCacheFixture() {
    constexpr const char* name = "render_contact_cache";
    SimulationWorld world(false, makeEnvironment(8));
    addCell(world, CellType::PHAGOCYTE, 0.0, 0.0);
    addCell(world, CellType::PHAGOCYTE, 0.02, 0.0);

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.updateGrids();
        require(!world.cellContactCacheIsFresh(),
                "grid rebuild must invalidate render contact cache");
        world.processCellOverlaps();
        require(world.cellContactCacheIsFresh(),
                "full overlap pass must make render contact cache fresh");
        world.updateGrids();
        require(!world.cellContactCacheIsFresh(),
                "later geometry rebuild must invalidate the fresh cache");
        world.processCellOverlaps();
        require(world.cellContactCacheIsFresh(),
                "recomputed contacts must restore cache freshness");
    }
    return finishFixture(name, sink, world);
}

FixtureResult splitCompactionFixture() {
    constexpr const char* name = "split_copy_remove_compaction";
    SimulationWorld world(false, makeEnvironment(12));
    const int parent = addCell(world, CellType::PHAGOCYTE, -0.1, 0.0, 0.03);
    const int removed = addCell(world, CellType::PHAGOCYTE, 0.0, 0.0, 0.02);
    const int survivor = addCell(world, CellType::GLUEOCYTE, 0.1, 0.0, 0.02);
    world.mCellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(CellData::MIN_SPLIT_MASS);
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(0));
        parentGene.setChild2GeneIndex(GeneIndex(0));
        parentGene.setMakesLink(true);
    });
    world.mCellData.mMass[parent] = CellData::MAX_MASS;
    world.mCellData.mNitrates[parent] = CellData::MAX_NITRATES;
    world.mCellData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    world.mCellData.mOldLinkCount[parent] = 0;
    world.mCellData.mOriginTag[survivor] = 77;

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.mCellData.handleCellSplit(world);
        require(world.mCellData.mCellCount == 4,
                "split fixture must allocate one child");
        world.mCellData.markCellAsDead(removed);
        world.removeDeadCells();
        require(world.mCellData.mCellCount == 3,
                "dead Cell removal must compact the active range");
    }
    require(world.mCellData.mTopology.validate(),
            "split/removal compaction must preserve topology validity");
    return finishFixture(name, sink, world);
}

FixtureResult metabolismNanDeathOrderFixture() {
    constexpr const char* name = "metabolism_nan_death_order";
    SimulationWorld world(false, makeEnvironment(8));
    const int nanFirst = addCell(
            world, CellType::NEUROCYTE, -0.03, 0.0);
    const int nitrateOnly = addCell(
            world, CellType::LIPOCYTE, 0.0, 0.0);
    const int nanAndNitrate = addCell(
            world, CellType::PHAGOCYTE, 0.03, 0.0);

    CellData& cells = world.mCellData;
    for (const int cellIndex : {nanFirst, nanAndNitrate}) {
        cells.mMass[cellIndex] = std::numeric_limits<double>::max();
        require(
                cells.editActiveGene(cellIndex, [](GeneBuilder& gene) {
                    gene.setSplitMass(CellData::MIN_SPLIT_MASS);
                    gene.setMassFlowPriority(
                            CellData::MAX_MASS_FLOW_PRIORITY);
                }),
                "NaN death-order fixture must configure a valid maximum "
                "mass-flow priority");
    }
    cells.mNitrates[nitrateOnly] = CellData::MIN_NITRATES * 0.5;
    cells.mNitrates[nanAndNitrate] = CellData::MIN_NITRATES * 0.5;

    cells.editActiveGene(nanFirst, [](GeneBuilder& neurocyteGene) {
        for (int outputIndex = 0;
                outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT;
                ++outputIndex) {
            neurocyteGene.setIntegerValue(
                    Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + outputIndex,
                    outputIndex);
            const int valueIndex =
                    Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + outputIndex;
            neurocyteGene.setParametricFormulaType(valueIndex, 0);
            neurocyteGene.setParametricVariableB(
                    valueIndex,
                    0.125f * static_cast<float>(outputIndex + 1));
        }
    });
    cells.mSignalSubstance[nitrateOnly][0] = 0.875f;
    cells.mSignalSubstance[nanAndNitrate][0] = 0.125f;

    require(cells.addLink(
                    nanFirst,
                    nanAndNitrate,
                    CellLinkCreationReason::UNSPECIFIED,
                    0.0,
                    0.0,
                    0.0f,
                    0.06f)
                    == 0,
            "NaN death-order fixture must add the overflowing mass-flow link");
    require(cells.addLink(
                    nitrateOnly,
                    nanAndNitrate,
                    CellLinkCreationReason::UNSPECIFIED,
                    0.0,
                    0.0,
                    0.0f,
                    0.03f)
                    == 0,
            "NaN death-order fixture must add the Lipocyte signal link");

    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.update2Partial(1.0);
    }

    require(cells.mIsDead[nanFirst],
            "lower-index NaN Cell must die in the apply phase; pending mass="
                    + std::to_string(cells.mNewMass[nanFirst])
                    + ", applied mass=" + std::to_string(cells.mMass[nanFirst]));
    require(cells.mIsDead[nitrateOnly],
            "higher-index nitrate-starved Cell must die in finalization");
    require(cells.mIsDead[nanAndNitrate],
            "Cell with both causes must remain dead after the repeated event");
    require(cells.mNewMass[nanFirst] == 0.0
                    && cells.mNewMass[nanAndNitrate] == 0.0,
            "NaN apply must clear pending mass after ordered deaths");
    for (const int cellIndex : {nanFirst, nanAndNitrate}) {
        const GeneView gene = cells.getActiveGeneView(cellIndex);
        require(gene.splitMass() == CellData::MIN_SPLIT_MASS
                        && gene.massFlowPriority()
                                == CellData::MAX_MASS_FLOW_PRIORITY,
                "NaN fixture must remain representable by canonical pooled Genes");
    }
    FixtureResult result = finishFixture(name, sink, world);
    const std::vector<std::string> expectedDeaths{
            "CellDeath(1:0x00000001,7:0x00)",
            "CellDeath(1:0x00000002,7:0x00)",
            "CellDeath(1:0x00000000,7:0x00)",
            "CellDeath(1:0x00000002,7:0x01)",
    };
    require(result.trace.apply.capturedEventDescriptions == expectedDeaths,
            "metabolism deaths must retain nitrate-first and ascending NaN-tail order");
    return result;
}

std::unique_ptr<SimulationWorld> makeSaveLoadTieWorld() {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(16));
    addCell(*world, CellType::PHAGOCYTE, -0.02, 0.0);
    addCell(*world, CellType::PHAGOCYTE, -0.02, 0.01);
    addCell(*world, CellType::PHAGOCYTE, 0.01, 0.0);
    world->mCellData.replaceActiveCellCount(world->mCellData.mCellCount);
    return world;
}

FixtureResult runSaveLoadTieWorld(SimulationWorld& world) {
    constexpr const char* name = "save_load_equal_x_tie";
    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        world.updateGrids();
        world.processCellOverlaps();
        world.mCellData.handleAllCellToCellCollisionsPartial(
                SimulationWorld::DELTA_TIME, world.mEnvironment);
    }
    return finishFixture(name, sink, world);
}

FixtureResult saveLoadTieFixture() {
    std::unique_ptr<SimulationWorld> world = makeSaveLoadTieWorld();
    return runSaveLoadTieWorld(*world);
}

using FixtureFunction = FixtureResult (*)();

const std::array<std::pair<const char*, FixtureFunction>, 16> kFixtures{{
        {"pair_geometry", pairGeometryFixture},
        {"endpoint_capacity", endpointCapacityFixture},
        {"containment_cleanup", containmentCleanupFixture},
        {"fertilization_overwrite", fertilizationOverwriteFixture},
        {"fertilization_before_death", fertilizationBeforeDeathFixture},
        {"fertilization_incoming_dead_target", fertilizationIncomingDeadTargetFixture},
        {"typed_virocyte_visibility", virocyteVisibilityFixture},
        {"virocyte_rng_before_safety", virocyteSafetyRngFixture},
        {"typed_glueocyte", glueocyteFixture},
        {"typed_devorocyte", devorocyteFixture},
        {"typed_secrocyte", secrocyteFixture},
        {"typed_ciliocyte_capacity", ciliocyteCapacityFixture},
        {"topology_capacity_swap_remove", topologyCapacityFixture},
        {"render_contact_cache", renderCacheFixture},
        {"split_copy_remove_compaction", splitCompactionFixture},
        {"metabolism_nan_death_order", metabolismNanDeathOrderFixture},
}};

} // namespace

std::vector<std::string> semanticFixtureNames() {
    std::vector<std::string> names;
    names.reserve(kFixtures.size() + 1);
    for (const auto& fixture : kFixtures) {
        names.emplace_back(fixture.first);
    }
    names.emplace_back("save_load_equal_x_tie");
    return names;
}

FixtureResult runSemanticFixture(const std::string& name) {
    SimulationRandom::global.setSeed(kFixtureSeed ^ 0x5a5a5a5aLL);
    if (name == "save_load_equal_x_tie") {
        return saveLoadTieFixture();
    }
    for (const auto& fixture : kFixtures) {
        if (name == fixture.first) {
            return fixture.second();
        }
    }
    fail("unknown semantic fixture: " + name);
}

void verifySaveLoadTieOrderContract() {
    SimulationRandom::global.setSeed(kFixtureSeed ^ 0x5a5a5a5aLL);
    std::unique_ptr<SimulationWorld> source = makeSaveLoadTieWorld();
    const CellRuntimeId sourceIdentity = source->mCellData.runtimeId(0);
    const std::vector<std::uint8_t> bytes =
            save_format::release::encodeReleaseSubstrate(*source).bytes;
    std::unique_ptr<SimulationWorld> loaded =
            save_format::release::decodeReleaseSubstrate(bytes, false);
    require(sourceIdentity != loaded->mCellData.runtimeId(0),
            "save/load fixture must use different non-serialized runtime identities");

    SimulationRandom::global.setSeed(kFixtureSeed ^ 0x5a5a5a5aLL);
    const FixtureResult sourceResult = runSaveLoadTieWorld(*source);
    SimulationRandom::global.setSeed(kFixtureSeed ^ 0x5a5a5a5aLL);
    const FixtureResult loadedResult = runSaveLoadTieWorld(*loaded);

    auto sameChannel = [](const verification::TraceChannelResult& left,
                               const verification::TraceChannelResult& right) {
        return left.eventCount == right.eventCount
                && left.digest == right.digest
                && left.capturedEventDigests == right.capturedEventDigests
                && left.diagnosticWindowTruncated == right.diagnosticWindowTruncated;
    };
    require(sameChannel(sourceResult.trace.sweep, loadedResult.trace.sweep)
                    && sameChannel(sourceResult.trace.endpoint, loadedResult.trace.endpoint)
                    && sameChannel(sourceResult.trace.apply, loadedResult.trace.apply),
            "save/load changed an order trace despite equivalent serialized state");
    require(sourceResult.normalizedStateDigest == loadedResult.normalizedStateDigest,
            "save/load tie fixture final normalized states differ");
}

} // namespace cellularity::semantic_tests
