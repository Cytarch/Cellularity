#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "core/GeneAccess.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity;
using namespace cellularity::tests;

namespace {

std::uint32_t floatBits(float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(value));
    return bits;
}

void testCompactGeneDataLayoutAndDescriptors() {
    assertTrue(sizeof(GeneData) == 272, "Compact GeneData ABI size");
    assertTrue(sizeof(GeneData) * CellDataConstants::MAX_GENES_COUNT == 10880,
            "Fixed compatibility Genome uses compact GeneData storage");
    assertTrue(sizeof(GeneData) * 4 <= 396 * 3,
            "Compact GeneData is at least 25 percent smaller than legacy Gene");
    assertNear(-3.0f * CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT,
            GeneData::floatMinimum(GeneData::FloatType::SMELL_AMPLITUDE),
            0.0,
            "Static smell-amplitude minimum");
    assertNear(2.0,
            GeneData::floatMaximum(GeneData::FloatType::CYTOSKELETON),
            0.0,
            "Static cytoskeleton maximum");

    GeneData source;
    GeneBuilder sourceBuilder(source);
    for (int index = 0; index < GeneData::MAX_INTEGERS_COUNT; ++index) {
        sourceBuilder.setIntegerValue(index, GeneData::INTEGER_MAX[index] - 1);
    }
    sourceBuilder.setChild1GeneIndex(SELF_GENE_INDEX);
    sourceBuilder.setChild2GeneIndex(GeneIndex(255));

    GeneData copy;
    GeneBuilder(copy).copyFrom(sourceBuilder.view());
    const GeneView copyView(copy);
    for (int index = 0; index < GeneData::MAX_INTEGERS_COUNT; ++index) {
        assertTrue(copyView.integerValue(index)
                        == GeneData::INTEGER_MAX[index] - 1,
                "Compact integer storage round-trips every valid domain");
    }
    assertTrue(copyView.child1GeneIndex() == SELF_GENE_INDEX
                    && copyView.child2GeneIndex() == GeneIndex(255),
            "Compact 16-bit references preserve explicit and self values");
}

void testBuilderAndViewCoverEveryFieldFamily() {
    Gene gene;
    GeneBuilder builder(gene);
    builder.setColor(0, 0.1f);
    builder.setColor(1, 0.2f);
    builder.setColor(2, 0.3f);
    builder.setSplitMass(11.0f);
    builder.setSplitRatio(0.4f);
    builder.setSplitAngle(0.5f);
    builder.setChild1Angle(0.6f);
    builder.setChild2Angle(0.7f);
    builder.setMassFlowPriority(2.0f);
    builder.setLinkStrength(3.0f);
    builder.setChild1GeneIndex(GeneIndex(4));
    builder.setChild2GeneIndex(GeneIndex(5));
    builder.setMakesLink(true);
    builder.setChild1KeepsLinks(true);
    builder.setChild2KeepsLinks(false);
    builder.setStaysAlive(true);
    builder.setInitialCompatibilityFlag(true);
    builder.setChild1Mirrored(true);
    builder.setChild2Mirrored(false);
    builder.setCellType(CellType::NEUROCYTE);
    builder.setParametricVariableA(3, 0.25f);
    builder.setParametricVariableB(3, -0.5f);
    builder.setParametricVariableC(3, 0.75f);
    builder.setParametricSignalChannel(3, 6);
    builder.setParametricFormulaType(3, 2);
    builder.setIntegerValue(4, 8);
    builder.setVirusFromGeneIndex(GeneIndex(9));
    builder.setVirusToGeneIndex(GeneIndex(10));
    builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 1.5f);
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.25f);

    const GeneView view = builder.view();
    assertNear(0.1, view.color(0), 1.0e-6, "GeneView color 0");
    assertNear(0.2, view.color(1), 1.0e-6, "GeneView color 1");
    assertNear(0.3, view.color(2), 1.0e-6, "GeneView color 2");
    assertNear(11.0, view.splitMass(), 1.0e-6, "GeneView split mass");
    assertNear(0.4, view.splitRatio(), 1.0e-6, "GeneView split ratio");
    assertNear(0.5, view.splitAngle(), 1.0e-6, "GeneView split angle");
    assertNear(0.6, view.child1Angle(), 1.0e-6, "GeneView child 1 angle");
    assertNear(0.7, view.child2Angle(), 1.0e-6, "GeneView child 2 angle");
    assertNear(2.0, view.massFlowPriority(), 1.0e-6, "GeneView mass-flow priority");
    assertNear(3.0, view.linkStrength(), 1.0e-6, "GeneView link strength");
    assertTrue(view.child1GeneIndex() == GeneIndex(4), "GeneView child 1 index");
    assertTrue(view.child2GeneIndex() == GeneIndex(5), "GeneView child 2 index");
    assertTrue(view.makesLink() && view.child1KeepsLinks()
                    && !view.child2KeepsLinks() && view.staysAlive()
                    && view.initialCompatibilityFlag() && view.child1Mirrored()
                    && !view.child2Mirrored(),
            "GeneView flags");
    assertTrue(view.cellType() == CellType::NEUROCYTE, "GeneView Cell type");
    assertNear(0.25, view.parametricVariableA(3), 1.0e-6, "GeneView parametric A");
    assertNear(-0.5, view.parametricVariableB(3), 1.0e-6, "GeneView parametric B");
    assertNear(0.75, view.parametricVariableC(3), 1.0e-6, "GeneView parametric C");
    assertTrue(view.parametricSignalChannel(3) == 6, "GeneView signal channel");
    assertTrue(view.parametricFormulaType(3) == 2, "GeneView formula type");
    assertTrue(view.integerValue(4) == 8, "GeneView integer value");
    assertTrue(view.virusFromGeneIndex() == GeneIndex(9), "GeneView virus source");
    assertTrue(view.virusToGeneIndex() == GeneIndex(10), "GeneView virus target");
    assertNear(1.5, view.floatValue(Gene::FloatType::SMELL_AMPLITUDE), 1.0e-6,
            "GeneView smell amplitude");
    assertNear(1.25, view.floatValue(Gene::FloatType::CYTOSKELETON), 1.0e-6,
            "GeneView cytoskeleton");
}

void testCompactStoragePreservesEveryFloatBit() {
    GeneData source;
    GeneBuilder builder(source);
    const std::array<float, 3> colors{-0.0f, 0.123456791f, 1.0f};
    for (int channel = 0; channel < 3; ++channel) {
        builder.setColor(channel, colors[static_cast<std::size_t>(channel)]);
    }
    builder.setSplitMass(12.125f);
    builder.setSplitRatio(0.3125f);
    builder.setSplitAngle(0.625f);
    builder.setChild1Angle(1.25f);
    builder.setChild2Angle(2.5f);
    builder.setMassFlowPriority(1.375f);
    builder.setLinkStrength(0.875f);
    for (int index = 0; index < GeneData::MAX_PARAMETRIC_VALUES_COUNT; ++index) {
        const float base = static_cast<float>(index + 1) * 0.03125f;
        builder.setParametricVariableA(index, base);
        builder.setParametricVariableB(index, -base);
        builder.setParametricVariableC(index, base + 0.015625f);
    }
    const std::array<float, GeneData::MAX_FLOATS_COUNT> floatValues{
            -0.3125f, 0.125f, 0.25f, 0.5f, 0.75f, 0.00625f, 1.625f
    };
    for (int index = 0; index < GeneData::MAX_FLOATS_COUNT; ++index) {
        builder.setFloatValue(
                static_cast<GeneData::FloatType>(index),
                floatValues[static_cast<std::size_t>(index)]);
    }

    GeneData copy;
    GeneBuilder(copy).copyFrom(builder.view());
    const GeneView view(copy);
    for (int channel = 0; channel < 3; ++channel) {
        assertTrue(floatBits(view.color(channel))
                        == floatBits(colors[static_cast<std::size_t>(channel)]),
                "Compact color storage preserves exact float bits");
    }
    const std::array<float, 7> expectedScalars{
            12.125f, 0.3125f, 0.625f, 1.25f, 2.5f, 1.375f, 0.875f
    };
    const std::array<float, 7> actualScalars{
            view.splitMass(),
            view.splitRatio(),
            view.splitAngle(),
            view.child1Angle(),
            view.child2Angle(),
            view.massFlowPriority(),
            view.linkStrength()
    };
    for (std::size_t index = 0; index < expectedScalars.size(); ++index) {
        assertTrue(floatBits(actualScalars[index])
                        == floatBits(expectedScalars[index]),
                "Compact scalar storage preserves exact float bits");
    }
    for (int index = 0; index < GeneData::MAX_PARAMETRIC_VALUES_COUNT; ++index) {
        const float base = static_cast<float>(index + 1) * 0.03125f;
        assertTrue(floatBits(view.parametricVariableA(index)) == floatBits(base)
                        && floatBits(view.parametricVariableB(index))
                                == floatBits(-base)
                        && floatBits(view.parametricVariableC(index))
                                == floatBits(base + 0.015625f),
                "Compact parametric storage preserves exact float bits");
    }
    for (int index = 0; index < GeneData::MAX_FLOATS_COUNT; ++index) {
        assertTrue(
                floatBits(view.floatValue(
                        static_cast<GeneData::FloatType>(index)))
                        == floatBits(floatValues[static_cast<std::size_t>(index)]),
                "Compact special-float storage preserves exact bits");
    }
}

void testCopyAndGenomeViewsDoNotExposeContainerShape() {
    std::array<GeneData, 3> genes{};
    GeneBuilder(genes[1]).setSplitMass(13.0f);
    GeneBuilder(genes[2]).copyFrom(GeneView(genes[1]));

    const GenomeView genome(genes);
    assertTrue(genome.geneCount() == 3, "GenomeView reports the actual count");
    assertNear(13.0, genome.gene(GeneIndex(1)).splitMass(), 1.0e-6,
            "GenomeView resolves a typed index");
    assertNear(13.0, genome.gene(2).splitMass(), 1.0e-6,
            "GeneBuilder copies through GeneView");

    GeneBuilder(genes[1]).setSplitMass(14.0f);
    assertNear(13.0, genome.gene(2).splitMass(), 1.0e-6,
            "Builder copy owns independent compatibility storage");
}

} // namespace

int main() {
    testCompactGeneDataLayoutAndDescriptors();
    testBuilderAndViewCoverEveryFieldFamily();
    testCompactStoragePreservesEveryFloatBit();
    testCopyAndGenomeViewsDoNotExposeContainerShape();
    std::cout << "Gene access tests passed\n";
    return 0;
}
