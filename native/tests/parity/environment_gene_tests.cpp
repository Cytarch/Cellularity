#include "core/BesselLookup.h"
#include "core/CellData.h"
#include "core/CellSweepBroadphase.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/GenomeIdentity.h"
#include "core/RenderContactChord.h"
#include "core/SimulationRandom.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cellularity::tests;

namespace {

using cellularity::GeneBuilder;
using cellularity::GeneIndex;
using cellularity::GeneView;

void testGenomeIdentityTypesAreDistinctAndCompact() {
    static_assert(!std::is_convertible_v<cellularity::GeneIndex, cellularity::GeneId>);
    static_assert(!std::is_convertible_v<cellularity::GeneId, cellularity::GeneIndex>);
    static_assert(!std::is_convertible_v<cellularity::GeneId, cellularity::GenomeId>);
    static_assert(!std::is_constructible_v<cellularity::GeneIndex, cellularity::GeneId>);
    static_assert(!std::is_constructible_v<cellularity::GeneId, cellularity::GenomeId>);
    static_assert(sizeof(cellularity::GeneIndex) == sizeof(std::uint16_t));
    static_assert(sizeof(cellularity::GeneId) == sizeof(std::uint32_t));
    static_assert(sizeof(cellularity::GenomeId) == sizeof(std::uint32_t));

    assertTrue(cellularity::SELF_GENE_INDEX.value() == 0xffffU,
            "Gene self-reference sentinel");
    assertTrue(cellularity::INVALID_GENE_INDEX.value() == 0xfffeU,
            "Invalid GeneIndex sentinel");
    assertTrue(cellularity::GeneId{} == cellularity::INVALID_GENE_ID,
            "Default GeneId is invalid");
    assertTrue(cellularity::GenomeId{} == cellularity::INVALID_GENOME_ID,
            "Default GenomeId is invalid");
}

void testGeneIndexBoundaryConversionsAreBounded() {
    using namespace cellularity;
    assertTrue(isSupportedGeneCount(1), "One-gene genome is supported");
    assertTrue(isSupportedGeneCount(256), "256-gene genome is supported");
    assertTrue(!isSupportedGeneCount(0), "Empty genome is unsupported");
    assertTrue(!isSupportedGeneCount(257), "Genome above defensive limit is unsupported");

    const auto first = geneIndexFromBoundary(0, 40);
    const auto last = geneIndexFromBoundary(39, 40);
    assertTrue(first.has_value() && first->value() == 0,
            "Boundary conversion accepts first local index");
    assertTrue(last.has_value() && last->value() == 39,
            "Boundary conversion accepts last local index");
    assertTrue(!geneIndexFromBoundary(-1, 40).has_value(),
            "Boundary conversion rejects negative index");
    assertTrue(!geneIndexFromBoundary(40, 40).has_value(),
            "Boundary conversion rejects index equal to gene count");
    assertTrue(!geneIndexFromBoundary(0, 0).has_value(),
            "Boundary conversion rejects invalid gene count");

    const auto self = geneReferenceFromBoundary(SELF_GENE_INDEX.value(), 40);
    assertTrue(self.has_value() && *self == SELF_GENE_INDEX,
            "Reference conversion accepts explicit self sentinel");
    assertTrue(!geneReferenceFromBoundary(INVALID_GENE_INDEX.value(), 40).has_value(),
            "Reference conversion rejects invalid sentinel");
    assertTrue(geneIndexToBoundary(*last, 40).value() == 39,
            "Concrete index converts back to boundary value");
    assertTrue(!geneIndexToBoundary(SELF_GENE_INDEX, 40).has_value(),
            "Concrete-index writer rejects self sentinel");
    assertTrue(geneReferenceToBoundary(SELF_GENE_INDEX, 40).value() == 0xffffU,
            "Reference writer preserves self sentinel");
}

void testGeneReferenceResolutionUsesOwnerAndBounds() {
    using namespace cellularity;
    const GeneIndex owner(7);
    assertTrue(resolveGeneReference(SELF_GENE_INDEX, owner, 40) == owner,
            "Self-reference resolves to owner index");
    assertTrue(resolveGeneReference(GeneIndex(3), owner, 40) == GeneIndex(3),
            "Explicit reference preserves its local index");
    assertTrue(resolveGeneReference(GeneIndex(40), owner, 40) == INVALID_GENE_INDEX,
            "Out-of-range reference resolves invalid");
    assertTrue(resolveGeneReference(GeneIndex(3), GeneIndex(40), 40)
                    == INVALID_GENE_INDEX,
            "Invalid owner makes reference resolution fail");
    assertTrue(resolveGeneReference(INVALID_GENE_INDEX, owner, 40)
                    == INVALID_GENE_INDEX,
            "Invalid reference remains invalid");
}

void testGeneReferenceRemappingPreservesSelfAndMapsExplicitIndices() {
    using namespace cellularity;
    const std::array<GeneIndex, 4> deletionRemap{
            GeneIndex(0), GeneIndex(1), GeneIndex(1), GeneIndex(2)
    };
    const GeneIndexRemapView remap{
            deletionRemap.data(), deletionRemap.size(), 3
    };
    assertTrue(isValidGeneIndexRemap(remap), "Deletion remap is valid");
    assertTrue(remapGeneIndex(GeneIndex(3), remap) == GeneIndex(2),
            "Index above deletion shifts down");
    assertTrue(remapGeneReference(GeneIndex(1), remap) == GeneIndex(1),
            "Deleted target redirects to explicit replacement");
    assertTrue(remapGeneReference(SELF_GENE_INDEX, remap) == SELF_GENE_INDEX,
            "Self sentinel remains self-relative during remap");
    assertTrue(remapGeneReference(INVALID_GENE_INDEX, remap) == INVALID_GENE_INDEX,
            "Invalid reference remains invalid during remap");

    const std::array<GeneIndex, 2> invalidTargets{GeneIndex(0), GeneIndex(2)};
    const GeneIndexRemapView invalidRemap{
            invalidTargets.data(), invalidTargets.size(), 2
    };
    assertTrue(!isValidGeneIndexRemap(invalidRemap),
            "Remap rejects target outside new genome");
    assertTrue(remapGeneReference(SELF_GENE_INDEX, invalidRemap)
                    == INVALID_GENE_INDEX,
            "Invalid remap cannot preserve a reference");
}

void testSimulationRandomKnownSequence() {
    SimulationRandom random;
    assertTrue(random.nextLong() == 46525339305LL,
            "SimulationRandom first nextLong differs from the reference sequence");
    assertTrue(random.nextLong() == 1508956563863969977LL,
            "SimulationRandom second nextLong differs from the reference sequence");

    random.setSeed(1989);
    int64_t l1 = random.nextLong();
    random.setSeed(1989);
    int64_t l2 = random.nextLong();
    assertTrue(l1 == l2, "SimulationRandom setSeed should reproduce its sequence");
    assertTrue(random.nextInt(1) == 0, "SimulationRandom nextInt(1) should return zero");
}

void testFoodData() {
    Environment environment = Environment::constructDefaultEnvironment(10, 3, 1.0);
    FoodData foodData(3);
    foodData.addFood(0.1, 0.2, 0.5, 0.25f, environment);
    foodData.addFood(2.0, 0.0, 1.0, 0.0f, environment);
    assertTrue(foodData.mFoodCount == 1, "FoodData should reject food outside dish");
    assertNear(0.5, foodData.getFoodMass(), 1e-12, "FoodData mass sum");

    foodData.copy(0, 1);
    assertNear(foodData.mPositionX[0], foodData.mPositionX[1], 0.0, "FoodData copy x");
    assertNear(foodData.mLipidLayer[0], foodData.mLipidLayer[1], 0.0, "FoodData copy lipid");
}

void testEnvironment() {
    Environment environment = Environment::constructDefaultEnvironment(123, 456, 2.5);
    assertTrue(environment.maxCellCount == 123, "Environment default maxCellCount");
    assertTrue(environment.maxFoodCount == 456, "Environment default maxFoodCount");
    assertNear(2.5, environment.mRadius, 0.0, "Environment default radius");
    assertTrue(environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)], "Environment default phagocyte gene pool");
    assertTrue(environment.mGenePool[CellTypes::ordinal(CellType::FLAGELLOCYTE)], "Environment default flagellocyte gene pool");
    assertTrue(!environment.mGenePool[CellTypes::ordinal(CellType::KERATINOCYTE)], "Environment default keratinocyte gene pool");

    environment.mLightAngle = 0.0;
    environment.mLightSpeed = 2.0;
    environment.updateLight(0.25);
    assertNear(0.5, environment.mLightAngle, 1e-12, "Environment updateLight angle");
    assertNear(std::cos(0.5), environment.mLightAngleCos, 1e-12, "Environment updateLight cos");
    assertNear(std::sin(0.5), environment.mLightAngleSin, 1e-12, "Environment updateLight sin");

    environment.mDensity = 0.25;
    environment.mDensityGradient = 0.5;
    environment.syncDensityFieldFromLegacyGradient();
    assertNear(0.25 + 0.5 * 0.4 / 2.5, environment.getDensity(0.0, 0.4), 1e-12, "Environment vertical directional density");
    environment.mDensityFieldOffset = 0.1;
    assertNear(0.25 + 0.5 * (0.4 / 2.5 - 0.1), environment.getDensity(0.0, 0.4), 1e-12, "Environment vertical directional density offset");

    environment.mDensityFieldType = Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL;
    environment.mDensityFieldBase = 1.0;
    environment.mDensityFieldStrength = 2.0;
    environment.mDensityFieldAngle = 0.0;
    environment.mDensityFieldOffset = 0.1;
    assertNear(1.0 + 2.0 * (0.5 / 2.5 - 0.1), environment.getDensity(0.5, 0.0), 1e-12, "Environment directional density");

    environment.mDensityFieldType = Environment::DENSITY_FIELD_RADIAL;
    environment.mDensityFieldBase = 0.2;
    environment.mDensityFieldStrength = -0.4;
    environment.mDensityFieldOffset = 0.25;
    assertNear(0.2 - 0.4 * (0.5 / 2.5 - 0.25), environment.getDensity(0.3, 0.4), 1e-12, "Environment radial density");

    environment.mDensityFieldSoftness = 0.5;
    double radialCoordinate = 0.5 / 2.5 - 0.25;
    double softenedCoordinate = radialCoordinate / (1.0 + 0.5 * std::abs(radialCoordinate) * 2.0);
    assertNear(0.2 - 0.4 * softenedCoordinate, environment.getDensity(0.3, 0.4), 1e-12, "Environment radial density softness");

    environment.mSalinity = 0.3f;
    environment.syncSalinityFieldFromLegacyValue();
    assertNear(0.3, environment.getSalinity(0.0, 0.0), 1e-7, "Environment constant salinity field");
    environment.mSalinityFieldType = Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL;
    environment.mSalinityFieldBase = 0.5;
    environment.mSalinityFieldStrength = 0.75;
    environment.mSalinityFieldAngle = 0.0;
    assertNear(0.5 + 0.75 * 0.5 / 2.5, environment.getSalinity(0.5, 0.0), 1e-12, "Environment directional salinity field");
    environment.mSalinityFieldBase = 0.9;
    environment.mSalinityFieldStrength = 1.0;
    assertNear(1.0, environment.getSalinity(2.5, 0.0), 1e-12, "Environment salinity field clamps high");
    environment.mFoodLipidLayer = 0.2f;
    environment.syncFoodLipidLayerFieldFromLegacyValue();
    assertNear(0.2, environment.getFoodLipidLayer(0.0, 0.0), 1e-7, "Environment constant food lipid field");
    environment.mFoodLipidLayerFieldType = Environment::DENSITY_FIELD_RADIAL;
    environment.mFoodLipidLayerFieldBase = 0.1;
    environment.mFoodLipidLayerFieldStrength = -0.5;
    assertNear(0.0, environment.getFoodLipidLayer(2.5, 0.0), 1e-12, "Environment food lipid field clamps low");
}

void testEnvironmentMutationWeightsUseLegacyFlatDefaults() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    double pointWeight = 0.0;
    for (int i = 0; i < Environment::MUTATION_POINT_WEIGHT_COUNT; i++) {
        pointWeight += environment.mMutationWeights[i];
    }
    double structuralWeight = 0.0;
    for (int i = 0; i < Environment::MUTATION_STRUCTURAL_WEIGHT_COUNT; i++) {
        structuralWeight += environment.mMutationWeights[Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET + i];
    }

    assertTrue(Environment::MUTATION_WEIGHT_COUNT == 100, "Environment mutation weight count");
    assertNear(pointWeight, structuralWeight, 1e-9, "Environment legacy flat mutation weights keep previous effective distribution");
    assertTrue(environment.mMutationWeights[Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX] > 0.0
                    && environment.mMutationWeights[Environment::MUTATION_DELETE_GENE_WEIGHT_INDEX] > 0.0,
            "New environments enable dynamic genome length mutations");
}

void testDeadSplashData() {
    DeadSplashData deadSplashData(2);
    deadSplashData.replaceActiveDeadSplashCount(2);
    deadSplashData.mRadius[0] = static_cast<float>(CellData::MAX_CELL_RADIUS);
    deadSplashData.mRadius[1] = static_cast<float>(CellData::MAX_CELL_RADIUS);
    deadSplashData.mAge[0] = 0.99f;
    deadSplashData.mAge[1] = 0.0f;
    deadSplashData.mPositionX[1] = 0.25f;

    deadSplashData.updateDeadSplashes(0.04);
    assertTrue(deadSplashData.mDeadSplashCount == 1, "DeadSplashData should remove expired splash");
    assertNear(0.25, deadSplashData.mPositionX[0], 0.0, "DeadSplashData should compact by copying last element");
}

void testCellType() {
    assertTrue(CellTypes::length == 18, "CellTypes length");
    for (int ordinal = 0; ordinal < CellTypes::length; ordinal++) {
        assertTrue(CellTypes::ordinal(CellTypes::values[ordinal]) == ordinal,
                "CellTypes stable native ordinal");
    }
    CellTypeInfo flagellocyte = CellTypes::info(CellType::FLAGELLOCYTE);
    assertTrue(flagellocyte.baseShader == "phagocyte", "CellType flagellocyte base shader");
    assertTrue(flagellocyte.outlineShader == "flagellocyte_outline", "CellType flagellocyte outline shader");
    assertNear(static_cast<float>(CellData::MAX_CELL_RADIUS) * 4.0f, flagellocyte.outlineRadius, 0.0, "CellType flagellocyte outline radius");
    assertTrue(CellTypes::fromOrdinal(17) == CellType::CILIOCYTE, "CellTypes from ordinal");

    assertTrue(CellDataConstants::BOUNDARY_CONNECTION_INDEX == -1,
            "Common contract boundary connection index");
    assertNear(400.0, CellDataConstants::MASS_PER_AREA, 0.0,
            "Common contract mass per area");
    assertNear(0.03, CellDataConstants::MAX_CELL_RADIUS, 0.0,
            "Common contract max cell radius");
    assertNear(0.072, CellDataConstants::MIN_MASS, 1e-15,
            "Common contract min mass");
    assertNear(0.36, CellDataConstants::MAX_MASS, 1e-15,
            "Common contract max mass");
    assertNear(2.16, CellDataConstants::MAX_LIPO_MASS, 1e-15,
            "Common contract max lipocyte mass");
    assertNear(0.013416407864998739, CellDataConstants::MIN_CELL_RADIUS, 1e-15,
            "Common contract min cell radius");
    assertNear(240.0, CellDataConstants::MAX_AGE, 0.0,
            "Common contract max age");
    assertNear(1.8, CellDataConstants::MAX_LIPIDS, 1e-6,
            "Common contract max lipids");
    assertNear(1.0, CellDataConstants::MAX_SWIM_FORCE, 0.0,
            "Common contract max swim force");
    assertNear(20.0, CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT, 0.0,
            "Common contract max signal output");
    assertNear(0.05, CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT_INVERSE, 1e-7,
            "Common contract inverse max signal output");
    assertNear(5.0, CellDataConstants::SIGNAL_SUBSTANCE_FLOW, 0.0,
            "Common contract signal flow");
    assertNear(5.0, CellDataConstants::SIGNAL_SUBSTANCE_DECAY, 0.0,
            "Common contract signal decay");
    assertNear(0.01, CellDataConstants::SIGNAL_SUBSTANCE_INITIAL, 1e-7,
            "Common contract initial signal");
    assertTrue(CellDataConstants::SIGNAL_SUBSTANCE_COUNT == 4,
            "Common contract signal count");
    assertTrue(CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT == 4,
            "Common contract auxiliary signal count");
    assertTrue(CellDataConstants::NEUROCYTE_OUTPUT_COUNT == 4,
            "Common contract neurocyte output count");
    assertTrue(CellDataConstants::MAX_NEIGHBORS == 20,
            "Common contract max neighbors");
    assertTrue(CellDataConstants::MAX_GENES_COUNT == 40,
            "Common contract max genes");
    assertNear(8.0, CellDataConstants::VALUE_VARIABLE_MAX, 0.0,
            "Common contract variable limit");
    assertNear(10.0, CellDataConstants::MAX_MASS_FLOW_PRIORITY, 0.0,
            "Common contract max mass flow priority");
    assertNear(0.1, CellDataConstants::MIN_MASS_FLOW_PRIORITY, 1e-7,
            "Common contract min mass flow priority");
    assertNear(0.1, CellDataConstants::MIN_RATIO, 1e-7,
            "Common contract min split ratio");
    assertNear(0.3672, CellDataConstants::MAX_SPLIT_MASS, 1e-7,
            "Common contract max split mass");
    assertNear(0.0648, CellDataConstants::MIN_SPLIT_MASS, 1e-7,
            "Common contract min split mass");
    assertTrue(CellDataConstants::MUTATE_PERIOD == 15,
            "Common contract mutation period");
    assertNear(35.0, CellDataConstants::MAX_LINK_STIFFNESS, 0.0,
            "Common contract max link stiffness");

    assertTrue(Environment::DENSITY_FIELD_CONSTANT == 0,
            "Common contract constant scalar field");
    assertTrue(Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL == 1,
            "Common contract linear scalar field");
    assertTrue(Environment::DENSITY_FIELD_RADIAL == 2,
            "Common contract radial scalar field");
    assertTrue(Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL == 0,
            "Common contract linear vector field");
    assertTrue(Environment::VECTOR_FIELD_RADIAL_INWARD == 1,
            "Common contract inward vector field");
    assertTrue(Environment::VECTOR_FIELD_RADIAL_OUTWARD == 2,
            "Common contract outward vector field");
    assertNear(2.0, Environment::MAX_LIGHT, 0.0,
            "Common contract max light");
    assertNear(1.0, Environment::MAX_LIGHT_RANGE, 0.0,
            "Common contract max light range");
    assertNear(1.0, Environment::MAX_LIGHT_SPEED, 0.0,
            "Common contract max light speed");
    assertNear(0.09, Environment::MAX_RADIATION, 1e-15,
            "Common contract max radiation");
    assertNear(150.0, Environment::VISCOSITY_MAX, 1e-12,
            "Common contract max viscosity");
    assertNear(1.152, Environment::MAX_FOOD_SIZE, 1e-15,
            "Common contract max food size");
    assertNear(0.0216, Environment::MIN_FOOD_SIZE, 1e-15,
            "Common contract min food size");
    assertNear(100.0, Environment::MAX_FOOD_LUMPINESS, 0.0,
            "Common contract max food lumpiness");
    assertNear(0.01, Environment::MIN_FOOD_LUMP_SIZE, 0.0,
            "Common contract min food lump size");
    assertNear(0.2, Environment::MAX_LUMP_SIZE, 0.0,
            "Common contract max lump size");
    assertNear(15.0, Environment::MAX_FOOD_SPEED, 1e-12,
            "Common contract max food speed");
    assertNear(8.0, Environment::MAX_FRICTION, 0.0,
            "Common contract max friction");
}

void testBesselLookupMatchesReferenceTable() {
    assertNear(4.72124, BesselLookup::k0(0.0), 0.0, "BesselLookup K0 first table value");
    assertNear(4.02846, BesselLookup::k0(0.014), 0.0, "BesselLookup K0 rounded index");
    assertNear(0.0036911, BesselLookup::k0(100.0), 0.0, "BesselLookup K0 clamps high");
    assertNear(99.9739, BesselLookup::k1(-1.0), 0.0, "BesselLookup K1 clamps low");
    assertNear(49.9547, BesselLookup::k1(0.014), 0.0, "BesselLookup K1 rounded index");
    assertNear(0.00404461, BesselLookup::k1(100.0), 0.0, "BesselLookup K1 clamps high");
}

void testGeneFixAndCopy() {
    Gene gene;
    GeneBuilder builder(gene);
    builder.setSplitRatio(0.0f);
    builder.setMassFlowPriority(CellData::MAX_MASS_FLOW_PRIORITY + 1.0f);
    builder.setIntegerValue(
            Gene::SMELL_OUT_INTEGER_INDEX,
            Gene::INTEGER_MAX[Gene::SMELL_OUT_INTEGER_INDEX] + 10);
    builder.setIntegerValue(Gene::VIRUS_FROM_INTEGER_INDEX, 255);
    builder.setIntegerValue(Gene::VIRUS_TO_INTEGER_INDEX, 255);
    builder.setParametricFormulaType(Gene::SWIM_FORCE_VALUE_INDEX, 9);
    builder.setParametricSignalChannel(Gene::SWIM_FORCE_VALUE_INDEX, 99);
    builder.setFloatValue(
            Gene::FloatType::CYTOSKELETON,
            builder.view().floatMaximum(Gene::FloatType::CYTOSKELETON) + 10.0f);

    builder.fix();

    const GeneView fixed = builder.view();
    assertNear(CellData::MIN_RATIO, fixed.splitRatio(), 0.0, "Gene fix split ratio");
    assertNear(CellData::MAX_MASS_FLOW_PRIORITY, fixed.massFlowPriority(), 0.0, "Gene fix mass flow priority");
    assertTrue(fixed.integerValue(Gene::SMELL_OUT_INTEGER_INDEX) == Gene::INTEGER_MAX[Gene::SMELL_OUT_INTEGER_INDEX] - 1, "Gene fix integer max");
    assertTrue(fixed.virusFromGeneIndex() == GeneIndex(39)
                    && fixed.virusToGeneIndex() == GeneIndex(39),
            "Gene fix defaults to the legacy 40-Gene reference domain");
    assertTrue(fixed.parametricFormulaType(Gene::SWIM_FORCE_VALUE_INDEX) == 3, "Gene fix formula type");
    assertTrue(fixed.parametricSignalChannel(Gene::SWIM_FORCE_VALUE_INDEX) == CellData::SIGNAL_SUBSTANCE_COUNT + CellData::AUX_SIGNAL_SUBSTANCE_COUNT - 1, "Gene fix signal channel");
    assertNear(fixed.floatMaximum(Gene::FloatType::CYTOSKELETON), fixed.floatValue(Gene::FloatType::CYTOSKELETON), 0.0, "Gene FloatValue fix");
    assertTrue(fixed.hasValidValues(), "Gene valid values after fix");

    builder.setVirusFromGeneIndex(GeneIndex(255));
    builder.setVirusToGeneIndex(GeneIndex(255));
    builder.fix(150);
    assertTrue(builder.view().virusFromGeneIndex() == GeneIndex(149)
                    && builder.view().virusToGeneIndex() == GeneIndex(149),
            "Gene fix uses the actual dynamic Genome reference domain");

    builder.setCellType(CellType::CILIOCYTE);
    builder.setColor(0, 0.25f);
    builder.setLinkStrength(3.5f);
    Gene copy(gene);
    const GeneView copyView(copy);
    assertTrue(copyView.cellType() == CellType::CILIOCYTE, "Gene copy cell type");
    assertNear(0.25f, copyView.color(0), 0.0, "Gene copy color");
    assertNear(3.5f, copyView.linkStrength(), 0.0, "Gene copy link strength");
}

void testGeneMutateCanReachAllMutableFieldFamilies() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mGenePool.fill(true);
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(912345);

    Gene gene;
    std::array<bool, 3> colorTouched{};
    std::array<bool, 7> continuousTouched{};
    std::array<bool, 2> childIndexTouched{};
    std::array<bool, 6> booleanTouched{};
    std::array<std::array<bool, 5>, Gene::MAX_PARAMETRIC_VALUES_COUNT> parametricTouched{};
    std::array<bool, Gene::MAX_INTEGERS_COUNT> integerTouched{};
    std::array<bool, Gene::MAX_FLOATS_COUNT> floatTouched{};
    bool cellTypeTouched = false;

    for (int i = 0; i < 30000; i++) {
        GeneBuilder(gene).mutate(environment);
        const GeneView view(gene);
        for (int colorIndex = 0; colorIndex < 3; colorIndex++) {
            colorTouched[colorIndex] = colorTouched[colorIndex] || view.color(colorIndex) != 0.0f;
        }
        continuousTouched[0] = continuousTouched[0] || view.splitAngle() != 0.0f;
        continuousTouched[1] = continuousTouched[1] || view.child1Angle() != 0.0f;
        continuousTouched[2] = continuousTouched[2] || view.child2Angle() != 0.0f;
        continuousTouched[3] = continuousTouched[3] || view.massFlowPriority() != 0.0f;
        continuousTouched[4] = continuousTouched[4] || view.splitMass() != 0.0f;
        continuousTouched[5] = continuousTouched[5] || view.splitRatio() != 0.0f;
        continuousTouched[6] = continuousTouched[6] || view.linkStrength() != 0.0f;
        childIndexTouched[0] = childIndexTouched[0] || view.child1GeneIndex() != GeneIndex(0);
        childIndexTouched[1] = childIndexTouched[1] || view.child2GeneIndex() != GeneIndex(0);
        booleanTouched[0] = booleanTouched[0] || view.makesLink();
        booleanTouched[1] = booleanTouched[1] || view.child1KeepsLinks();
        booleanTouched[2] = booleanTouched[2] || view.child2KeepsLinks();
        booleanTouched[3] = booleanTouched[3] || view.staysAlive();
        booleanTouched[4] = booleanTouched[4] || view.child1Mirrored();
        booleanTouched[5] = booleanTouched[5] || view.child2Mirrored();
        cellTypeTouched = cellTypeTouched || view.cellType() != CellType::PHAGOCYTE;
        for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
            const cellularity::GeneParametricValueView value = view.parametricValue(valueIndex);
            parametricTouched[valueIndex][0] = parametricTouched[valueIndex][0] || value.variableA() != 0.0f;
            parametricTouched[valueIndex][1] = parametricTouched[valueIndex][1] || value.variableB() != 0.0f;
            parametricTouched[valueIndex][2] = parametricTouched[valueIndex][2] || value.variableC() != 0.0f;
            parametricTouched[valueIndex][3] = parametricTouched[valueIndex][3] || value.signalChannel() != 0;
            parametricTouched[valueIndex][4] = parametricTouched[valueIndex][4] || value.formulaType() != 0;
        }
        for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
            integerTouched[integerIndex] = integerTouched[integerIndex] || view.integerValue(integerIndex) != 0;
        }
        constexpr std::array<Gene::FloatType, 7> floatTypes{
                Gene::FloatType::SMELL_AMPLITUDE,
                Gene::FloatType::SMELL_RED,
                Gene::FloatType::SMELL_GREEN,
                Gene::FloatType::SMELL_BLUE,
                Gene::FloatType::SMELL_THRESHOLD,
                Gene::FloatType::ADHESIN_LENGTH,
                Gene::FloatType::CYTOSKELETON
        };
        for (std::size_t floatIndex = 0; floatIndex < floatTypes.size(); floatIndex++) {
            floatTouched[floatIndex] = floatTouched[floatIndex]
                    || view.floatValue(floatTypes[floatIndex]) != 0.0f;
        }
    }

    for (bool touched : colorTouched) {
        assertTrue(touched, "Gene mutate reaches each color channel");
    }
    for (bool touched : continuousTouched) {
        assertTrue(touched, "Gene mutate reaches each continuous scalar");
    }
    for (bool touched : childIndexTouched) {
        assertTrue(touched, "Gene mutate reaches each child gene index");
    }
    for (bool touched : booleanTouched) {
        assertTrue(touched, "Gene mutate reaches each mutable boolean");
    }
    assertTrue(cellTypeTouched, "Gene mutate reaches cell type");
    for (const auto& valueTouched : parametricTouched) {
        for (bool touched : valueTouched) {
            assertTrue(touched, "Gene mutate reaches each parametric subfield");
        }
    }
    for (bool touched : integerTouched) {
        assertTrue(touched, "Gene mutate reaches each integer field");
    }
    for (bool touched : floatTouched) {
        assertTrue(touched, "Gene mutate reaches each FloatValue field");
    }
}

void testGeneMutateAnglesWrapAcrossZero() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(77331);

    Gene gene;
    bool wrappedAcrossZero = false;
    const float halfTurn = static_cast<float>(std::acos(-1.0));
    for (int i = 0; i < 5000 && !wrappedAcrossZero; i++) {
        GeneBuilder builder(gene);
        builder.setSplitAngle(0.0f);
        builder.setChild1Angle(0.0f);
        builder.setChild2Angle(0.0f);

        builder.mutate(environment);

        const GeneView view = builder.view();
        wrappedAcrossZero = view.splitAngle() > halfTurn
                || view.child1Angle() > halfTurn
                || view.child2Angle() > halfTurn;
    }

    assertTrue(wrappedAcrossZero, "Gene mutate angles wrap negative deltas across zero");
}

void testGeneMutateColorUsesHsvHueWrap() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(88421);

    Gene gene;
    bool hueWrappedColor = false;
    for (int i = 0; i < 10000 && !hueWrappedColor; i++) {
        GeneBuilder builder(gene);
        builder.setColor(0, 1.0f);
        builder.setColor(1, 0.0f);
        builder.setColor(2, 0.0f);

        builder.mutate(environment);

        const GeneView view = builder.view();
        hueWrappedColor = view.color(0) > 0.9f
                && view.color(1) < 0.1f
                && view.color(2) > 0.2f;
    }

    assertTrue(hueWrappedColor, "Gene mutate color hue wraps through HSV");
}

void testGeneMutateSmellColorUsesHsvHueWrap() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(94127);

    Gene gene;
    bool hueWrappedColor = false;
    for (int i = 0; i < 10000 && !hueWrappedColor; i++) {
        GeneBuilder builder(gene);
        builder.setFloatValue(Gene::FloatType::SMELL_RED, 1.0f);
        builder.setFloatValue(Gene::FloatType::SMELL_GREEN, 0.0f);
        builder.setFloatValue(Gene::FloatType::SMELL_BLUE, 0.0f);

        builder.mutate(environment);

        const GeneView view = builder.view();
        hueWrappedColor = view.floatValue(Gene::FloatType::SMELL_RED) > 0.9f
                && view.floatValue(Gene::FloatType::SMELL_GREEN) < 0.1f
                && view.floatValue(Gene::FloatType::SMELL_BLUE) > 0.2f;
    }

    assertTrue(hueWrappedColor, "Gene mutate smell color hue wraps through HSV");
}

void testGeneMutateFloatUsesTrueReflectionAtBoundaries() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mTestMutationCoef = 1.5;
    environment.mRandom.setSeed(51671);

    Gene gene;
    int splitRatioMutations = 0;
    for (int i = 0; i < 50000 && splitRatioMutations < 200; i++) {
        GeneBuilder builder(gene);
        builder.setSplitRatio(0.5f);
        builder.mutate(environment);
        const float splitRatio = builder.view().splitRatio();
        if (splitRatio == 0.5f) {
            continue;
        }
        splitRatioMutations++;

        assertTrue(splitRatio >= CellData::MIN_RATIO, "Gene mutate float stays above min");
        assertTrue(splitRatio <= 1.0f - CellData::MIN_RATIO, "Gene mutate float stays below max");
        assertTrue(splitRatio != CellData::MIN_RATIO, "Gene mutate float does not clamp to min");
        assertTrue(splitRatio != 1.0f - CellData::MIN_RATIO, "Gene mutate float does not clamp to max");
    }
    assertTrue(splitRatioMutations == 200, "Gene mutate float reflection test samples split ratio mutations");
}

void testGeneMutateSplitMassTreatsNoSplitAsSeparateState() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(61781);

    Gene gene;
    GeneBuilder builder(gene);
    builder.setCellType(CellType::PHAGOCYTE);
    bool exitedNoSplit = false;
    for (int i = 0; i < 50000 && !exitedNoSplit; i++) {
        builder.setSplitMass(CellData::MAX_SPLIT_MASS);
        builder.mutate(environment);
        exitedNoSplit = builder.view().splitMass() != CellData::MAX_SPLIT_MASS;
    }
    assertTrue(exitedNoSplit, "Gene mutate split mass samples no-split state");
    assertTrue(builder.view().splitMass() < CellData::MAX_MASS, "Gene mutate split mass exits no-split below max mass");

    bool enteredNoSplit = false;
    float highSplittable = std::nextafter(static_cast<float>(CellData::MAX_MASS), CellData::MIN_SPLIT_MASS);
    for (int i = 0; i < 50000 && !enteredNoSplit; i++) {
        builder.setSplitMass(highSplittable);
        builder.mutate(environment);
        enteredNoSplit = builder.view().splitMass() == CellData::MAX_SPLIT_MASS;
    }
    assertTrue(enteredNoSplit, "Gene mutate split mass can enter no-split state");
}

void testGeneMutateTelomeraseOnlyForGamete() {
    Environment phagocyteEnvironment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    phagocyteEnvironment.mGenePool.fill(false);
    phagocyteEnvironment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    phagocyteEnvironment.mRandom.setSeed(72011);

    Gene phagocyteGene;
    GeneBuilder phagocyteBuilder(phagocyteGene);
    phagocyteBuilder.setCellType(CellType::PHAGOCYTE);
    phagocyteBuilder.setIntegerValue(Gene::TELOMERASE_LENGTH_INDEX, 0);
    for (int i = 0; i < 50000; i++) {
        phagocyteBuilder.mutate(phagocyteEnvironment);
    }
    assertTrue(phagocyteBuilder.view().cellType() == CellType::PHAGOCYTE, "Gene mutate keeps phagocyte under phagocyte-only pool");
    assertTrue(phagocyteBuilder.view().integerValue(Gene::TELOMERASE_LENGTH_INDEX) == 0, "Gene mutate skips telomerase for non-gamete");

    Environment gameteEnvironment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    gameteEnvironment.mGenePool.fill(false);
    gameteEnvironment.mGenePool[CellTypes::ordinal(CellType::GAMETE)] = true;
    gameteEnvironment.mRandom.setSeed(72012);

    Gene gameteGene;
    GeneBuilder gameteBuilder(gameteGene);
    gameteBuilder.setCellType(CellType::GAMETE);
    gameteBuilder.setIntegerValue(Gene::TELOMERASE_LENGTH_INDEX, 0);
    bool telomeraseMutated = false;
    for (int i = 0; i < 50000 && !telomeraseMutated; i++) {
        gameteBuilder.mutate(gameteEnvironment);
        telomeraseMutated = gameteBuilder.view().integerValue(Gene::TELOMERASE_LENGTH_INDEX) != 0;
    }
    assertTrue(telomeraseMutated, "Gene mutate allows telomerase for gamete");
}

void testGeneMutateGameteCompatibilityTargetsGameteGenesWhenGenomeContextIsProvided() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::GAMETE)] = true;
    environment.mRandom.setSeed(91357);

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (Gene& gene : genome) {
        GeneBuilder(gene).setCellType(CellType::PHAGOCYTE);
    }
    GeneBuilder genome0Builder(genome[0]);
    genome0Builder.setCellType(CellType::GAMETE);
    GeneBuilder(genome[3]).setCellType(CellType::GAMETE);
    genome0Builder.setVirusToGeneIndex(GeneIndex(0));

    bool selfTargetSeen = true;
    bool otherGameteTargetSeen = false;
    for (int i = 0; i < 50000 && (!selfTargetSeen || !otherGameteTargetSeen); i++) {
        genome0Builder.mutate(environment, cellularity::GenomeView(genome));

        int compatibleGeneIndex = genome0Builder.view().virusToGeneIndex().value();
        assertTrue(compatibleGeneIndex == 0 || compatibleGeneIndex == 3,
                "Gene mutate gamete compatibility target stays on gamete gene");
        selfTargetSeen = selfTargetSeen || compatibleGeneIndex == 0;
        otherGameteTargetSeen = otherGameteTargetSeen || compatibleGeneIndex == 3;
    }

    assertTrue(selfTargetSeen, "Gene mutate gamete compatibility can target self gamete gene");
    assertTrue(otherGameteTargetSeen, "Gene mutate gamete compatibility can target another gamete gene");
}

void testGeneMutationUsesContextualGenomeReferenceDomain() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mMutationWeights.fill(0.0);
    environment.mMutationWeights[Environment::MUTATION_INTEGER_WEIGHT_OFFSET
            + Gene::VIRUS_FROM_INTEGER_INDEX] = 1.0;
    environment.mRandom.setSeed(0x47454e45);

    Gene legacyGene;
    GeneBuilder legacyBuilder(legacyGene);
    legacyBuilder.setVirusFromGeneIndex(GeneIndex(39));
    for (int mutation = 0; mutation < 512; ++mutation) {
        legacyBuilder.mutate(environment);
        assertTrue(
                legacyBuilder.view().virusFromGeneIndex().value()
                        < CellData::MAX_GENES_COUNT,
                "Contextless Gene mutation preserves the legacy 40-Gene domain");
    }

    cellularity::GenomeData dynamicGenome(150);
    GeneBuilder dynamicBuilder(dynamicGenome[0]);
    dynamicBuilder.setVirusFromGeneIndex(GeneIndex(39));
    bool reachedExtendedDomain = false;
    for (int mutation = 0; mutation < 4096; ++mutation) {
        const bool changed = dynamicBuilder.mutatePointByWeightIndex(
                environment,
                Environment::MUTATION_INTEGER_WEIGHT_OFFSET
                        + Gene::VIRUS_FROM_INTEGER_INDEX,
                cellularity::GenomeView(
                        dynamicGenome.data(), dynamicGenome.size()));
        assertTrue(changed, "Contextual virus-reference mutation is available");
        const int index = dynamicBuilder.view().virusFromGeneIndex().value();
        assertTrue(index >= 0 && index < 150,
                "Contextual Gene mutation stays inside the dynamic domain");
        reachedExtendedDomain = reachedExtendedDomain || index >= 40;
    }
    assertTrue(reachedExtendedDomain,
            "A 150-Gene context can mutate beyond the legacy domain");
}

void testGeneMutateReceiverSignalInputPrefersProducedSignalsWithGenomeContext() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    environment.mRandom.setSeed(27731);

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    GeneBuilder receiverBuilder(genome[0]);
    receiverBuilder.setCellType(CellType::PHAGOCYTE);
    GeneBuilder producerBuilder(genome[1]);
    producerBuilder.setCellType(CellType::NEUROCYTE);
    for (int outputIndex = 0; outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT; outputIndex++) {
        producerBuilder.setIntegerValue(
                Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + outputIndex, 2);
    }

    int producedSignalSelections = 0;
    int exploratorySelections = 0;
    for (int i = 0; i < 200000 && (producedSignalSelections + exploratorySelections) < 400; i++) {
        receiverBuilder.setParametricSignalChannel(0, 0);
        receiverBuilder.mutate(environment, cellularity::GenomeView(genome));

        int channelIndex = receiverBuilder.view().parametricSignalChannel(0);
        if (channelIndex == 0) {
            continue;
        }
        if (channelIndex == 2) {
            producedSignalSelections++;
        } else {
            exploratorySelections++;
        }
    }

    assertTrue(producedSignalSelections > exploratorySelections * 2,
            "Gene mutate receiver signal input prefers produced signals");
    assertTrue(exploratorySelections > 0,
            "Gene mutate receiver signal input keeps exploratory channel choices");
}

void testGeneMutateOnlyTweakMutationsPreservesDiscreteFields() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.IsOnlyTweakMutations = true;
    environment.mGenePool.fill(true);
    environment.mRandom.setSeed(60617);

    Gene gene;
    GeneBuilder builder(gene);
    builder.setCellType(CellType::GAMETE);
    builder.setChild1GeneIndex(GeneIndex(7));
    builder.setChild2GeneIndex(GeneIndex(11));
    builder.setMakesLink(true);
    builder.setChild1KeepsLinks(true);
    builder.setChild2KeepsLinks(false);
    builder.setStaysAlive(true);
    builder.setChild1Mirrored(true);
    builder.setChild2Mirrored(false);
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        builder.setIntegerValue(
                integerIndex,
                Gene::INTEGER_MAX[integerIndex] > 1
                        ? Gene::INTEGER_MAX[integerIndex] / 2
                        : 0);
    }
    for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
        builder.setParametricSignalChannel(
                valueIndex,
                valueIndex % (CellData::SIGNAL_SUBSTANCE_COUNT + CellData::AUX_SIGNAL_SUBSTANCE_COUNT));
        builder.setParametricFormulaType(valueIndex, valueIndex % 4);
    }

    const GeneView initial = builder.view();
    GeneIndex child1GeneIndex = initial.child1GeneIndex();
    GeneIndex child2GeneIndex = initial.child2GeneIndex();
    bool isMakeLink = initial.makesLink();
    bool child1KeepLinks = initial.child1KeepsLinks();
    bool child2KeepLinks = initial.child2KeepsLinks();
    bool stayAlive = initial.staysAlive();
    bool child1Mirror = initial.child1Mirrored();
    bool child2Mirror = initial.child2Mirrored();
    CellType cellType = initial.cellType();
    std::array<int, Gene::MAX_INTEGERS_COUNT> integers{};
    std::array<int16_t, Gene::MAX_PARAMETRIC_VALUES_COUNT> signalInputChannels{};
    std::array<int16_t, Gene::MAX_PARAMETRIC_VALUES_COUNT> formulaTypes{};
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        integers[integerIndex] = initial.integerValue(integerIndex);
    }
    for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
        signalInputChannels[valueIndex] = static_cast<int16_t>(initial.parametricSignalChannel(valueIndex));
        formulaTypes[valueIndex] = static_cast<int16_t>(initial.parametricFormulaType(valueIndex));
    }

    for (int i = 0; i < 50000; i++) {
        builder.mutate(environment);
    }

    const GeneView mutated = builder.view();
    assertTrue(mutated.child1GeneIndex() == child1GeneIndex, "Gene tweak mutate preserves child1 gene index");
    assertTrue(mutated.child2GeneIndex() == child2GeneIndex, "Gene tweak mutate preserves child2 gene index");
    assertTrue(mutated.makesLink() == isMakeLink, "Gene tweak mutate preserves make link");
    assertTrue(mutated.child1KeepsLinks() == child1KeepLinks, "Gene tweak mutate preserves child1 keep links");
    assertTrue(mutated.child2KeepsLinks() == child2KeepLinks, "Gene tweak mutate preserves child2 keep links");
    assertTrue(mutated.staysAlive() == stayAlive, "Gene tweak mutate preserves stay alive");
    assertTrue(mutated.child1Mirrored() == child1Mirror, "Gene tweak mutate preserves child1 mirror");
    assertTrue(mutated.child2Mirrored() == child2Mirror, "Gene tweak mutate preserves child2 mirror");
    assertTrue(mutated.cellType() == cellType, "Gene tweak mutate preserves cell type");
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        assertTrue(mutated.integerValue(integerIndex) == integers[integerIndex],
                "Gene tweak mutate preserves integers");
    }
    for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
        assertTrue(mutated.parametricSignalChannel(valueIndex) == signalInputChannels[valueIndex],
                "Gene tweak mutate preserves signal input channel");
        assertTrue(mutated.parametricFormulaType(valueIndex) == formulaTypes[valueIndex],
                "Gene tweak mutate preserves formula type");
    }

    bool allowedFieldChanged = mutated.splitAngle() != 0.0f
            || mutated.child1Angle() != 0.0f
            || mutated.child2Angle() != 0.0f
            || mutated.massFlowPriority() != 0.0f
            || mutated.splitMass() != 0.0f
            || mutated.splitRatio() != 0.0f
            || mutated.linkStrength() != 0.0f;
    for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
        allowedFieldChanged = allowedFieldChanged
                || mutated.parametricVariableA(valueIndex) != 0.0f
                || mutated.parametricVariableB(valueIndex) != 0.0f
                || mutated.parametricVariableC(valueIndex) != 0.0f;
    }
    assertTrue(allowedFieldChanged, "Gene tweak mutate still changes continuous fields");
}

void testGeneMutateIntegersMoveByOne() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    environment.mRandom.setSeed(84219);

    Gene gene;
    GeneBuilder builder(gene);
    builder.setCellType(CellType::PHAGOCYTE);
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        const bool isGenomeReference =
                integerIndex == Gene::VIRUS_FROM_INTEGER_INDEX
                || integerIndex == Gene::VIRUS_TO_INTEGER_INDEX;
        const int upperBound = isGenomeReference
                ? CellData::MAX_GENES_COUNT
                : Gene::INTEGER_MAX[integerIndex];
        builder.setIntegerValue(integerIndex, upperBound / 2);
    }

    int integerMutations = 0;
    for (int i = 0; i < 50000 && integerMutations < 200; i++) {
        std::array<int, Gene::MAX_INTEGERS_COUNT> before{};
        for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
            before[integerIndex] = builder.view().integerValue(integerIndex);
        }
        builder.mutate(environment);
        const GeneView mutated = builder.view();

        for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
            if (integerIndex == Gene::TELOMERASE_LENGTH_INDEX) {
                continue;
            }
            if (mutated.integerValue(integerIndex) == before[integerIndex]) {
                continue;
            }

            integerMutations++;
            int delta = mutated.integerValue(integerIndex) - before[integerIndex];
            assertTrue(delta == -1 || delta == 1, "Gene mutate integer moves by one");
        }
        assertTrue(mutated.integerValue(Gene::TELOMERASE_LENGTH_INDEX) == before[Gene::TELOMERASE_LENGTH_INDEX],
                "Gene mutate integer test skips non-gamete telomerase");
        assertTrue(mutated.virusFromGeneIndex().value() < CellData::MAX_GENES_COUNT
                        && mutated.virusToGeneIndex().value()
                                < CellData::MAX_GENES_COUNT,
                "Contextless integer mutation keeps legacy Genome references bounded");
    }
    assertTrue(integerMutations == 200, "Gene mutate integer step test samples integer mutations");
}

} // namespace

int main() {
    testGenomeIdentityTypesAreDistinctAndCompact();
    testGeneIndexBoundaryConversionsAreBounded();
    testGeneReferenceResolutionUsesOwnerAndBounds();
    testGeneReferenceRemappingPreservesSelfAndMapsExplicitIndices();
    testSimulationRandomKnownSequence();
    testFoodData();
    testEnvironment();
    testEnvironmentMutationWeightsUseLegacyFlatDefaults();
    testDeadSplashData();
    testCellType();
    testBesselLookupMatchesReferenceTable();
    testGeneFixAndCopy();
    testGeneMutateCanReachAllMutableFieldFamilies();
    testGeneMutateAnglesWrapAcrossZero();
    testGeneMutateColorUsesHsvHueWrap();
    testGeneMutateSmellColorUsesHsvHueWrap();
    testGeneMutateFloatUsesTrueReflectionAtBoundaries();
    testGeneMutateSplitMassTreatsNoSplitAsSeparateState();
    testGeneMutateTelomeraseOnlyForGamete();
    testGeneMutateGameteCompatibilityTargetsGameteGenesWhenGenomeContextIsProvided();
    testGeneMutationUsesContextualGenomeReferenceDomain();
    testGeneMutateReceiverSignalInputPrefersProducedSignalsWithGenomeContext();
    testGeneMutateOnlyTweakMutationsPreservesDiscreteFields();
    testGeneMutateIntegersMoveByOne();
    std::cout << "native environment/gene parity tests passed\n";
    return 0;
}
