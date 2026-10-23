#include "core/Environment.h"

#include <algorithm>
#include <cmath>

Environment::Environment() {
    mGenePool.fill(false);
    resetLegacyMutationWeights();
    mLightAngleCos = std::cos(mLightAngle);
    mLightAngleSin = std::sin(mLightAngle);
}

void Environment::set(const Environment& environment) {
    mRandom = environment.mRandom;
    mIsPreviewMode = environment.mIsPreviewMode;
    mLightAngle = environment.mLightAngle;
    mLightAngleCos = environment.mLightAngleCos;
    mLightAngleSin = environment.mLightAngleSin;
    mFoodSpeed = environment.mFoodSpeed;
    mFoodSize = environment.mFoodSize;
    mRadiation = environment.mRadiation;
    mLightIntensity = environment.mLightIntensity;
    mLightSpeed = environment.mLightSpeed;
    mViscosity = environment.mViscosity;
    mLightRange = environment.mLightRange;
    mGenePool = environment.mGenePool;
    addRandom = environment.addRandom;
    mGravity = environment.mGravity;
    mDensity = environment.mDensity;
    mDensityGradient = environment.mDensityGradient;
    mGravityFieldType = environment.mGravityFieldType;
    mGravityFieldStrength = environment.mGravityFieldStrength;
    mGravityFieldAngle = environment.mGravityFieldAngle;
    mDensityFieldType = environment.mDensityFieldType;
    mDensityFieldBase = environment.mDensityFieldBase;
    mDensityFieldStrength = environment.mDensityFieldStrength;
    mDensityFieldAngle = environment.mDensityFieldAngle;
    mDensityFieldOffset = environment.mDensityFieldOffset;
    mDensityFieldSoftness = environment.mDensityFieldSoftness;
    mSalinityFieldType = environment.mSalinityFieldType;
    mSalinityFieldBase = environment.mSalinityFieldBase;
    mSalinityFieldStrength = environment.mSalinityFieldStrength;
    mSalinityFieldAngle = environment.mSalinityFieldAngle;
    mSalinityFieldOffset = environment.mSalinityFieldOffset;
    mSalinityFieldSoftness = environment.mSalinityFieldSoftness;
    mFoodLipidLayerFieldType = environment.mFoodLipidLayerFieldType;
    mFoodLipidLayerFieldBase = environment.mFoodLipidLayerFieldBase;
    mFoodLipidLayerFieldStrength = environment.mFoodLipidLayerFieldStrength;
    mFoodLipidLayerFieldAngle = environment.mFoodLipidLayerFieldAngle;
    mFoodLipidLayerFieldOffset = environment.mFoodLipidLayerFieldOffset;
    mFoodLipidLayerFieldSoftness = environment.mFoodLipidLayerFieldSoftness;
    dieEdge = environment.dieEdge;
    mNitrates = environment.mNitrates;
    maxCellCount = environment.maxCellCount;
    maxFoodCount = environment.maxFoodCount;
    mRadius = environment.mRadius;
    mInnerRadius = environment.mInnerRadius;
    mFrictionDynamic = environment.mFrictionDynamic;
    mFrictionStatic = environment.mFrictionStatic;
    IsOnlyTweakMutations = environment.IsOnlyTweakMutations;
    mutationOnlyAfterSplit = environment.mutationOnlyAfterSplit;
    mSalinity = environment.mSalinity;
    aging = environment.aging;
    mFoodLumpiness = environment.mFoodLumpiness;
    mFoodLumpSize = environment.mFoodLumpSize;
    mobileFood = environment.mobileFood;
    mFoodLipidLayer = environment.mFoodLipidLayer;
    mTestMulticellularCoef = environment.mTestMulticellularCoef;
    mTestMutationCoef = environment.mTestMutationCoef;
    mTestFoodDecay = environment.mTestFoodDecay;
    mTestIdleMetabolismScale = environment.mTestIdleMetabolismScale;
    mTestFlagellocyteCostScale = environment.mTestFlagellocyteCostScale;
    mTestPhagocyteEatSpeedScale = environment.mTestPhagocyteEatSpeedScale;
    mMutationWeights = environment.mMutationWeights;
}

void Environment::resetLegacyMutationWeights() {
    mMutationWeights.fill(1.0);
    double structuralWeight = static_cast<double>(MUTATION_POINT_WEIGHT_COUNT)
            / static_cast<double>(MUTATION_STRUCTURAL_WEIGHT_COUNT);
    for (int i = 0; i < MUTATION_STRUCTURAL_WEIGHT_COUNT; i++) {
        mMutationWeights[MUTATION_STRUCTURAL_WEIGHT_OFFSET + i] = structuralWeight;
    }
}

void Environment::syncGravityFieldFromLegacyGravity() {
    mGravityFieldType = VECTOR_FIELD_LINEAR_DIRECTIONAL;
    mGravityFieldStrength = mGravity;
    mGravityFieldAngle = -PI / 2.0;
}

void Environment::syncLegacyGravityFromVectorField() {
    mGravity = mGravityFieldStrength;
}

void Environment::getGravityVector(double positionX, double positionY, double& gravityX, double& gravityY) const {
    switch (mGravityFieldType) {
        case VECTOR_FIELD_RADIAL_INWARD:
        case VECTOR_FIELD_RADIAL_OUTWARD: {
            double length = std::sqrt(positionX * positionX + positionY * positionY);
            if (length <= 0.0000001) {
                gravityX = 0.0;
                gravityY = 0.0;
                return;
            }
            double direction = mGravityFieldType == VECTOR_FIELD_RADIAL_INWARD ? -1.0 : 1.0;
            gravityX = direction * positionX / length * mGravityFieldStrength;
            gravityY = direction * positionY / length * mGravityFieldStrength;
            return;
        }
        case VECTOR_FIELD_LINEAR_DIRECTIONAL:
        default:
            gravityX = std::cos(mGravityFieldAngle) * mGravityFieldStrength;
            gravityY = std::sin(mGravityFieldAngle) * mGravityFieldStrength;
            return;
    }
}

void Environment::syncDensityFieldFromLegacyGradient() {
    mDensityFieldType = DENSITY_FIELD_LINEAR_DIRECTIONAL;
    mDensityFieldBase = mDensity;
    mDensityFieldStrength = mDensityGradient;
    mDensityFieldAngle = PI / 2.0;
    mDensityFieldOffset = 0.0;
    mDensityFieldSoftness = 0.0;
}

void Environment::syncLegacyGradientFromDensityField() {
    mDensity = mDensityFieldBase;
    if (mDensityFieldType == DENSITY_FIELD_LINEAR_DIRECTIONAL) {
        mDensityGradient = mDensityFieldStrength;
    }
}

double Environment::getDensity(double positionX, double positionY) const {
    return evaluateScalarField(mDensityFieldType,
            mDensityFieldBase,
            mDensityFieldStrength,
            mDensityFieldAngle,
            mDensityFieldOffset,
            mDensityFieldSoftness,
            positionX,
            positionY);
}

void Environment::syncSalinityFieldFromLegacyValue() {
    mSalinityFieldType = DENSITY_FIELD_CONSTANT;
    mSalinityFieldBase = mSalinity;
    mSalinityFieldStrength = 0.0;
    mSalinityFieldAngle = PI / 2.0;
    mSalinityFieldOffset = 0.0;
    mSalinityFieldSoftness = 0.0;
}

void Environment::syncLegacySalinityFromField() {
    mSalinity = static_cast<float>(std::max(0.0, std::min(1.0, mSalinityFieldBase)));
}

double Environment::getSalinity(double positionX, double positionY) const {
    double value = evaluateScalarField(mSalinityFieldType,
            mSalinityFieldBase,
            mSalinityFieldStrength,
            mSalinityFieldAngle,
            mSalinityFieldOffset,
            mSalinityFieldSoftness,
            positionX,
            positionY);
    return std::max(0.0, std::min(1.0, value));
}

void Environment::syncFoodLipidLayerFieldFromLegacyValue() {
    mFoodLipidLayerFieldType = DENSITY_FIELD_CONSTANT;
    mFoodLipidLayerFieldBase = mFoodLipidLayer;
    mFoodLipidLayerFieldStrength = 0.0;
    mFoodLipidLayerFieldAngle = PI / 2.0;
    mFoodLipidLayerFieldOffset = 0.0;
    mFoodLipidLayerFieldSoftness = 0.0;
}

void Environment::syncLegacyFoodLipidLayerFromField() {
    mFoodLipidLayer = static_cast<float>(std::max(0.0, std::min(1.0, mFoodLipidLayerFieldBase)));
}

double Environment::getFoodLipidLayer(double positionX, double positionY) const {
    double value = evaluateScalarField(mFoodLipidLayerFieldType,
            mFoodLipidLayerFieldBase,
            mFoodLipidLayerFieldStrength,
            mFoodLipidLayerFieldAngle,
            mFoodLipidLayerFieldOffset,
            mFoodLipidLayerFieldSoftness,
            positionX,
            positionY);
    return std::max(0.0, std::min(1.0, value));
}

double Environment::evaluateScalarField(int fieldType,
        double base,
        double strength,
        double angle,
        double offset,
        double softness,
        double positionX,
        double positionY) const {
    double radius = mRadius != 0.0 ? mRadius : 1.0;
    double normalizedX = positionX / radius;
    double normalizedY = positionY / radius;
    double coordinate;
    switch (fieldType) {
        case DENSITY_FIELD_CONSTANT:
            return base;
        case DENSITY_FIELD_LINEAR_DIRECTIONAL:
            coordinate = normalizedX * std::cos(angle) + normalizedY * std::sin(angle);
            break;
        case DENSITY_FIELD_RADIAL:
            coordinate = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY);
            break;
        default:
            coordinate = 0.0;
            break;
    }
    coordinate -= offset;
    double clampedSoftness = std::max(0.0, std::min(1.0, softness));
    coordinate = coordinate / (1.0 + clampedSoftness * std::abs(coordinate) * 2.0);
    return base + strength * coordinate;
}

double Environment::getLight(double positionX, double positionY) const {
    double py = (mLightAngleCos * positionX + mLightAngleSin * positionY) / mRadius;
    double distance = 1.0 + (1.0 - mLightRange) * (1.0 - std::sqrt(positionX * positionX + positionY * positionY) / mRadius) / mLightRange;
    double distanceSq = distance * distance;

    return std::max(mLightIntensity * (py * (1.0 - mLightRange) + mLightRange) / distanceSq, 0.0);
}

double Environment::getLightddx(double positionX, double positionY) const {
    double py = (mLightAngleCos * positionX + mLightAngleSin * positionY) / mRadius;
    double r = std::sqrt(positionX * positionX + positionY * positionY);
    double distance = 1.0 + (1.0 - mLightRange) * (1.0 - r / mRadius) / mLightRange;
    double distanceSq = distance * distance;

    if (mLightIntensity * (py * (1.0 - mLightRange) + mLightRange) / distanceSq < 0.0) {
        return 0.0;
    }
    return -((mLightIntensity * (-1.0 + mLightRange) * mLightRange * mLightRange * mRadius
                     * ((mLightAngleCos - mLightAngleCos * mLightRange) * r * r + mLightAngleCos * r * mRadius
                             + 2.0 * mLightRange * mRadius * positionX
                             - 2.0 * (-1.0 + mLightRange) * mLightAngleSin * positionX * positionY
                             + 2.0 * mLightAngleCos * (-1.0 + mLightRange) * positionY * positionY))
            / (r * std::pow((-1.0 + mLightRange) * r + mRadius, 3.0)));
}

double Environment::getLightddy(double positionX, double positionY) const {
    double py = (mLightAngleCos * positionX + mLightAngleSin * positionY) / mRadius;
    double r = std::sqrt(positionX * positionX + positionY * positionY);
    double distance = 1.0 + (1.0 - mLightRange) * (1.0 - r / mRadius) / mLightRange;
    double distanceSq = distance * distance;

    if (mLightIntensity * (py * (1.0 - mLightRange) + mLightRange) / distanceSq < 0.0) {
        return 0.0;
    }
    return -((mLightIntensity * (-1.0 + mLightRange) * mLightRange * mLightRange * mRadius
                     * ((-1.0 + mLightRange) * r * r * mLightAngleSin + r * mRadius * mLightAngleSin
                             + 2.0 * positionY * (mLightAngleCos * positionX + mLightAngleSin * positionY + mLightRange * (mRadius - mLightAngleCos * positionX - mLightAngleSin * positionY))))
            / (r * std::pow((-1.0 + mLightRange) * r + mRadius, 3.0)));
}

void Environment::updateLight(double deltaTime) {
    mLightAngle += mLightSpeed * deltaTime;
    mLightAngleCos = std::cos(mLightAngle);
    mLightAngleSin = std::sin(mLightAngle);
}

Environment Environment::constructDefaultEnvironment(int maxCellCount, int maxFoodCount, double radius) {
    Environment environment;
    environment.mViscosity = Environment::VISCOSITY_MAX * 0.4;
    environment.mFoodSpeed = Environment::MAX_FOOD_SPEED * 0.7;
    environment.mFoodSize = 0.25;
    environment.mFoodLumpiness = 0.0;
    environment.mFoodLumpSize = Environment::MIN_FOOD_LUMP_SIZE * 3.0;
    environment.mLightRange = 0.0;
    environment.mRadiation = 0.0;
    environment.mSalinity = 0.3f;
    environment.syncSalinityFieldFromLegacyValue();
    environment.mDensity = 0.0;
    environment.mGravity = 0.0;
    environment.syncGravityFieldFromLegacyGravity();
    environment.mDensityGradient = 0.0;
    environment.syncDensityFieldFromLegacyGradient();
    environment.syncFoodLipidLayerFieldFromLegacyValue();
    environment.mNitrates = 1.0;
    environment.addRandom = false;
    environment.dieEdge = false;
    environment.IsOnlyTweakMutations = false;
    environment.mutationOnlyAfterSplit = false;
    environment.aging = true;
    environment.maxCellCount = maxCellCount;
    environment.maxFoodCount = maxFoodCount;
    environment.mRadius = radius;
    environment.mGenePool.fill(false);
    environment.mGenePool[0] = true;
    environment.mGenePool[1] = true;
    return environment;
}

Environment Environment::setupPreviewSimulationEnvironment(int previewMaxShow) {
    Environment environment;
    environment.mRadiation = 0.0;
    environment.mFoodSpeed = 0.0;
    environment.mLightRange = 0.0;
    environment.mNitrates = 1.0;
    environment.mGravity = 0.0;
    environment.syncGravityFieldFromLegacyGravity();
    environment.syncDensityFieldFromLegacyGradient();
    environment.syncSalinityFieldFromLegacyValue();
    environment.syncFoodLipidLayerFieldFromLegacyValue();
    environment.dieEdge = true;
    environment.mViscosity = Environment::VISCOSITY_MAX * 0.5;
    environment.maxFoodCount = 0;
    environment.maxCellCount = previewMaxShow;
    environment.mRadius = 1.0;
    return environment;
}
