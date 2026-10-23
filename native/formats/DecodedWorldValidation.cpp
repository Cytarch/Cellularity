#include "formats/DecodedWorldValidation.h"

#include <array>
#include <cmath>
#include <string>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/GeneAccess.h"
#include "core/SimulationMemoryPolicy.h"
#include "formats/SnapshotCodecSupport.h"

namespace cellularity::snapshot_codec {

void validateDecodedGene(
        const GeneData& gene,
        const char* formatName,
        std::size_t geneCount) {
    const cellularity::GeneView view(gene);
    const auto requireGeneFinite = [formatName](double value,
                                           const char* fieldName) {
        if (!std::isfinite(value)) {
            failDecode(DecodeErrorCode::InvalidValue,
                    std::string(formatName)
                            + " Gene has a non-finite " + fieldName);
        }
    };
    for (int channel = 0; channel < 3; ++channel) {
        requireGeneFinite(view.color(channel), "color");
    }
    requireGeneFinite(view.splitMass(), "split mass");
    requireGeneFinite(view.splitRatio(), "split ratio");
    requireGeneFinite(view.splitAngle(), "split angle");
    requireGeneFinite(view.child1Angle(), "child angle");
    requireGeneFinite(view.child2Angle(), "child angle");
    requireGeneFinite(view.massFlowPriority(), "mass-flow priority");
    requireGeneFinite(view.linkStrength(), "link strength");
    if (!cellularity::isGeneIndexInBounds(view.child1GeneIndex(), geneCount)
            || !cellularity::isGeneIndexInBounds(
                    view.child2GeneIndex(), geneCount)) {
        failDecode(DecodeErrorCode::InvalidValue,
                std::string(formatName)
                        + " Gene has an invalid Gene index");
    }
    for (int integerIndex = 0;
            integerIndex < Gene::MAX_INTEGERS_COUNT;
            ++integerIndex) {
        const bool valid = integerIndex == Gene::VIRUS_FROM_INTEGER_INDEX
                        || integerIndex == Gene::VIRUS_TO_INTEGER_INDEX
                ? cellularity::geneIndexFromBoundary(
                          view.integerValue(integerIndex), geneCount)
                          .has_value()
                : view.integerValue(integerIndex) >= 0
                        && view.integerValue(integerIndex)
                                < Gene::INTEGER_MAX[integerIndex];
        if (!valid) {
            failDecode(DecodeErrorCode::InvalidValue,
                    std::string(formatName)
                            + " Gene has an invalid integer parameter");
        }
    }
    for (int index = 0;
            index < Gene::MAX_PARAMETRIC_VALUES_COUNT;
            ++index) {
        const cellularity::GeneParametricValueView value =
                view.parametricValue(index);
        requireGeneFinite(value.variableA(), "parametric value");
        requireGeneFinite(value.variableB(), "parametric value");
        requireGeneFinite(value.variableC(), "parametric value");
        if (value.signalChannel() < 0
                || value.signalChannel()
                        >= CellData::SIGNAL_SUBSTANCE_COUNT
                                + CellData::AUX_SIGNAL_SUBSTANCE_COUNT
                || value.formulaType() < 0 || value.formulaType() > 3) {
            failDecode(DecodeErrorCode::InvalidValue,
                    std::string(formatName)
                            + " Gene has an invalid parametric selector");
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
        requireGeneFinite(view.floatValue(type), "float parameter");
    }
}

void validateEnvironmentForAllocation(const Environment& environment) {
    if (environment.maxCellCount <= 0 || environment.maxFoodCount < 0
            || environment.maxDeadSplashCount < 0
            || !environment.geometry().isValid(CellData::MAX_CELL_RADIUS)) {
        failDecode(DecodeErrorCode::InvalidValue,
                "Invalid simulation allocation parameters");
    }
    cellularity::memory::validateWorldAllocation(environment);

    const std::array<double, 42> numericValues{
            environment.mLightAngle,
            environment.mLightAngleCos,
            environment.mLightAngleSin,
            environment.mFoodSpeed,
            environment.mFoodSize,
            environment.mRadiation,
            environment.mLightIntensity,
            environment.mLightSpeed,
            environment.mLightRange,
            environment.mViscosity,
            environment.mDensity,
            environment.mGravity,
            environment.mDensityGradient,
            environment.mRadius,
            environment.mTestMulticellularCoef,
            environment.mTestMutationCoef,
            environment.mTestFoodDecay,
            environment.mFrictionStatic,
            environment.mFrictionDynamic,
            environment.mSalinity,
            environment.mFoodLipidLayer,
            environment.mFoodLumpiness,
            environment.mFoodLumpSize,
            environment.mNitrates,
            environment.mGravityFieldStrength,
            environment.mGravityFieldAngle,
            environment.mDensityFieldBase,
            environment.mDensityFieldStrength,
            environment.mDensityFieldAngle,
            environment.mDensityFieldOffset,
            environment.mDensityFieldSoftness,
            environment.mSalinityFieldBase,
            environment.mSalinityFieldStrength,
            environment.mSalinityFieldAngle,
            environment.mSalinityFieldOffset,
            environment.mSalinityFieldSoftness,
            environment.mFoodLipidLayerFieldBase,
            environment.mFoodLipidLayerFieldStrength,
            environment.mFoodLipidLayerFieldAngle,
            environment.mFoodLipidLayerFieldOffset,
            environment.mFoodLipidLayerFieldSoftness,
            environment.mTestIdleMetabolismScale
    };
    for (double value : numericValues) {
        requireFinite(value, "Environment numeric value");
    }
    requireFinite(environment.mTestFlagellocyteCostScale,
            "Environment Flagellocyte cost scale");
    requireFinite(environment.mTestPhagocyteEatSpeedScale,
            "Environment Phagocyte eat-speed scale");
    for (double weight : environment.mMutationWeights) {
        requireFinite(weight, "Environment mutation weight");
    }
}

void validateDecodedWorldDynamicValues(const SimulationWorld& world) {
    const CellData& cells = world.mCellData;
    if (!cells.validateOrganismIdentities()) {
        failDecode(
                DecodeErrorCode::InvalidValue,
                "Cell organism identities are invalid");
    }
    for (int cellIndex = 0; cellIndex < cells.mCellCount; ++cellIndex) {
        const std::array<double, 22> values{
                cells.mPositionX[cellIndex],
                cells.mPositionY[cellIndex],
                cells.mPositionZ[cellIndex],
                cells.mRadius[cellIndex],
                cells.mVelocityX[cellIndex],
                cells.mVelocityY[cellIndex],
                cells.mForceX[cellIndex],
                cells.mForceY[cellIndex],
                cells.mForceZ[cellIndex],
                cells.mTorque[cellIndex],
                cells.mAngle[cellIndex],
                cells.mAngleCytoplasm[cellIndex],
                cells.mAngularVelocity[cellIndex],
                cells.mAngularVelocityCytoplasm[cellIndex],
                cells.mAge[cellIndex],
                cells.mNitrates[cellIndex],
                cells.mSwimAnimationTime[cellIndex],
                cells.mSwimAnimationTime2[cellIndex],
                cells.mHairRotation[cellIndex],
                cells.hairX[cellIndex],
                cells.hairY[cellIndex],
                cells.mMass[cellIndex]
        };
        for (double value : values) {
            requireFinite(value, "Cell snapshot value");
        }
        // Snapshot safety uses the bounding box, not a stricter disk test.
        // Preserve the accepted legacy inputs at the corners of that box.
        const auto geometry = world.mEnvironment.geometry();
        const auto bounds = geometry.bounds();
        if (cells.mRadius[cellIndex] <= 0.0
                || (geometry.isRing() && cells.mRadius[cellIndex] > CellData::MAX_CELL_RADIUS)
                || cells.mPositionX[cellIndex] < bounds.minX
                || cells.mPositionX[cellIndex] > bounds.maxX
                || cells.mPositionY[cellIndex] < bounds.minY
                || cells.mPositionY[cellIndex] > bounds.maxY) {
            failDecode(
                    DecodeErrorCode::InvalidValue,
                    "Cell geometry is outside safety bounds");
        }
        for (float value : cells.mColor[cellIndex]) {
            requireFinite(value, "Cell color");
        }
        for (float value : cells.mSignalSubstance[cellIndex]) {
            requireFinite(value, "Cell signal substance");
        }
        for (float value : cells.mNewSignalSubstance[cellIndex]) {
            requireFinite(value, "Cell new signal substance");
        }
        for (float value : cells.mSignalSubstanceOut[cellIndex]) {
            requireFinite(value, "Cell output signal substance");
        }
        for (float value : {
                     cells.mLipids[cellIndex],
                     cells.mToxin[cellIndex],
                     cells.mToxication[cellIndex],
                     cells.mNewToxin[cellIndex],
                     cells.mMyocyteContraction[cellIndex],
                     cells.mMyocyteBending[cellIndex],
                     cells.mMyocyteLifting[cellIndex]
             }) {
            requireFinite(value, "Cell scalar value");
        }
    }

    const FoodData& food = world.mFoodData;
    for (int foodIndex = 0; foodIndex < food.mFoodCount; ++foodIndex) {
        for (float value : {
                     food.mPositionX[foodIndex],
                     food.mPositionY[foodIndex],
                     food.mMass[foodIndex],
                     food.mVelocityX[foodIndex],
                     food.mVelocityY[foodIndex],
                     food.mLipidLayer[foodIndex]
             }) {
            requireFinite(value, "Food snapshot value");
        }
        const auto bounds = world.mEnvironment.geometry().bounds();
        if (food.mPositionX[foodIndex] < bounds.minX
                || food.mPositionX[foodIndex] > bounds.maxX
                || food.mPositionY[foodIndex] < bounds.minY
                || food.mPositionY[foodIndex] > bounds.maxY) {
            failDecode(
                    DecodeErrorCode::InvalidValue,
                    "Food geometry is outside the Environment safety bounds");
        }
    }

    const DeadSplashData& splashes = world.mDeadSplashData;
    for (int splashIndex = 0;
            splashIndex < splashes.mDeadSplashCount;
            ++splashIndex) {
        for (float value : {
                     splashes.mPositionX[splashIndex],
                     splashes.mPositionY[splashIndex],
                     splashes.mRadius[splashIndex],
                     splashes.mAge[splashIndex]
             }) {
            requireFinite(value, "Dead splash snapshot value");
        }
        for (float value : splashes.mColor[splashIndex]) {
            requireFinite(value, "Dead splash color");
        }
    }
}

void preserveEnvironmentRuntimeFields(
        SimulationWorld& world,
        const Environment& source) {
    world.mEnvironment.mLightAngle = source.mLightAngle;
    world.mEnvironment.mLightAngleCos = source.mLightAngleCos;
    world.mEnvironment.mLightAngleSin = source.mLightAngleSin;
    world.mEnvironment.maxDeadSplashCount = world.mEnvironment.maxCellCount / 4
            + (world.mEnvironment.maxCellCount % 4 != 0 ? 1 : 0);
}

} // namespace cellularity::snapshot_codec
