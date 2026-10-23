#pragma once

#include "core/CanonicalGene.h"
#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "tests/support/TestAssertions.h"
#include <array>
#include <cmath>

namespace cellularity::tests {

inline FixedGenome canonicalDefaultGenome(
        GeneIndex initialGeneIndex = GeneIndex(0)) {
    FixedGenome genome{};
    for (std::size_t index = 0; index < genome.size(); ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto gene = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                owner,
                genome.size(),
                owner == initialGeneIndex);
        assertTrue(gene.has_value(), "Canonical default test Gene materializes");
        genome[index] = *gene;
    }
    return genome;
}

inline GenomeData canonicalDefaultGenomeData(
        std::size_t geneCount = 1,
        GeneIndex initialGeneIndex = GeneIndex(0)) {
    assertTrue(isSupportedGeneCount(geneCount),
            "Dynamic canonical test Genome length is supported");
    assertTrue(isGeneIndexInBounds(initialGeneIndex, geneCount),
            "Dynamic canonical test initial Gene is in bounds");
    GenomeData genome(geneCount);
    for (std::size_t index = 0; index < genome.size(); ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto gene = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                owner,
                genome.size(),
                owner == initialGeneIndex);
        assertTrue(gene.has_value(), "Dynamic canonical test Gene materializes");
        genome[index] = *gene;
    }
    return genome;
}

inline int addAliveCell(CellData& cellData, CellType cellType) {
    int index = cellData.allocateNewCell();
    const bool edited = cellData.editActiveGene(
            index,
            [cellType](GeneBuilder& builder) {
                builder.setCellType(cellType);
                builder.setFloatValue(
                        Gene::FloatType::CYTOSKELETON, 1.0f);
            });
    assertTrue(edited, "CellData test Cell active Gene initialized");
    cellData.mMass[index] = CellData::INITIAL_MASS;
    cellData.mRadius[index] = std::sqrt(cellData.mMass[index] / CellData::MASS_PER_AREA);
    cellData.mNitrates[index] = CellData::MAX_NITRATES;
    cellData.markCellAsAlive(index);
    return index;
}

inline void makeCellSplitReady(CellData& cellData, int cellIndex, double mass = CellData::INITIAL_MASS) {
    cellData.mMass[cellIndex] = mass;
    cellData.mRadius[cellIndex] = std::sqrt(cellData.mMass[cellIndex] / CellData::MASS_PER_AREA);
    cellData.mRadiusSq[cellIndex] = cellData.mRadius[cellIndex] * cellData.mRadius[cellIndex];
    cellData.mAge[cellIndex] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mNitrates[cellIndex] = CellData::MAX_NITRATES;
    cellData.mTelomeresCount[cellIndex] = -1;
    cellData.mOldLinkCount[cellIndex] = cellData.mTopology.linkCount(cellIndex);
}

inline bool sameParametricValue(
        GeneParametricValueView left,
        GeneParametricValueView right) {
    return left.variableA() == right.variableA()
            && left.variableB() == right.variableB()
            && left.variableC() == right.variableC()
            && left.signalChannel() == right.signalChannel()
            && left.formulaType() == right.formulaType();
}

inline bool sameGene(const Gene& left, const Gene& right) {
    const GeneView leftView(left);
    const GeneView rightView(right);
    for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
        if (!sameParametricValue(
                    leftView.parametricValue(valueIndex),
                    rightView.parametricValue(valueIndex))) {
            return false;
        }
    }
    for (int channel = 0; channel < 3; channel++) {
        if (leftView.color(channel) != rightView.color(channel)) {
            return false;
        }
    }
    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
        if (leftView.integerValue(index) != rightView.integerValue(index)) {
            return false;
        }
    }
    constexpr std::array<Gene::FloatType, Gene::MAX_FLOATS_COUNT> floatTypes{
            Gene::FloatType::SMELL_AMPLITUDE,
            Gene::FloatType::SMELL_RED,
            Gene::FloatType::SMELL_GREEN,
            Gene::FloatType::SMELL_BLUE,
            Gene::FloatType::SMELL_THRESHOLD,
            Gene::FloatType::ADHESIN_LENGTH,
            Gene::FloatType::CYTOSKELETON
    };
    for (Gene::FloatType type : floatTypes) {
        if (leftView.floatValue(type) != rightView.floatValue(type)) {
            return false;
        }
    }
    return leftView.splitMass() == rightView.splitMass()
            && leftView.splitRatio() == rightView.splitRatio()
            && leftView.splitAngle() == rightView.splitAngle()
            && leftView.child1Angle() == rightView.child1Angle()
            && leftView.child2Angle() == rightView.child2Angle()
            && leftView.massFlowPriority() == rightView.massFlowPriority()
            && leftView.linkStrength() == rightView.linkStrength()
            && leftView.child1GeneIndex() == rightView.child1GeneIndex()
            && leftView.child2GeneIndex() == rightView.child2GeneIndex()
            && leftView.makesLink() == rightView.makesLink()
            && leftView.child1KeepsLinks() == rightView.child1KeepsLinks()
            && leftView.child2KeepsLinks() == rightView.child2KeepsLinks()
            && leftView.staysAlive() == rightView.staysAlive()
            && leftView.initialCompatibilityFlag()
            == rightView.initialCompatibilityFlag()
            && leftView.child1Mirrored() == rightView.child1Mirrored()
            && leftView.child2Mirrored() == rightView.child2Mirrored()
            && leftView.cellType() == rightView.cellType();
}

template <typename LeftGenome, typename RightGenome>
inline bool sameGenome(const LeftGenome& left, const RightGenome& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t geneIndex = 0; geneIndex < left.size(); geneIndex++) {
        if (!sameGene(left[geneIndex], right[geneIndex])) {
            return false;
        }
    }
    return true;
}

inline std::array<GeneData, CellData::MAX_GENES_COUNT> copyGenome(const std::array<GeneData, CellData::MAX_GENES_COUNT>& source) {
    std::array<GeneData, CellData::MAX_GENES_COUNT> copy;
    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
        GeneBuilder(copy[geneIndex]).copyFrom(GeneView(source[geneIndex]));
    }
    return copy;
}

inline GenomeData copyGenome(
        const CellData& cellData,
        int cellIndex) {
    return cellData.materializeGenome(cellIndex);
}

template <typename ExpectedGenome>
inline bool sameGenome(
        const ExpectedGenome& expected,
        const CellData& cellData,
        int cellIndex) {
    return sameGenome(expected, cellData.materializeGenome(cellIndex));
}

inline void setConstantValue(
        GeneBuilder builder,
        int valueIndex,
        float normalizedValue) {
    builder.setParametricFormulaType(valueIndex, 0);
    builder.setParametricVariableB(valueIndex, normalizedValue);
}

inline void setConstantValue(Gene& gene, int valueIndex, float normalizedValue) {
    setConstantValue(GeneBuilder(gene), valueIndex, normalizedValue);
}

inline bool setConstantValue(
        CellData& cellData,
        int cellIndex,
        int valueIndex,
        float normalizedValue) {
    return cellData.editActiveGene(
            cellIndex,
            [=](GeneBuilder& builder) {
                setConstantValue(builder, valueIndex, normalizedValue);
            });
}

inline int addCiliocyteTestCell(CellData& cellData, CellType cellType, double x, double y, double mass, double radius) {
    int index = addAliveCell(cellData, cellType);
    const bool edited = cellData.editActiveGene(
            index,
            [](GeneBuilder& builder) {
                setConstantValue(
                        builder,
                        Gene::MYOCYTE_BENDING_VALUE_INDEX,
                        0.0f);
                setConstantValue(
                        builder,
                        Gene::MYOCYTE_LIFTING_VALUE_INDEX,
                        0.0f);
            });
    assertTrue(edited, "Ciliocyte test values initialized");
    cellData.mPositionX[index] = x;
    cellData.mPositionY[index] = y;
    cellData.mMass[index] = mass;
    cellData.mRadius[index] = radius;
    return index;
}

inline void addCollisionContact(CellData& cellData, int current, int collided) {
    double distanceX = cellData.mPositionX[current] - cellData.mPositionX[collided];
    double distanceY = cellData.mPositionY[current] - cellData.mPositionY[collided];
    double distanceSq = distanceX * distanceX + distanceY * distanceY;
    assertTrue(cellData.addCellCollisionContact(current, collided, distanceX, distanceY, distanceSq),
            "CellData test collision contact added");
}

inline int collisionContactTargetAt(const CellData& cellData, int cellIndex, int contactOffset) {
    int contactBegin = cellData.cellCollisionContactBegin(cellIndex);
    assertTrue(contactOffset >= 0 && contactOffset < cellData.cellCollisionContactCount(cellIndex),
            "CellData test collision contact offset valid");
    return cellData.cellCollisionContactTarget(contactBegin + contactOffset);
}

inline void addPairLink(CellData& cellData, int a, int b, double angleA, double angleB, float stiffness, float restLength) {
    int result = cellData.addLink(
            a,
            b,
            CellLinkCreationReason::UNSPECIFIED,
            angleA,
            angleB,
            stiffness,
            restLength);
    assertTrue(result >= 0, "CellData test pair link added");
}

inline double testMetabolismDrain(CellType cellType) {
    switch (cellType) {
        case CellType::PHAGOCYTE:
        case CellType::FLAGELLOCYTE:
        case CellType::DEVOROCYTE:
            return 0.1;
        case CellType::PHOTOCYTE:
            return 0.25;
        case CellType::LIPOCYTE:
        case CellType::GAMETE:
        case CellType::CILIOCYTE:
            return 0.005;
        case CellType::KERATINOCYTE:
        case CellType::GLUEOCYTE:
        case CellType::STEREOCYTE:
        case CellType::SENSEOCYTE:
            return 0.01;
        case CellType::BUOYOCYTE:
        case CellType::NITROCYTE:
        case CellType::MYOCYTE:
        case CellType::NEUROCYTE:
        case CellType::STEMOCYTE:
            return 0.02;
        case CellType::VIROCYTE:
            return 0.2;
        case CellType::SECROCYTE:
            return 0.03;
    }
    return 0.0;
}

inline double expectedIdleMetabolismCost(CellData& cellData, int cellIndex, CellType cellType, double deltaTime, const Environment& environment) {
    double finalCellIdleMetabolism = testMetabolismDrain(cellType)
            / (1.0 + cellData.mTopology.linkCount(cellIndex) * environment.mTestMulticellularCoef);
    double salinity = environment.getSalinity(cellData.mPositionX[cellIndex], cellData.mPositionY[cellIndex]);
    return finalCellIdleMetabolism
            * ((97.0 / 70.0) - (9.0 / 7.0) * salinity)
            * CellData::MASS_PER_AREA
            * (cellData.mRadius[cellIndex] + CellData::MAX_CELL_RADIUS * 0.25)
            * cellData.mRadius[cellIndex]
            * deltaTime;
}

} // namespace cellularity::tests
