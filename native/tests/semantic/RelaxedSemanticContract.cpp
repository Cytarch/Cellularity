#include "tests/semantic/RelaxedSemanticContract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/CellData.h"
#include "core/CellTopology.h"
#include "core/DeadSplashData.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/SimulationRandom.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderSourceFrame.h"
#include "verification/SemanticTrace.h"

namespace cellularity::semantic_tests::level2 {
namespace {

using verification::ScopedSemanticTrace;
using verification::SemanticEventKind;
using verification::SemanticTraceMode;
using verification::SemanticTraceResult;
using verification::SemanticTraceSink;
using verification::TraceChannel;
using verification::TraceChannelResult;

constexpr std::int64_t kInitialGlobalRandomSeed = 0x4c32'5eed'0919LL;
constexpr double kFiniteMagnitudeLimit = 1.0e12;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::string indexedName(const std::string& base, int index) {
    return base + '[' + std::to_string(index) + ']';
}

void requireNear(double reference,
        double candidate,
        const NumericalTolerance& tolerance,
        const std::string& field) {
    if (!std::isfinite(reference) || !std::isfinite(candidate)) {
        fail(field + ": non-finite numerical comparison");
    }
    const double scale = std::max(std::abs(reference), std::abs(candidate));
    const double limit = tolerance.absolute + tolerance.relative * scale;
    const double difference = std::abs(reference - candidate);
    if (difference > limit) {
        std::ostringstream out;
        out << std::setprecision(17) << field << ": numerical mismatch; reference="
            << reference << ", candidate=" << candidate << ", difference="
            << difference << ", limit=" << limit;
        fail(out.str());
    }
}

template <typename T>
void compareNumericVector(const std::vector<T>& reference,
        const std::vector<T>& candidate,
        int count,
        const NumericalTolerance& tolerance,
        const std::string& field) {
    require(count >= 0
                    && static_cast<std::size_t>(count) <= reference.size()
                    && static_cast<std::size_t>(count) <= candidate.size(),
            field + ": invalid active numerical range");
    for (int index = 0; index < count; index++) {
        requireNear(static_cast<double>(reference[static_cast<std::size_t>(index)]),
                static_cast<double>(candidate[static_cast<std::size_t>(index)]),
                tolerance,
                indexedName(field, index));
    }
}

template <typename T>
void requireFiniteVector(const std::vector<T>& values,
        int count,
        const std::string& field) {
    require(count >= 0 && static_cast<std::size_t>(count) <= values.size(),
            field + ": invalid active finite range");
    for (int index = 0; index < count; index++) {
        const double value = static_cast<double>(values[static_cast<std::size_t>(index)]);
        require(std::isfinite(value) && std::abs(value) <= kFiniteMagnitudeLimit,
                indexedName(field, index) + ": non-finite or physically unbounded value");
    }
}

void compareGeneDiscrete(const Gene& reference,
        const Gene& candidate,
        const std::string& field) {
    const GeneView referenceView(reference);
    const GeneView candidateView(candidate);
    require(referenceView.child1GeneIndex() == candidateView.child1GeneIndex()
                    && referenceView.child2GeneIndex()
                            == candidateView.child2GeneIndex()
                    && referenceView.makesLink() == candidateView.makesLink()
                    && referenceView.child1KeepsLinks()
                            == candidateView.child1KeepsLinks()
                    && referenceView.child2KeepsLinks()
                            == candidateView.child2KeepsLinks()
                    && referenceView.staysAlive() == candidateView.staysAlive()
                    && referenceView.initialCompatibilityFlag()
                            == candidateView.initialCompatibilityFlag()
                    && referenceView.child1Mirrored()
                            == candidateView.child1Mirrored()
                    && referenceView.child2Mirrored()
                            == candidateView.child2Mirrored()
                    && referenceView.cellType() == candidateView.cellType(),
            field + ": discrete Gene state changed");
    for (int integerIndex = 0;
            integerIndex < Gene::MAX_INTEGERS_COUNT;
            integerIndex++) {
        require(referenceView.integerValue(integerIndex)
                        == candidateView.integerValue(integerIndex),
                field + ": discrete Gene integer state changed");
    }
    for (int valueIndex = 0;
            valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT;
            valueIndex++) {
        const GeneParametricValueView left =
                referenceView.parametricValue(valueIndex);
        const GeneParametricValueView right =
                candidateView.parametricValue(valueIndex);
        require(left.signalChannel() == right.signalChannel()
                        && left.formulaType() == right.formulaType(),
                field + ".parametric[" + std::to_string(valueIndex)
                        + "]: discrete formula changed");
    }
}

void compareGeneNumerical(const Gene& reference,
        const Gene& candidate,
        const NumericalTolerance& tolerance,
        const std::string& field) {
    const GeneView referenceView(reference);
    const GeneView candidateView(candidate);
    for (int channel = 0; channel < 3; channel++) {
        requireNear(referenceView.color(channel), candidateView.color(channel), tolerance,
                field + ".color[" + std::to_string(channel) + ']');
    }
    requireNear(referenceView.splitMass(), candidateView.splitMass(), tolerance,
            field + ".splitMass");
    requireNear(referenceView.splitRatio(), candidateView.splitRatio(), tolerance,
            field + ".splitRatio");
    requireNear(referenceView.splitAngle(), candidateView.splitAngle(), tolerance,
            field + ".splitAngle");
    requireNear(referenceView.child1Angle(), candidateView.child1Angle(), tolerance,
            field + ".child1Angle");
    requireNear(referenceView.child2Angle(), candidateView.child2Angle(), tolerance,
            field + ".child2Angle");
    requireNear(referenceView.massFlowPriority(), candidateView.massFlowPriority(),
            tolerance, field + ".massFlowPriority");
    requireNear(referenceView.linkStrength(), candidateView.linkStrength(), tolerance,
            field + ".linkStrength");
    for (int valueIndex = 0;
            valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT;
            valueIndex++) {
        const GeneParametricValueView left =
                referenceView.parametricValue(valueIndex);
        const GeneParametricValueView right =
                candidateView.parametricValue(valueIndex);
        const std::string valueField = field + ".parametric["
                + std::to_string(valueIndex) + ']';
        requireNear(left.variableA(), right.variableA(), tolerance,
                valueField + ".a");
        requireNear(left.variableB(), right.variableB(), tolerance,
                valueField + ".b");
        requireNear(left.variableC(), right.variableC(), tolerance,
                valueField + ".c");
    }
    constexpr std::array<Gene::FloatType, Gene::MAX_FLOATS_COUNT> floatTypes{{
            Gene::FloatType::SMELL_AMPLITUDE,
            Gene::FloatType::SMELL_RED,
            Gene::FloatType::SMELL_GREEN,
            Gene::FloatType::SMELL_BLUE,
            Gene::FloatType::SMELL_THRESHOLD,
            Gene::FloatType::ADHESIN_LENGTH,
            Gene::FloatType::CYTOSKELETON,
    }};
    for (std::size_t index = 0; index < floatTypes.size(); index++) {
        requireNear(referenceView.floatValue(floatTypes[index]),
                candidateView.floatValue(floatTypes[index]), tolerance,
                field + ".float[" + std::to_string(index) + ']');
    }
}

void compareTopologyDiscrete(const CellTopology& reference,
        const CellTopology& candidate,
        int cellCount,
        const std::string& context) {
    require(reference.edgeCount() == candidate.edgeCount(),
            context + ": topology edge count changed");
    require(reference.boundaryCount() == candidate.boundaryCount(),
            context + ": topology boundary count changed");
    require(reference.structuralRevision() == candidate.structuralRevision(),
            context + ": topology structural revision changed");
    for (int edge = 0; edge < reference.edgeCount(); edge++) {
        require(reference.edgeRuntimeId(edge) == candidate.edgeRuntimeId(edge)
                        && reference.edgeCellA(edge) == candidate.edgeCellA(edge)
                        && reference.edgeCellB(edge) == candidate.edgeCellB(edge)
                        && reference.edgeCreationReason(edge)
                                == candidate.edgeCreationReason(edge),
                context + ": ordered topology edge changed at "
                        + std::to_string(edge));
    }
    for (int boundary = 0; boundary < reference.boundaryCount(); boundary++) {
        require(reference.boundaryCell(boundary)
                        == candidate.boundaryCell(boundary),
                context + ": ordered boundary link changed at "
                        + std::to_string(boundary));
    }
    for (int cell = 0; cell < cellCount; cell++) {
        require(reference.linkCount(cell) == candidate.linkCount(cell),
                context + ": Cell link count changed at "
                        + std::to_string(cell));
        for (int slot = 0; slot < reference.linkCount(cell); slot++) {
            require(reference.adjacentCell(cell, slot)
                                    == candidate.adjacentCell(cell, slot)
                            && reference.incidentEdgeIndex(cell, slot)
                                    == candidate.incidentEdgeIndex(cell, slot),
                    context + ": adjacency order changed at Cell "
                            + std::to_string(cell) + " slot "
                            + std::to_string(slot));
        }
    }
}

void compareCollisionGridDiscrete(const CollisionGrid& reference,
        const CollisionGrid& candidate,
        bool compareTypes,
        const std::string& context) {
    require(reference.stride() == candidate.stride()
                    && reference.gridSize() == candidate.gridSize()
                    && reference.touchedCount() == candidate.touchedCount(),
            context + ": grid layout or touched range changed");
    for (int touched = 0; touched < reference.touchedCount(); touched++) {
        const int referenceGridIndex = reference.touchedIndexAt(touched);
        const int candidateGridIndex = candidate.touchedIndexAt(touched);
        require(referenceGridIndex == candidateGridIndex,
                context + ": touched-grid order changed at "
                        + std::to_string(touched));
        const int referenceCount = reference.countAt(referenceGridIndex);
        const int candidateCount = candidate.countAt(candidateGridIndex);
        require(referenceCount == candidateCount,
                context + ": entry count changed at grid "
                        + std::to_string(referenceGridIndex));
        const int referenceBegin = reference.entryOffsetAt(referenceGridIndex);
        const int candidateBegin = candidate.entryOffsetAt(candidateGridIndex);
        for (int offset = 0; offset < referenceCount; offset++) {
            require(reference.indexAtOffset(referenceBegin + offset)
                            == candidate.indexAtOffset(candidateBegin + offset),
                    context + ": entry order changed at grid "
                            + std::to_string(referenceGridIndex) + " offset "
                            + std::to_string(offset));
            if (compareTypes) {
                require(reference.typeAtOffset(referenceBegin + offset)
                                == candidate.typeAtOffset(candidateBegin + offset),
                        context + ": entry type changed at grid "
                                + std::to_string(referenceGridIndex)
                                + " offset " + std::to_string(offset));
            }
        }
    }
}

void compareDiscreteState(const SimulationWorld& reference,
        const SimulationWorld& candidate,
        std::int64_t referenceGlobalSeed,
        std::int64_t candidateGlobalSeed,
        const std::string& context) {
    const CellData& left = reference.mCellData;
    const CellData& right = candidate.mCellData;
    require(reference.getUpdateStage() == candidate.getUpdateStage(),
            context + ": update stage changed");
    require(reference.cellContactCacheIsFresh()
                    == candidate.cellContactCacheIsFresh(),
            context + ": contact-cache freshness changed");
    require(reference.foodSpatialIndexIsFresh()
                    == candidate.foodSpatialIndexIsFresh(),
            context + ": Food spatial-index freshness changed");
    require(referenceGlobalSeed == candidateGlobalSeed,
            context + ": shared RNG order changed");
    require(reference.mEnvironment.mRandom.getSeed()
                    == candidate.mEnvironment.mRandom.getSeed(),
            context + ": Environment RNG order changed");
    require(left.mCellCount == right.mCellCount,
            context + ": active Cell count changed");
    require(left.nextOrganismId() == right.nextOrganismId(),
            context + ": next Organism identity changed");
    require(reference.mFoodData.mFoodCount == candidate.mFoodData.mFoodCount,
            context + ": active Food count changed");
    require(reference.mDeadSplashData.mDeadSplashCount
                    == candidate.mDeadSplashData.mDeadSplashCount,
            context + ": active dead-splash count changed");
    require(reference.mHasAnySmellConsumers == candidate.mHasAnySmellConsumers
                    && reference.mHasSmellGridConsumers
                            == candidate.mHasSmellGridConsumers
                    && reference.mHasFoodSmellConsumers
                            == candidate.mHasFoodSmellConsumers
                    && reference.mHasFoodPhysicsQueryConsumers
                            == candidate.mHasFoodPhysicsQueryConsumers
                    && reference.mFoodSmellGridConsumerMaskActive
                            == candidate.mFoodSmellGridConsumerMaskActive
                    && reference.mHasFertilizationIntents
                            == candidate.mHasFertilizationIntents,
            context + ": discrete world feature flags changed");

    for (int cell = 0; cell < left.mCellCount; cell++) {
        const std::size_t index = static_cast<std::size_t>(cell);
        const std::string cellField = context + ".Cell[" + std::to_string(cell)
                + ']';
        require(left.runtimeId(cell) == right.runtimeId(cell),
                cellField + ": runtime identity changed");
        require(left.mIsDead[index] == right.mIsDead[index]
                        && left.mIsMirrored[index] == right.mIsMirrored[index]
                        && left.mIsStuck[index] == right.mIsStuck[index]
                        && left.activeGeneIndex(cell)
                                == right.activeGeneIndex(cell)
                        && left.mOriginTag[index] == right.mOriginTag[index]
                        && left.mOrganismIds[index]
                                == right.mOrganismIds[index]
                        && left.mParentOrganismIds[index]
                                == right.mParentOrganismIds[index]
                        && left.mSelectTag[index] == right.mSelectTag[index]
                        && left.mMutationCount[index] == right.mMutationCount[index]
                        && left.mPendingMutationCount[index]
                                == right.mPendingMutationCount[index]
                        && left.mSeldomCounter[index] == right.mSeldomCounter[index]
                        && left.mOldLinkCount[index] == right.mOldLinkCount[index]
                        && left.mGridIndex1[index] == right.mGridIndex1[index]
                        && left.mTelomeresCount[index]
                                == right.mTelomeresCount[index]
                        && left.mActiveCellType[index]
                                == right.mActiveCellType[index]
                        && left.mFertilizationTarget[index]
                                == right.mFertilizationTarget[index],
                cellField + ": discrete Cell state changed");
        const GenomeData leftGenome = left.materializeGenome(cell);
        const GenomeData rightGenome = right.materializeGenome(cell);
        require(leftGenome.size() == rightGenome.size(),
                cellField + ": Genome length changed");
        for (std::size_t gene = 0; gene < leftGenome.size(); gene++) {
            compareGeneDiscrete(
                    leftGenome[gene],
                    rightGenome[gene],
                    cellField + ".gene[" + std::to_string(gene) + ']');
        }
        const int leftBegin = left.cellCollisionContactBegin(cell);
        const int rightBegin = right.cellCollisionContactBegin(cell);
        const int leftCount = left.cellCollisionContactCount(cell);
        const int rightCount = right.cellCollisionContactCount(cell);
        require(leftCount == rightCount,
                cellField + ": directed contact count changed");
        for (int offset = 0; offset < leftCount; offset++) {
            require(left.cellCollisionContactTarget(leftBegin + offset)
                            == right.cellCollisionContactTarget(
                                    rightBegin + offset),
                    cellField + ": directed contact order changed at "
                            + std::to_string(offset));
        }
    }

    const auto& leftIntents = left.mCellCollisionIntentBuffer;
    const auto& rightIntents = right.mCellCollisionIntentBuffer;
    require(leftIntents.count == rightIntents.count,
            context + ": collision-intent count changed");
    for (int intent = 0; intent < leftIntents.count; intent++) {
        require(leftIntents.currentCellIndex[intent]
                                == rightIntents.currentCellIndex[intent]
                        && leftIntents.collidedCellIndex[intent]
                                == rightIntents.collidedCellIndex[intent],
                context + ": collision-intent order changed at "
                        + std::to_string(intent));
    }
    require(left.mCiliocyteInteractionCount
                    == right.mCiliocyteInteractionCount,
            context + ": Ciliocyte queue count changed");
    for (int interaction = 0;
            interaction < left.mCiliocyteInteractionCount;
            interaction++) {
        require(left.mCiliocyteInteractionCellIndex1[interaction]
                                == right.mCiliocyteInteractionCellIndex1[interaction]
                        && left.mCiliocyteInteractionCellIndex2[interaction]
                                == right.mCiliocyteInteractionCellIndex2[interaction],
                context + ": Ciliocyte queue order changed at "
                        + std::to_string(interaction));
    }

    compareTopologyDiscrete(left.mTopology, right.mTopology, left.mCellCount,
            context);
    require(reference.mCellCollisionGrid != nullptr
                    && candidate.mCellCollisionGrid != nullptr
                    && reference.mSmellCollisionGrid != nullptr
                    && candidate.mSmellCollisionGrid != nullptr,
            context + ": spatial grid is missing");
    compareCollisionGridDiscrete(*reference.mCellCollisionGrid,
            *candidate.mCellCollisionGrid, false,
            context + ".cellGrid");
    compareCollisionGridDiscrete(*reference.mSmellCollisionGrid,
            *candidate.mSmellCollisionGrid, true,
            context + ".smellGrid");
    for (int food = 0; food < reference.mFoodData.mFoodCount; food++) {
        const std::size_t index = static_cast<std::size_t>(food);
        require(reference.mFoodData.runtimeId(food)
                                == candidate.mFoodData.runtimeId(food)
                        && reference.mFoodData.mCellCollisionGridIndex[index]
                                == candidate.mFoodData.mCellCollisionGridIndex[index]
                        && reference.mFoodData.mCellCollisionGridIndexValid[index]
                                == candidate.mFoodData.mCellCollisionGridIndexValid[index]
                        && reference.mFoodData
                                        .mCellCollisionGridMappingGeneration[index]
                                == candidate.mFoodData
                                        .mCellCollisionGridMappingGeneration[index],
                context + ": Food identity or spatial mapping changed at "
                        + std::to_string(food));
    }
    for (int splash = 0;
            splash < reference.mDeadSplashData.mDeadSplashCount;
            splash++) {
        require(reference.mDeadSplashData.runtimeId(splash)
                        == candidate.mDeadSplashData.runtimeId(splash),
                context + ": dead-splash runtime identity changed at "
                        + std::to_string(splash));
    }
}

void compareTopologyNumerical(const CellTopology& reference,
        const CellTopology& candidate,
        const NumericalTolerance& doubleTolerance,
        const NumericalTolerance& floatTolerance,
        const std::string& context) {
    for (int edge = 0; edge < reference.edgeCount(); edge++) {
        const std::string field = context + ".edge[" + std::to_string(edge) + ']';
        requireNear(reference.edgeAngleA(edge), candidate.edgeAngleA(edge),
                doubleTolerance, field + ".angleA");
        requireNear(reference.edgeAngleB(edge), candidate.edgeAngleB(edge),
                doubleTolerance, field + ".angleB");
        requireNear(reference.edgeAngleACos(edge), candidate.edgeAngleACos(edge),
                doubleTolerance, field + ".angleACos");
        requireNear(reference.edgeAngleASin(edge), candidate.edgeAngleASin(edge),
                doubleTolerance, field + ".angleASin");
        requireNear(reference.edgeAngleBCos(edge), candidate.edgeAngleBCos(edge),
                doubleTolerance, field + ".angleBCos");
        requireNear(reference.edgeAngleBSin(edge), candidate.edgeAngleBSin(edge),
                doubleTolerance, field + ".angleBSin");
        requireNear(reference.edgeTorsionalStiffness(edge),
                candidate.edgeTorsionalStiffness(edge), floatTolerance,
                field + ".torsionalStiffness");
        requireNear(reference.edgeRestLength(edge), candidate.edgeRestLength(edge),
                floatTolerance, field + ".restLength");
        requireNear(reference.edgeRenderStartOffsetAX(edge),
                candidate.edgeRenderStartOffsetAX(edge), floatTolerance,
                field + ".renderStartAX");
        requireNear(reference.edgeRenderStartOffsetAY(edge),
                candidate.edgeRenderStartOffsetAY(edge), floatTolerance,
                field + ".renderStartAY");
        requireNear(reference.edgeRenderEndOffsetAX(edge),
                candidate.edgeRenderEndOffsetAX(edge), floatTolerance,
                field + ".renderEndAX");
        requireNear(reference.edgeRenderEndOffsetAY(edge),
                candidate.edgeRenderEndOffsetAY(edge), floatTolerance,
                field + ".renderEndAY");
    }
    for (int boundary = 0; boundary < reference.boundaryCount(); boundary++) {
        const std::string field = context + ".boundary["
                + std::to_string(boundary) + ']';
        requireNear(reference.boundaryAngleA(boundary),
                candidate.boundaryAngleA(boundary), doubleTolerance,
                field + ".angleA");
        requireNear(reference.boundaryAngleACos(boundary),
                candidate.boundaryAngleACos(boundary), doubleTolerance,
                field + ".angleACos");
        requireNear(reference.boundaryAngleASin(boundary),
                candidate.boundaryAngleASin(boundary), doubleTolerance,
                field + ".angleASin");
        requireNear(reference.boundaryAnchorX(boundary),
                candidate.boundaryAnchorX(boundary), doubleTolerance,
                field + ".anchorX");
        requireNear(reference.boundaryAnchorY(boundary),
                candidate.boundaryAnchorY(boundary), doubleTolerance,
                field + ".anchorY");
        requireNear(reference.boundaryTorsionalStiffness(boundary),
                candidate.boundaryTorsionalStiffness(boundary),
                floatTolerance, field + ".torsionalStiffness");
        requireNear(reference.boundaryRestLength(boundary),
                candidate.boundaryRestLength(boundary), floatTolerance,
                field + ".restLength");
        requireNear(reference.boundaryRenderStartOffsetX(boundary),
                candidate.boundaryRenderStartOffsetX(boundary),
                floatTolerance, field + ".renderStartX");
        requireNear(reference.boundaryRenderStartOffsetY(boundary),
                candidate.boundaryRenderStartOffsetY(boundary),
                floatTolerance, field + ".renderStartY");
    }
}

void compareNumericalState(const SimulationWorld& reference,
        const SimulationWorld& candidate,
        const ContractConfiguration& configuration,
        const std::string& context) {
    const CellData& left = reference.mCellData;
    const CellData& right = candidate.mCellData;
    const int cellCount = left.mCellCount;
    const NumericalTolerance& d = configuration.doubleTolerance;
    const NumericalTolerance& f = configuration.floatTolerance;

    requireNear(reference.mTime, candidate.mTime, d, context + ".worldTime");
    requireNear(reference.mEnvironment.mLightAngle,
            candidate.mEnvironment.mLightAngle, d,
            context + ".environment.lightAngle");
    requireNear(reference.mEnvironment.mLightAngleCos,
            candidate.mEnvironment.mLightAngleCos, d,
            context + ".environment.lightAngleCos");
    requireNear(reference.mEnvironment.mLightAngleSin,
            candidate.mEnvironment.mLightAngleSin, d,
            context + ".environment.lightAngleSin");

    compareNumericVector(left.mPositionX, right.mPositionX, cellCount, d,
            context + ".positionX");
    compareNumericVector(left.mPositionY, right.mPositionY, cellCount, d,
            context + ".positionY");
    compareNumericVector(left.mPositionZ, right.mPositionZ, cellCount, d,
            context + ".positionZ");
    compareNumericVector(left.mRadius, right.mRadius, cellCount, d,
            context + ".radius");
    compareNumericVector(left.mRadiusSq, right.mRadiusSq, cellCount, d,
            context + ".radiusSq");
    compareNumericVector(left.mVelocityX, right.mVelocityX, cellCount, d,
            context + ".velocityX");
    compareNumericVector(left.mVelocityY, right.mVelocityY, cellCount, d,
            context + ".velocityY");
    compareNumericVector(left.mForceX, right.mForceX, cellCount, d,
            context + ".forceX");
    compareNumericVector(left.mForceY, right.mForceY, cellCount, d,
            context + ".forceY");
    compareNumericVector(left.mForceZ, right.mForceZ, cellCount, d,
            context + ".forceZ");
    compareNumericVector(left.mTorque, right.mTorque, cellCount, d,
            context + ".torque");
    compareNumericVector(left.mAngle, right.mAngle, cellCount, d,
            context + ".angle");
    compareNumericVector(left.mCachedAngle, right.mCachedAngle, cellCount, d,
            context + ".cachedAngle");
    compareNumericVector(left.mCachedAngleCos, right.mCachedAngleCos, cellCount,
            d, context + ".cachedAngleCos");
    compareNumericVector(left.mCachedAngleSin, right.mCachedAngleSin, cellCount,
            d, context + ".cachedAngleSin");
    compareNumericVector(left.mAngleCytoplasm, right.mAngleCytoplasm, cellCount,
            d, context + ".angleCytoplasm");
    compareNumericVector(left.mAngularVelocity, right.mAngularVelocity,
            cellCount, d, context + ".angularVelocity");
    compareNumericVector(left.mAngularVelocityCytoplasm,
            right.mAngularVelocityCytoplasm, cellCount, d,
            context + ".angularVelocityCytoplasm");
    compareNumericVector(left.mAge, right.mAge, cellCount, d,
            context + ".age");
    compareNumericVector(left.mNitrates, right.mNitrates, cellCount, d,
            context + ".nitrates");
    compareNumericVector(left.mHairRotation, right.mHairRotation, cellCount, d,
            context + ".hairRotation");
    compareNumericVector(left.hairX, right.hairX, cellCount, d,
            context + ".hairX");
    compareNumericVector(left.hairY, right.hairY, cellCount, d,
            context + ".hairY");
    compareNumericVector(left.mMass, right.mMass, cellCount, d,
            context + ".mass");
    compareNumericVector(left.mNewMass, right.mNewMass, cellCount, d,
            context + ".newMass");
    compareNumericVector(left.mNewNitrates, right.mNewNitrates, cellCount, d,
            context + ".newNitrates");
    compareNumericVector(left.mMetabolismScaledMass,
            right.mMetabolismScaledMass, cellCount, d,
            context + ".metabolismScaledMass");
    compareNumericVector(left.mMetabolismMassFlowPriority,
            right.mMetabolismMassFlowPriority, cellCount, d,
            context + ".metabolismMassFlowPriority");
    compareNumericVector(left.mActiveCellCollisionElasticity,
            right.mActiveCellCollisionElasticity, cellCount, d,
            context + ".activeCollisionElasticity");

    compareNumericVector(left.mSwimAnimationTime, right.mSwimAnimationTime,
            cellCount, f, context + ".swimAnimationTime");
    compareNumericVector(left.mSwimAnimationTime2, right.mSwimAnimationTime2,
            cellCount, f, context + ".swimAnimationTime2");
    compareNumericVector(left.mLipids, right.mLipids, cellCount, f,
            context + ".lipids");
    compareNumericVector(left.mToxin, right.mToxin, cellCount, f,
            context + ".toxin");
    compareNumericVector(left.mToxication, right.mToxication, cellCount, f,
            context + ".toxication");
    compareNumericVector(left.mNewToxin, right.mNewToxin, cellCount, f,
            context + ".newToxin");
    compareNumericVector(left.mMyocyteContraction, right.mMyocyteContraction,
            cellCount, f, context + ".myocyteContraction");
    compareNumericVector(left.mMyocyteBending, right.mMyocyteBending,
            cellCount, f, context + ".myocyteBending");
    compareNumericVector(left.mMyocyteLifting, right.mMyocyteLifting,
            cellCount, f, context + ".myocyteLifting");

    for (int cell = 0; cell < cellCount; cell++) {
        const std::size_t index = static_cast<std::size_t>(cell);
        const std::string field = context + ".Cell[" + std::to_string(cell) + ']';
        for (std::size_t channel = 0; channel < left.mColor[index].size(); channel++) {
            requireNear(left.mColor[index][channel], right.mColor[index][channel],
                    f, field + ".color[" + std::to_string(channel) + ']');
        }
        for (std::size_t channel = 0;
                channel < left.mSignalSubstance[index].size();
                channel++) {
            requireNear(left.mSignalSubstance[index][channel],
                    right.mSignalSubstance[index][channel], f,
                    field + ".signal[" + std::to_string(channel) + ']');
        }
        for (std::size_t channel = 0;
                channel < left.mNewSignalSubstance[index].size();
                channel++) {
            requireNear(left.mNewSignalSubstance[index][channel],
                    right.mNewSignalSubstance[index][channel], f,
                    field + ".newSignal[" + std::to_string(channel) + ']');
            requireNear(left.mSignalSubstanceOut[index][channel],
                    right.mSignalSubstanceOut[index][channel], f,
                    field + ".signalOut[" + std::to_string(channel) + ']');
        }
        const GenomeData leftGenome = left.materializeGenome(cell);
        const GenomeData rightGenome = right.materializeGenome(cell);
        require(leftGenome.size() == rightGenome.size(),
                field + ": Genome length changed");
        for (std::size_t gene = 0; gene < leftGenome.size(); gene++) {
            compareGeneNumerical(
                    leftGenome[gene],
                    rightGenome[gene], f,
                    field + ".gene[" + std::to_string(gene) + ']');
        }
        const int leftBegin = left.cellCollisionContactBegin(cell);
        const int rightBegin = right.cellCollisionContactBegin(cell);
        for (int offset = 0; offset < left.cellCollisionContactCount(cell);
                offset++) {
            const auto& leftGeometry =
                    left.mCellContactBuffer.geometry[leftBegin + offset];
            const auto& rightGeometry =
                    right.mCellContactBuffer.geometry[rightBegin + offset];
            const std::string contactField = field + ".contact["
                    + std::to_string(offset) + ']';
            requireNear(leftGeometry.distanceX, rightGeometry.distanceX, d,
                    contactField + ".distanceX");
            requireNear(leftGeometry.distanceY, rightGeometry.distanceY, d,
                    contactField + ".distanceY");
            requireNear(leftGeometry.distanceSq, rightGeometry.distanceSq, d,
                    contactField + ".distanceSq");
        }
    }

    for (int intent = 0; intent < left.mCellCollisionIntentBuffer.count;
            intent++) {
        const std::string field = context + ".intent["
                + std::to_string(intent) + ']';
        requireNear(left.mCellCollisionIntentBuffer.distanceX[intent],
                right.mCellCollisionIntentBuffer.distanceX[intent], d,
                field + ".distanceX");
        requireNear(left.mCellCollisionIntentBuffer.distanceY[intent],
                right.mCellCollisionIntentBuffer.distanceY[intent], d,
                field + ".distanceY");
        requireNear(left.mCellCollisionIntentBuffer.distance[intent],
                right.mCellCollisionIntentBuffer.distance[intent], d,
                field + ".distance");
        requireNear(left.mCellCollisionIntentBuffer.contactChordLengthSquared[intent],
                right.mCellCollisionIntentBuffer.contactChordLengthSquared[intent],
                d, field + ".contactChordLengthSquared");
    }

    compareTopologyNumerical(left.mTopology, right.mTopology, d, f,
            context + ".topology");

    const int foodCount = reference.mFoodData.mFoodCount;
    compareNumericVector(reference.mFoodData.mPositionX,
            candidate.mFoodData.mPositionX, foodCount, f,
            context + ".food.positionX");
    compareNumericVector(reference.mFoodData.mPositionY,
            candidate.mFoodData.mPositionY, foodCount, f,
            context + ".food.positionY");
    compareNumericVector(reference.mFoodData.mMass,
            candidate.mFoodData.mMass, foodCount, f,
            context + ".food.mass");
    compareNumericVector(reference.mFoodData.mVelocityX,
            candidate.mFoodData.mVelocityX, foodCount, f,
            context + ".food.velocityX");
    compareNumericVector(reference.mFoodData.mVelocityY,
            candidate.mFoodData.mVelocityY, foodCount, f,
            context + ".food.velocityY");
    compareNumericVector(reference.mFoodData.mLipidLayer,
            candidate.mFoodData.mLipidLayer, foodCount, f,
            context + ".food.lipidLayer");

    const int splashCount = reference.mDeadSplashData.mDeadSplashCount;
    compareNumericVector(reference.mDeadSplashData.mPositionX,
            candidate.mDeadSplashData.mPositionX, splashCount, f,
            context + ".splash.positionX");
    compareNumericVector(reference.mDeadSplashData.mPositionY,
            candidate.mDeadSplashData.mPositionY, splashCount, f,
            context + ".splash.positionY");
    compareNumericVector(reference.mDeadSplashData.mRadius,
            candidate.mDeadSplashData.mRadius, splashCount, f,
            context + ".splash.radius");
    compareNumericVector(reference.mDeadSplashData.mAge,
            candidate.mDeadSplashData.mAge, splashCount, f,
            context + ".splash.age");
    for (int splash = 0; splash < splashCount; splash++) {
        for (std::size_t channel = 0;
                channel < reference.mDeadSplashData.mColor[splash].size();
                channel++) {
            requireNear(reference.mDeadSplashData.mColor[splash][channel],
                    candidate.mDeadSplashData.mColor[splash][channel], f,
                    context + ".splash.color[" + std::to_string(splash)
                            + "][" + std::to_string(channel) + ']');
        }
    }
}

void verifyWorldInvariants(const SimulationWorld& world, const std::string& context) {
    const CellData& cells = world.mCellData;
    require(cells.mCellCount >= 0 && cells.mCellCount <= cells.MAX_CELL_COUNT,
            context + ": active Cell range is invalid");
    require(world.mFoodData.mFoodCount >= 0
                    && world.mFoodData.mFoodCount
                            <= world.mFoodData.MAX_FOOD_COUNT,
            context + ": active Food range is invalid");
    require(world.mDeadSplashData.mDeadSplashCount >= 0
                    && world.mDeadSplashData.mDeadSplashCount
                            <= world.mDeadSplashData.MAX_DEAD_SPLASH_COUNT,
            context + ": active dead-splash range is invalid");
    require(cells.mTopology.validate(),
            context + ": topology validation failed");
    require(cells.validateOrganismIdentities(),
            context + ": Organism identity validation failed");
    require(std::isfinite(world.mTime)
                    && std::abs(world.mTime) <= kFiniteMagnitudeLimit,
            context + ": world time is non-finite or unbounded");
    require(std::isfinite(world.mEnvironment.mLightAngle)
                    && std::isfinite(world.mEnvironment.mLightAngleCos)
                    && std::isfinite(world.mEnvironment.mLightAngleSin),
            context + ": dynamic Environment state is non-finite");

    requireFiniteVector(cells.mPositionX, cells.mCellCount,
            context + ".positionX");
    requireFiniteVector(cells.mPositionY, cells.mCellCount,
            context + ".positionY");
    requireFiniteVector(cells.mPositionZ, cells.mCellCount,
            context + ".positionZ");
    requireFiniteVector(cells.mRadius, cells.mCellCount,
            context + ".radius");
    requireFiniteVector(cells.mRadiusSq, cells.mCellCount,
            context + ".radiusSq");
    requireFiniteVector(cells.mVelocityX, cells.mCellCount,
            context + ".velocityX");
    requireFiniteVector(cells.mVelocityY, cells.mCellCount,
            context + ".velocityY");
    requireFiniteVector(cells.mForceX, cells.mCellCount,
            context + ".forceX");
    requireFiniteVector(cells.mForceY, cells.mCellCount,
            context + ".forceY");
    requireFiniteVector(cells.mForceZ, cells.mCellCount,
            context + ".forceZ");
    requireFiniteVector(cells.mTorque, cells.mCellCount,
            context + ".torque");
    requireFiniteVector(cells.mAngle, cells.mCellCount,
            context + ".angle");
    requireFiniteVector(cells.mCachedAngle, cells.mCellCount,
            context + ".cachedAngle");
    requireFiniteVector(cells.mCachedAngleCos, cells.mCellCount,
            context + ".cachedAngleCos");
    requireFiniteVector(cells.mCachedAngleSin, cells.mCellCount,
            context + ".cachedAngleSin");
    requireFiniteVector(cells.mAngleCytoplasm, cells.mCellCount,
            context + ".angleCytoplasm");
    requireFiniteVector(cells.mAngularVelocity, cells.mCellCount,
            context + ".angularVelocity");
    requireFiniteVector(cells.mAngularVelocityCytoplasm, cells.mCellCount,
            context + ".angularVelocityCytoplasm");
    requireFiniteVector(cells.mAge, cells.mCellCount, context + ".age");
    requireFiniteVector(cells.mMass, cells.mCellCount, context + ".mass");
    requireFiniteVector(cells.mNitrates, cells.mCellCount,
            context + ".nitrates");
    requireFiniteVector(cells.mSwimAnimationTime, cells.mCellCount,
            context + ".swimAnimationTime");
    requireFiniteVector(cells.mSwimAnimationTime2, cells.mCellCount,
            context + ".swimAnimationTime2");
    requireFiniteVector(cells.mHairRotation, cells.mCellCount,
            context + ".hairRotation");
    requireFiniteVector(cells.hairX, cells.mCellCount, context + ".hairX");
    requireFiniteVector(cells.hairY, cells.mCellCount, context + ".hairY");
    requireFiniteVector(cells.mNewMass, cells.mCellCount,
            context + ".newMass");
    requireFiniteVector(cells.mNewNitrates, cells.mCellCount,
            context + ".newNitrates");
    requireFiniteVector(cells.mMetabolismScaledMass, cells.mCellCount,
            context + ".metabolismScaledMass");
    requireFiniteVector(cells.mMetabolismMassFlowPriority, cells.mCellCount,
            context + ".metabolismMassFlowPriority");
    requireFiniteVector(cells.mActiveCellCollisionElasticity,
            cells.mCellCount,
            context + ".activeCollisionElasticity");
    requireFiniteVector(cells.mLipids, cells.mCellCount,
            context + ".lipids");
    requireFiniteVector(cells.mToxin, cells.mCellCount, context + ".toxin");
    requireFiniteVector(cells.mToxication, cells.mCellCount,
            context + ".toxication");
    requireFiniteVector(cells.mNewToxin, cells.mCellCount,
            context + ".newToxin");
    requireFiniteVector(cells.mMyocyteContraction, cells.mCellCount,
            context + ".myocyteContraction");
    requireFiniteVector(cells.mMyocyteBending, cells.mCellCount,
            context + ".myocyteBending");
    requireFiniteVector(cells.mMyocyteLifting, cells.mCellCount,
            context + ".myocyteLifting");

    for (int cell = 0; cell < cells.mCellCount; cell++) {
        const std::size_t index = static_cast<std::size_t>(cell);
        require(cells.mRadius[index] > 0.0
                        && cells.mRadiusSq[index] >= 0.0
                        && cells.mMass[index] >= 0.0,
                context + ": invalid physical Cell scalar at "
                        + std::to_string(cell));
        require(std::abs(cells.mPositionX[index])
                                <= world.mEnvironment.mRadius * 4.0 + 1.0
                        && std::abs(cells.mPositionY[index])
                                <= world.mEnvironment.mRadius * 4.0 + 1.0,
                context + ": Cell escaped the bounded verification domain at "
                        + std::to_string(cell));
        for (float value : cells.mColor[index]) {
            require(std::isfinite(value),
                    context + ": non-finite Cell color at Cell "
                            + std::to_string(cell));
        }
        for (float value : cells.mSignalSubstance[index]) {
            require(std::isfinite(value)
                            && std::abs(static_cast<double>(value))
                                    <= kFiniteMagnitudeLimit,
                    context + ": non-finite signal state at Cell "
                            + std::to_string(cell));
        }
        for (float value : cells.mNewSignalSubstance[index]) {
            require(std::isfinite(value),
                    context + ": non-finite new signal state at Cell "
                            + std::to_string(cell));
        }
        for (float value : cells.mSignalSubstanceOut[index]) {
            require(std::isfinite(value),
                    context + ": non-finite signal output at Cell "
                            + std::to_string(cell));
        }
        const GenomeView genome = cells.getGenomeView(cell);
        for (std::size_t geneIndex = 0;
                geneIndex < genome.geneCount();
                ++geneIndex) {
            const GeneView geneView =
                    genome.gene(geneIndex);
            const auto finite = [](double value) {
                return std::isfinite(value)
                        && std::abs(value) <= kFiniteMagnitudeLimit;
            };
            bool geneFinite = finite(geneView.splitMass())
                    && finite(geneView.splitRatio())
                    && finite(geneView.splitAngle())
                    && finite(geneView.child1Angle())
                    && finite(geneView.child2Angle())
                    && finite(geneView.massFlowPriority())
                    && finite(geneView.linkStrength());
            for (int channel = 0; channel < 3; channel++) {
                geneFinite = geneFinite && finite(geneView.color(channel));
            }
            for (int valueIndex = 0;
                    valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT;
                    valueIndex++) {
                const GeneParametricValueView value =
                        geneView.parametricValue(valueIndex);
                geneFinite = geneFinite && finite(value.variableA())
                        && finite(value.variableB())
                        && finite(value.variableC());
            }
            constexpr std::array<Gene::FloatType, Gene::MAX_FLOATS_COUNT>
                    floatTypes{{
                            Gene::FloatType::SMELL_AMPLITUDE,
                            Gene::FloatType::SMELL_RED,
                            Gene::FloatType::SMELL_GREEN,
                            Gene::FloatType::SMELL_BLUE,
                            Gene::FloatType::SMELL_THRESHOLD,
                            Gene::FloatType::ADHESIN_LENGTH,
                            Gene::FloatType::CYTOSKELETON,
                    }};
            for (Gene::FloatType type : floatTypes) {
                geneFinite = geneFinite && finite(geneView.floatValue(type));
            }
            require(geneFinite,
                    context + ": non-finite Gene state at Cell "
                            + std::to_string(cell) + " gene "
                            + std::to_string(geneIndex));
        }
        const int begin = cells.cellCollisionContactBegin(cell);
        const int end = cells.cellCollisionContactEnd(cell);
        require(end - begin >= 0
                        && end - begin <= CellData::MAX_COLLISIONS_PER_CELL,
                context + ": contact capacity invariant failed at Cell "
                        + std::to_string(cell));
        for (int contact = begin; contact < end; contact++) {
            const int target = cells.cellCollisionContactTarget(contact);
            require(target >= 0 && target < cells.mCellCount,
                    context + ": invalid directed contact endpoint");
            const auto& geometry = cells.mCellContactBuffer.geometry[contact];
            require(std::isfinite(geometry.distanceX)
                            && std::isfinite(geometry.distanceY)
                            && std::isfinite(geometry.distanceSq)
                            && geometry.distanceSq >= 0.0,
                    context + ": invalid directed contact geometry");
        }
    }

    const auto edgeView = cells.mTopology.edgeView();
    for (int edge = 0; edge < edgeView.count; edge++) {
        require(edgeView.cellA[edge] >= 0
                        && edgeView.cellA[edge] < cells.mCellCount
                        && edgeView.cellB[edge] >= 0
                        && edgeView.cellB[edge] < cells.mCellCount
                        && isValidCellLinkCreationReason(
                                edgeView.creationReason[edge]),
                context + ": invalid topology edge endpoint");
        require(std::isfinite(edgeView.angleA[edge])
                        && std::isfinite(edgeView.angleB[edge])
                        && std::isfinite(edgeView.angleACos[edge])
                        && std::isfinite(edgeView.angleASin[edge])
                        && std::isfinite(edgeView.angleBCos[edge])
                        && std::isfinite(edgeView.angleBSin[edge])
                        && std::isfinite(edgeView.torsionalStiffness[edge])
                        && std::isfinite(edgeView.restLength[edge]),
                context + ": non-finite topology edge state");
    }
    const auto boundaryView = cells.mTopology.boundaryView();
    for (int boundary = 0; boundary < boundaryView.count; boundary++) {
        require(boundaryView.cell[boundary] >= 0
                        && boundaryView.cell[boundary] < cells.mCellCount,
                context + ": invalid boundary-link endpoint");
        require(std::isfinite(boundaryView.angleA[boundary])
                        && std::isfinite(boundaryView.angleACos[boundary])
                        && std::isfinite(boundaryView.angleASin[boundary])
                        && std::isfinite(boundaryView.anchorX[boundary])
                        && std::isfinite(boundaryView.anchorY[boundary])
                        && std::isfinite(
                                boundaryView.torsionalStiffness[boundary])
                        && std::isfinite(boundaryView.restLength[boundary]),
                context + ": non-finite boundary-link state");
    }
    for (int intent = 0; intent < cells.mCellCollisionIntentBuffer.count;
            intent++) {
        const int actor = cells.mCellCollisionIntentBuffer.currentCellIndex[intent];
        const int target = cells.mCellCollisionIntentBuffer.collidedCellIndex[intent];
        require(actor >= 0 && actor < cells.mCellCount
                        && target >= 0 && target < cells.mCellCount,
                context + ": invalid collision-intent endpoint");
        require(std::isfinite(
                        cells.mCellCollisionIntentBuffer.distanceX[intent])
                        && std::isfinite(
                                cells.mCellCollisionIntentBuffer.distanceY[intent])
                        && std::isfinite(
                                cells.mCellCollisionIntentBuffer.distance[intent])
                        && std::isfinite(cells.mCellCollisionIntentBuffer
                                        .contactChordLengthSquared[intent]),
                context + ": non-finite collision-intent geometry");
    }
    require(cells.mCiliocyteInteractionCount >= 0
                    && static_cast<std::size_t>(cells.mCiliocyteInteractionCount)
                            <= cells.mCiliocyteInteractionCellIndex1.size(),
            context + ": invalid Ciliocyte queue range");
    for (int interaction = 0;
            interaction < cells.mCiliocyteInteractionCount;
            interaction++) {
        const int first = cells.mCiliocyteInteractionCellIndex1[interaction];
        const int second = cells.mCiliocyteInteractionCellIndex2[interaction];
        require(first >= 0 && first < cells.mCellCount
                        && (second == CellData::BOUNDARY_CONNECTION_INDEX
                                || (second >= 0 && second < cells.mCellCount)),
                context + ": invalid Ciliocyte queue endpoint");
    }

    requireFiniteVector(world.mFoodData.mPositionX,
            world.mFoodData.mFoodCount,
            context + ".food.positionX");
    requireFiniteVector(world.mFoodData.mPositionY,
            world.mFoodData.mFoodCount,
            context + ".food.positionY");
    requireFiniteVector(world.mFoodData.mMass, world.mFoodData.mFoodCount,
            context + ".food.mass");
    requireFiniteVector(world.mFoodData.mVelocityX,
            world.mFoodData.mFoodCount,
            context + ".food.velocityX");
    requireFiniteVector(world.mFoodData.mVelocityY,
            world.mFoodData.mFoodCount,
            context + ".food.velocityY");
    requireFiniteVector(world.mFoodData.mLipidLayer,
            world.mFoodData.mFoodCount,
            context + ".food.lipidLayer");
    for (int food = 0; food < world.mFoodData.mFoodCount; food++) {
        require(world.mFoodData.mMass[food] > 0.0f,
                context + ": retained Food has non-positive mass");
    }
    requireFiniteVector(world.mDeadSplashData.mPositionX,
            world.mDeadSplashData.mDeadSplashCount,
            context + ".splash.positionX");
    requireFiniteVector(world.mDeadSplashData.mPositionY,
            world.mDeadSplashData.mDeadSplashCount,
            context + ".splash.positionY");
    requireFiniteVector(world.mDeadSplashData.mRadius,
            world.mDeadSplashData.mDeadSplashCount,
            context + ".splash.radius");
    requireFiniteVector(world.mDeadSplashData.mAge,
            world.mDeadSplashData.mDeadSplashCount,
            context + ".splash.age");
    for (int splash = 0; splash < world.mDeadSplashData.mDeadSplashCount;
            splash++) {
        for (float value : world.mDeadSplashData.mColor[splash]) {
            require(std::isfinite(value),
                    context + ": non-finite dead-splash color");
        }
    }
}

bool channelEqual(const TraceChannelResult& reference,
        const TraceChannelResult& candidate) {
    return reference.eventCount == candidate.eventCount
            && reference.digest == candidate.digest
            && reference.capturedEventDigests
            == candidate.capturedEventDigests
            && reference.diagnosticWindowTruncated
            == candidate.diagnosticWindowTruncated;
}

void compareDiscreteTrace(const SemanticTraceResult& reference,
        const SemanticTraceResult& candidate,
        const std::string& context) {
    require(channelEqual(reference.sweep, candidate.sweep),
            context + ": discrete sweep trace changed");
    require(channelEqual(reference.endpoint, candidate.endpoint),
            context + ": discrete endpoint trace changed");
    require(channelEqual(reference.apply, candidate.apply),
            context + ": discrete apply/RNG/topology trace changed");
}

SemanticTraceResult updateWithDiscreteTrace(SimulationWorld& world,
        std::int64_t& globalSeed) {
    SimulationRandom::global.setSeed(globalSeed);
    SemanticTraceSink sink(SemanticTraceMode::DiscreteOnly);
    {
        ScopedSemanticTrace trace(sink);
        world.update(false, 1.0);
    }
    globalSeed = SimulationRandom::global.getSeed();
    return sink.finish();
}

void updateWithoutTrace(SimulationWorld& world, std::int64_t& globalSeed) {
    SimulationRandom::global.setSeed(globalSeed);
    world.update(false, 1.0);
    globalSeed = SimulationRandom::global.getSeed();
}

void compareSourceFrame(const render::RenderSourceFrame& reference,
        const render::RenderSourceFrame& candidate,
        const NumericalTolerance& tolerance,
        const std::string& context) {
    require(reference.valid && candidate.valid,
            context + ": source frame is not valid");
    require(reference.layoutVersion == candidate.layoutVersion
                    && reference.captureSequence == candidate.captureSequence
                    && reference.capture.completedUpdateId
                            == candidate.capture.completedUpdateId
                    && reference.capture.stage == candidate.capture.stage
                    && reference.sourceCellCount == candidate.sourceCellCount
                    && reference.sourceFoodCount == candidate.sourceFoodCount
                    && reference.sourceDeadSplashCount
                            == candidate.sourceDeadSplashCount
                    && reference.sourceTopologyEdgeCount
                            == candidate.sourceTopologyEdgeCount
                    && reference.sourceBoundaryLinkCount
                            == candidate.sourceBoundaryLinkCount
                    && reference.sourceFoodCapacity
                            == candidate.sourceFoodCapacity
                    && reference.sourceDeadSplashCapacity
                            == candidate.sourceDeadSplashCapacity
                    && reference.visibleCellCount == candidate.visibleCellCount
                    && reference.orderedCellRecordIndices
                            == candidate.orderedCellRecordIndices,
            context + ": source-frame discrete layout changed");
    require(reference.cells.size() == candidate.cells.size()
                    && reference.activeContactChords.size()
                            == candidate.activeContactChords.size()
                    && reference.activeContactChordNeighborRuntimeIds
                            == candidate.activeContactChordNeighborRuntimeIds
                    && reference.candidateLinks.size()
                            == candidate.candidateLinks.size()
                    && reference.foods.size() == candidate.foods.size()
                    && reference.deadSplashes.size()
                            == candidate.deadSplashes.size()
                    && reference.markers.size() == candidate.markers.size(),
            context + ": source-frame active ranges changed");
    require(reference.environment.revision == candidate.environment.revision
                    && reference.environment.previewMode
                            == candidate.environment.previewMode,
            context + ": source-frame preview mode changed");
    requireNear(reference.environment.lightAngle,
            candidate.environment.lightAngle, tolerance,
            context + ".environment.lightAngle");
    requireNear(reference.environment.lightRange,
            candidate.environment.lightRange, tolerance,
            context + ".environment.lightRange");
    requireNear(reference.environment.lightIntensity,
            candidate.environment.lightIntensity, tolerance,
            context + ".environment.lightIntensity");
    requireNear(reference.environment.dishRadius,
            candidate.environment.dishRadius, tolerance,
            context + ".environment.dishRadius");
    for (std::size_t channel = 0;
            channel < reference.environment.backgroundColor.size(); channel++) {
        requireNear(reference.environment.backgroundColor[channel],
                candidate.environment.backgroundColor[channel], tolerance,
                context + ".environment.backgroundColor["
                        + std::to_string(channel) + ']');
    }

    for (std::size_t index = 0; index < reference.cells.size(); index++) {
        const render::RenderSourceCell& left = reference.cells[index];
        const render::RenderSourceCell& right = candidate.cells[index];
        const std::string field = context + ".cell[" + std::to_string(index) + ']';
        require(left.runtimeId == right.runtimeId
                        && left.sourceCellIndex == right.sourceCellIndex
                        && left.cellType == right.cellType
                        && left.contactChordOffset == right.contactChordOffset
                        && left.contactChordCount == right.contactChordCount
                        && left.hasContactChordPayload
                                == right.hasContactChordPayload
                        && left.mirrored == right.mirrored
                        && left.previewArrow == right.previewArrow,
                field + ": discrete render Cell changed");
        const std::array<std::pair<float, float>, 15> values{{
                {left.positionX, right.positionX},
                {left.positionY, right.positionY},
                {left.radius, right.radius},
                {left.angle, right.angle},
                {left.red, right.red},
                {left.green, right.green},
                {left.blue, right.blue},
                {left.baseAux, right.baseAux},
                {left.baseAux4, right.baseAux4},
                {left.previewAux, right.previewAux},
                {left.previewAux4, right.previewAux4},
                {left.outlineAux1, right.outlineAux1},
                {left.outlineAux2, right.outlineAux2},
                {left.outlineAux3, right.outlineAux3},
                {left.outlineAnimation, right.outlineAnimation},
        }};
        for (std::size_t value = 0; value < values.size(); value++) {
            requireNear(values[value].first, values[value].second, tolerance,
                    field + ".value[" + std::to_string(value) + ']');
        }
    }
    for (std::size_t index = 0;
            index < reference.activeContactChords.size();
            index++) {
        const RenderContactChord& left = reference.activeContactChords[index];
        const RenderContactChord& right = candidate.activeContactChords[index];
        const std::string field = context + ".chord[" + std::to_string(index) + ']';
        require(left.isDiscarded == right.isDiscarded
                        && left.joinsNextChord == right.joinsNextChord
                        && left.neighborCellIndex == right.neighborCellIndex,
                field + ": discrete contact chord changed");
        requireNear(left.x1, right.x1, tolerance, field + ".x1");
        requireNear(left.y1, right.y1, tolerance, field + ".y1");
        requireNear(left.x2, right.x2, tolerance, field + ".x2");
        requireNear(left.y2, right.y2, tolerance, field + ".y2");
    }
    for (std::size_t index = 0; index < reference.candidateLinks.size(); index++) {
        const render::RenderSourceLink& left = reference.candidateLinks[index];
        const render::RenderSourceLink& right = candidate.candidateLinks[index];
        const std::string field = context + ".link[" + std::to_string(index) + ']';
        require(left.runtimeId == right.runtimeId
                        && left.cellARuntimeId == right.cellARuntimeId
                        && left.cellBRuntimeId == right.cellBRuntimeId
                        && left.sourceLinkIndex == right.sourceLinkIndex
                        && left.sourceCellBIndex == right.sourceCellBIndex
                        && left.sourceCellARecordIndex == right.sourceCellARecordIndex,
                field + ": discrete render Link changed");
        const std::array<std::pair<float, float>, 9> values{{
                {left.cellAPositionX, right.cellAPositionX},
                {left.cellAPositionY, right.cellAPositionY},
                {left.cellBPositionX, right.cellBPositionX},
                {left.cellBPositionY, right.cellBPositionY},
                {left.fallbackCenterRatio, right.fallbackCenterRatio},
                {left.renderStartOffsetX, right.renderStartOffsetX},
                {left.renderStartOffsetY, right.renderStartOffsetY},
                {left.renderEndOffsetX, right.renderEndOffsetX},
                {left.renderEndOffsetY, right.renderEndOffsetY},
        }};
        for (std::size_t value = 0; value < values.size(); value++) {
            requireNear(values[value].first, values[value].second, tolerance,
                    field + ".value[" + std::to_string(value) + ']');
        }
    }
    for (std::size_t index = 0; index < reference.foods.size(); index++) {
        const auto& left = reference.foods[index];
        const auto& right = candidate.foods[index];
        require(left.runtimeId == right.runtimeId,
                context + ": render Food identity changed at "
                        + std::to_string(index));
        for (std::size_t value = 0; value < left.instance.size(); value++) {
            requireNear(left.instance[value], right.instance[value], tolerance,
                    context + ".food[" + std::to_string(index)
                            + "].instance[" + std::to_string(value) + ']');
            requireNear(left.vertex[value], right.vertex[value], tolerance,
                    context + ".food[" + std::to_string(index)
                            + "].vertex[" + std::to_string(value) + ']');
        }
    }
    for (std::size_t index = 0; index < reference.deadSplashes.size(); index++) {
        const auto& left = reference.deadSplashes[index];
        const auto& right = candidate.deadSplashes[index];
        require(left.runtimeId == right.runtimeId,
                context + ": render dead-splash identity changed at "
                        + std::to_string(index));
        for (std::size_t value = 0; value < left.instance.size(); value++) {
            requireNear(left.instance[value], right.instance[value], tolerance,
                    context + ".splash[" + std::to_string(index)
                            + "].instance[" + std::to_string(value) + ']');
            requireNear(left.vertex[value], right.vertex[value], tolerance,
                    context + ".splash[" + std::to_string(index)
                            + "].vertex[" + std::to_string(value) + ']');
        }
    }
    for (std::size_t index = 0; index < reference.markers.size(); index++) {
        const render::RenderMarker& left = reference.markers[index];
        const render::RenderMarker& right = candidate.markers[index];
        require(left.selectedCellRuntimeId == right.selectedCellRuntimeId,
                context + ": render marker identity changed at "
                        + std::to_string(index));
        requireNear(left.x, right.x, tolerance,
                context + ".marker[" + std::to_string(index) + "].x");
        requireNear(left.y, right.y, tolerance,
                context + ".marker[" + std::to_string(index) + "].y");
        requireNear(left.radius, right.radius, tolerance,
                context + ".marker[" + std::to_string(index)
                        + "].radius");
        requireNear(left.alpha, right.alpha, tolerance,
                context + ".marker[" + std::to_string(index)
                        + "].alpha");
    }
}

void captureAndCompare(SimulationWorld& reference,
        SimulationWorld& candidate,
        render::RenderSourceCapture& referenceCapture,
        render::RenderSourceCapture& candidateCapture,
        render::RenderSourceFrame& referenceFrame,
        render::RenderSourceFrame& candidateFrame,
        std::uint64_t completedUpdate,
        const NumericalTolerance& tolerance,
        const std::string& context) {
    const float radius = static_cast<float>(
            std::max(reference.mEnvironment.mRadius,
                    candidate.mEnvironment.mRadius)
            * 1.1);
    const render::RenderViewBounds bounds{-radius, radius, -radius, radius};
    render::RenderFrameCaptureMetadata metadata;
    metadata.completedUpdateId = completedUpdate;
    metadata.stage = render::RenderCaptureStage::AfterCompletedUpdate;
    metadata.viewport.visibleBounds = bounds;
    metadata.viewport.coverageBounds = bounds;
    referenceCapture.capture(reference, bounds, metadata, referenceFrame);
    candidateCapture.capture(candidate, bounds, metadata, candidateFrame);
    compareSourceFrame(referenceFrame, candidateFrame, tolerance, context);
}

} // namespace

void verifyRenderSourceLinkComparison() {
    render::RenderSourceFrame reference;
    render::RenderSourceFrame candidate;
    reference.valid = candidate.valid = true;
    render::RenderSourceLink link;
    link.runtimeId = 41;
    link.cellARuntimeId = 3;
    link.cellBRuntimeId = 4;
    link.sourceLinkIndex = 7;
    link.sourceCellBIndex = 0;
    link.sourceCellARecordIndex = 6;
    link.cellAPositionX = 0.1f;
    link.cellAPositionY = 0.2f;
    link.cellBPositionX = 0.3f;
    link.cellBPositionY = 0.4f;
    link.fallbackCenterRatio = 4.25f;
    link.renderStartOffsetX = 0.01f;
    link.renderStartOffsetY = 0.02f;
    link.renderEndOffsetX = 0.03f;
    link.renderEndOffsetY = 0.04f;
    reference.candidateLinks.push_back(link);
    candidate.candidateLinks.push_back(link);
    const NumericalTolerance exact{};
    compareSourceFrame(reference, candidate, exact, "Link DTO identical normal record");

    auto rejectsMutation = [&](auto field, const char* name) {
        candidate.candidateLinks[0] = link;
        candidate.candidateLinks[0].*field += 1;
        bool rejected = false;
        try {
            compareSourceFrame(reference, candidate, exact, name);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, std::string("Link DTO comparator ignored ") + name);
    };
    rejectsMutation(&render::RenderSourceLink::runtimeId, "runtimeId");
    rejectsMutation(&render::RenderSourceLink::cellARuntimeId, "cellARuntimeId");
    rejectsMutation(&render::RenderSourceLink::cellBRuntimeId, "cellBRuntimeId");
    rejectsMutation(&render::RenderSourceLink::sourceLinkIndex, "sourceLinkIndex");
    rejectsMutation(&render::RenderSourceLink::sourceCellBIndex, "sourceCellBIndex");
    rejectsMutation(&render::RenderSourceLink::sourceCellARecordIndex, "sourceCellARecordIndex");
    rejectsMutation(&render::RenderSourceLink::cellAPositionX, "cellAPositionX");
    rejectsMutation(&render::RenderSourceLink::cellAPositionY, "cellAPositionY");
    rejectsMutation(&render::RenderSourceLink::cellBPositionX, "cellBPositionX");
    rejectsMutation(&render::RenderSourceLink::cellBPositionY, "cellBPositionY");
    rejectsMutation(&render::RenderSourceLink::fallbackCenterRatio, "fallbackCenterRatio");
    rejectsMutation(&render::RenderSourceLink::renderStartOffsetX, "renderStartOffsetX");
    rejectsMutation(&render::RenderSourceLink::renderStartOffsetY, "renderStartOffsetY");
    rejectsMutation(&render::RenderSourceLink::renderEndOffsetX, "renderEndOffsetX");
    rejectsMutation(&render::RenderSourceLink::renderEndOffsetY, "renderEndOffsetY");

    link.runtimeId = kInvalidTopologyEdgeRuntimeId;
    link.cellBRuntimeId = kBoundaryCellRuntimeId;
    link.sourceCellBIndex = CellData::BOUNDARY_CONNECTION_INDEX;
    link.fallbackCenterRatio = 0.5f;
    link.renderEndOffsetX = link.renderEndOffsetY = 0.0f;
    reference.sourceBoundaryLinkCount = candidate.sourceBoundaryLinkCount = 1;
    reference.candidateLinks[0] = candidate.candidateLinks[0] = link;
    compareSourceFrame(reference, candidate, exact, "Link DTO identical boundary record");
    candidate.sourceBoundaryLinkCount = 2;
    bool boundaryCountRejected = false;
    try {
        compareSourceFrame(reference, candidate, exact, "Boundary topology count changed");
    } catch (const std::runtime_error&) {
        boundaryCountRejected = true;
    }
    require(boundaryCountRejected, "Source comparator ignored the separate boundary topology count");
    candidate.sourceBoundaryLinkCount = reference.sourceBoundaryLinkCount;
    rejectsMutation(&render::RenderSourceLink::sourceLinkIndex, "boundary sourceLinkIndex");
    rejectsMutation(&render::RenderSourceLink::sourceCellBIndex, "boundary sourceCellBIndex");
    rejectsMutation(&render::RenderSourceLink::cellBPositionX, "boundary anchor X");
    rejectsMutation(&render::RenderSourceLink::cellBPositionY, "boundary anchor Y");
}

void verifyDiscreteTraceProjection() {
    auto emit = [](SemanticTraceMode mode, float numeric, std::int32_t discrete) {
        SemanticTraceSink sink(mode);
        {
            ScopedSemanticTrace trace(sink);
            verification::emitSemanticTrace(
                    TraceChannel::Apply,
                    SemanticEventKind::ApplyIntentResult,
                    {verification::traceI32(discrete),
                            verification::traceF32(numeric),
                            verification::traceF64(static_cast<double>(numeric))});
        }
        return sink.finish().apply;
    };

    const float adjacent = std::nextafter(1.0f, 2.0f);
    const TraceChannelResult exactA = emit(SemanticTraceMode::Exact, 1.0f, 7);
    const TraceChannelResult exactB = emit(SemanticTraceMode::Exact, adjacent, 7);
    require(exactA.digest != exactB.digest,
            "Level 1 exact trace must retain floating-point bit changes");

    const TraceChannelResult discreteA =
            emit(SemanticTraceMode::DiscreteOnly, 1.0f, 7);
    const TraceChannelResult discreteB =
            emit(SemanticTraceMode::DiscreteOnly, adjacent, 7);
    const TraceChannelResult discreteC =
            emit(SemanticTraceMode::DiscreteOnly, 1.0f, 8);
    require(channelEqual(discreteA, discreteB),
            "Level 2 discrete trace must project out floating-point fields");
    require(discreteA.digest != discreteC.digest,
            "Level 2 discrete trace must retain integer fields");
}

void verifyCandidate(const std::string& candidateName,
        const WorldFactory& factory,
        const WorldConfigurator& configureReference,
        const WorldConfigurator& configureCandidate,
        const ContractConfiguration& configuration) {
    require(!candidateName.empty(), "Level 2 candidate must have a name");
    require(configuration.shortHorizonUpdates > 0,
            candidateName + ": short horizon must be positive");
    require(configuration.longRunUpdates > 0,
            candidateName + ": long run must be positive");
    require(configuration.visualCheckpointInterval > 0,
            candidateName + ": visual checkpoint interval must be positive");
    require(configuration.doubleTolerance.absolute >= 0.0
                    && configuration.doubleTolerance.relative >= 0.0
                    && configuration.floatTolerance.absolute >= 0.0
                    && configuration.floatTolerance.relative >= 0.0
                    && configuration.visualTolerance.absolute >= 0.0
                    && configuration.visualTolerance.relative >= 0.0,
            candidateName + ": numerical tolerances must be non-negative");

    SimulationRandom::global.setSeed(kInitialGlobalRandomSeed);
    std::unique_ptr<SimulationWorld> reference = factory();
    std::int64_t referenceGlobalSeed = SimulationRandom::global.getSeed();
    SimulationRandom::global.setSeed(kInitialGlobalRandomSeed);
    std::unique_ptr<SimulationWorld> candidate = factory();
    std::int64_t candidateGlobalSeed = SimulationRandom::global.getSeed();
    require(reference != nullptr && candidate != nullptr,
            candidateName + ": world factory returned null");

    configureReference(*reference);
    configureCandidate(*candidate);

    render::RenderSourceFrame referenceFrame;
    render::RenderSourceFrame candidateFrame;
    render::RenderSourceCapture referenceCapture(
            reference->mCellData.MAX_CELL_COUNT, referenceFrame);
    render::RenderSourceCapture candidateCapture(
            candidate->mCellData.MAX_CELL_COUNT, candidateFrame);

    compareDiscreteState(*reference, *candidate, referenceGlobalSeed,
            candidateGlobalSeed, candidateName + ".initial");
    compareNumericalState(*reference, *candidate, configuration,
            candidateName + ".initial");
    verifyWorldInvariants(*reference, candidateName + ".reference.initial");
    verifyWorldInvariants(*candidate, candidateName + ".candidate.initial");
    captureAndCompare(*reference, *candidate, referenceCapture,
            candidateCapture, referenceFrame, candidateFrame, 0,
            configuration.visualTolerance,
            candidateName + ".visual.initial");

    std::uint64_t completedUpdates = 0;
    for (int update = 0; update < configuration.shortHorizonUpdates; update++) {
        const SemanticTraceResult referenceTrace =
                updateWithDiscreteTrace(*reference, referenceGlobalSeed);
        const SemanticTraceResult candidateTrace =
                updateWithDiscreteTrace(*candidate, candidateGlobalSeed);
        completedUpdates++;
        const std::string context = candidateName + ".short["
                + std::to_string(update) + ']';
        if (update == 0) {
            require(referenceTrace.sweep.eventCount
                                    + referenceTrace.endpoint.eventCount
                                    + referenceTrace.apply.eventCount
                            > 0,
                    candidateName
                            + ": traced world update emitted no semantic events");
        }
        compareDiscreteTrace(referenceTrace, candidateTrace, context);
        compareDiscreteState(*reference, *candidate, referenceGlobalSeed,
                candidateGlobalSeed, context);
        compareNumericalState(*reference, *candidate, configuration, context);
        verifyWorldInvariants(*reference, context + ".reference");
        verifyWorldInvariants(*candidate, context + ".candidate");
    }
    captureAndCompare(*reference, *candidate, referenceCapture,
            candidateCapture, referenceFrame, candidateFrame,
            completedUpdates, configuration.visualTolerance,
            candidateName + ".visual.short");

    for (int update = 0; update < configuration.longRunUpdates; update++) {
        updateWithoutTrace(*reference, referenceGlobalSeed);
        updateWithoutTrace(*candidate, candidateGlobalSeed);
        completedUpdates++;
        const std::string context = candidateName + ".long["
                + std::to_string(update) + ']';
        compareDiscreteState(*reference, *candidate, referenceGlobalSeed,
                candidateGlobalSeed, context);
        verifyWorldInvariants(*reference, context + ".reference");
        verifyWorldInvariants(*candidate, context + ".candidate");
        if ((update + 1) % configuration.visualCheckpointInterval == 0
                || update + 1 == configuration.longRunUpdates) {
            captureAndCompare(*reference, *candidate, referenceCapture,
                    candidateCapture, referenceFrame, candidateFrame,
                    completedUpdates, configuration.visualTolerance,
                    context + ".visual");
        }
    }
}

} // namespace cellularity::semantic_tests::level2
