#pragma once

#include <array>

#include "core/CellData.h"
#include "core/SimulationRandom.h"
#include "core/geometry/SubstrateGeometry.h"

class Environment {
public:
    static constexpr double PI = 3.14159265358979323846;
    static constexpr double MAX_LIGHT = 2.0;
    static constexpr double MAX_LIGHT_RANGE = 1.0;
    static constexpr double MAX_LIGHT_SPEED = 1.0;
    static constexpr double MAX_RADIATION = 0.06 * 3.0 / 2.0;
    static constexpr double PREVIEW_SUN = 0.7;
    static constexpr double VISCOSITY_MAX = 1.0 * 150.0 * CellData::MAX_CELL_RADIUS / 0.03;
    static constexpr double MAX_FOOD_SIZE = 4.0 * (CellData::MAX_MASS - CellData::MIN_MASS);
    static constexpr double MIN_FOOD_SIZE = CellData::MIN_MASS * 0.3;
    static constexpr double MAX_FOOD_LUMPINESS = 100.0;
    static constexpr double MIN_FOOD_LUMP_SIZE = 0.01;
    static constexpr double MAX_LUMP_SIZE = 0.2;
    static constexpr double MAX_FOOD_SPEED = 0.15 * 100.0 * 0.03 * 0.03
            / (CellData::MAX_CELL_RADIUS * CellData::MAX_CELL_RADIUS);
    static constexpr double MAX_FRICTION = 8.0;
    static constexpr int CELL_TYPE_COUNT = 18;
    static constexpr int MUTATION_COLOR_WEIGHT_OFFSET = 0;
    static constexpr int MUTATION_COLOR_WEIGHT_COUNT = 3;
    static constexpr int MUTATION_CONTINUOUS_WEIGHT_OFFSET = MUTATION_COLOR_WEIGHT_OFFSET + MUTATION_COLOR_WEIGHT_COUNT;
    static constexpr int MUTATION_CONTINUOUS_WEIGHT_COUNT = 7;
    static constexpr int MUTATION_CHILD_INDEX_WEIGHT_OFFSET = MUTATION_CONTINUOUS_WEIGHT_OFFSET + MUTATION_CONTINUOUS_WEIGHT_COUNT;
    static constexpr int MUTATION_CHILD_INDEX_WEIGHT_COUNT = 2;
    static constexpr int MUTATION_BOOLEAN_WEIGHT_OFFSET = MUTATION_CHILD_INDEX_WEIGHT_OFFSET + MUTATION_CHILD_INDEX_WEIGHT_COUNT;
    static constexpr int MUTATION_BOOLEAN_WEIGHT_COUNT = 6;
    static constexpr int MUTATION_CELL_TYPE_WEIGHT_OFFSET = MUTATION_BOOLEAN_WEIGHT_OFFSET + MUTATION_BOOLEAN_WEIGHT_COUNT;
    static constexpr int MUTATION_CELL_TYPE_WEIGHT_COUNT = 1;
    static constexpr int MUTATION_PARAMETRIC_WEIGHT_OFFSET = MUTATION_CELL_TYPE_WEIGHT_OFFSET + MUTATION_CELL_TYPE_WEIGHT_COUNT;
    static constexpr int MUTATION_PARAMETRIC_WEIGHT_COUNT = Gene::MAX_PARAMETRIC_VALUES_COUNT * 5;
    static constexpr int MUTATION_INTEGER_WEIGHT_OFFSET = MUTATION_PARAMETRIC_WEIGHT_OFFSET + MUTATION_PARAMETRIC_WEIGHT_COUNT;
    static constexpr int MUTATION_INTEGER_WEIGHT_COUNT = Gene::MAX_INTEGERS_COUNT;
    static constexpr int MUTATION_FLOAT_VALUE_WEIGHT_OFFSET = MUTATION_INTEGER_WEIGHT_OFFSET + MUTATION_INTEGER_WEIGHT_COUNT;
    static constexpr int MUTATION_FLOAT_VALUE_WEIGHT_COUNT = Gene::MAX_FLOATS_COUNT;
    static constexpr int MUTATION_POINT_WEIGHT_COUNT = MUTATION_FLOAT_VALUE_WEIGHT_OFFSET + MUTATION_FLOAT_VALUE_WEIGHT_COUNT;
    static constexpr int MUTATION_STRUCTURAL_WEIGHT_OFFSET = MUTATION_POINT_WEIGHT_COUNT;
    static constexpr int MUTATION_STRUCTURAL_WEIGHT_COUNT = 6;
    static constexpr int MUTATION_GENOME_LENGTH_WEIGHT_OFFSET = MUTATION_STRUCTURAL_WEIGHT_OFFSET + MUTATION_STRUCTURAL_WEIGHT_COUNT;
    static constexpr int MUTATION_GENOME_LENGTH_WEIGHT_COUNT = 2;
    static constexpr int MUTATION_INSERT_GENE_WEIGHT_INDEX = MUTATION_GENOME_LENGTH_WEIGHT_OFFSET;
    static constexpr int MUTATION_DELETE_GENE_WEIGHT_INDEX = MUTATION_GENOME_LENGTH_WEIGHT_OFFSET + 1;
    static constexpr int MUTATION_WEIGHT_COUNT = MUTATION_GENOME_LENGTH_WEIGHT_OFFSET + MUTATION_GENOME_LENGTH_WEIGHT_COUNT;
    static constexpr int DENSITY_FIELD_CONSTANT = 0;
    static constexpr int DENSITY_FIELD_LINEAR_DIRECTIONAL = 1;
    static constexpr int DENSITY_FIELD_RADIAL = 2;
    static constexpr int VECTOR_FIELD_LINEAR_DIRECTIONAL = 0;
    static constexpr int VECTOR_FIELD_RADIAL_INWARD = 1;
    static constexpr int VECTOR_FIELD_RADIAL_OUTWARD = 2;

    SimulationRandom mRandom;
    bool mIsPreviewMode = false;
    double mLightAngle = PI / 2.0;
    double mLightAngleCos = 0.0;
    double mLightAngleSin = 0.0;
    double mFoodSpeed = 0.0;
    double mFoodSize = 0.0;
    double mRadiation = 0.0;
    double mLightIntensity = 0.0;
    double mLightSpeed = 0.0;
    double mLightRange = 0.0;
    double mViscosity = 0.0;
    double mDensity = 0.0;
    double mGravity = 0.0;
    double mDensityGradient = 0.0;
    int mGravityFieldType = VECTOR_FIELD_LINEAR_DIRECTIONAL;
    double mGravityFieldStrength = 0.0;
    double mGravityFieldAngle = -PI / 2.0;
    int mDensityFieldType = DENSITY_FIELD_LINEAR_DIRECTIONAL;
    double mDensityFieldBase = 0.0;
    double mDensityFieldStrength = 0.0;
    double mDensityFieldAngle = PI / 2.0;
    double mDensityFieldOffset = 0.0;
    double mDensityFieldSoftness = 0.0;
    int mSalinityFieldType = DENSITY_FIELD_CONSTANT;
    double mSalinityFieldBase = 0.0;
    double mSalinityFieldStrength = 0.0;
    double mSalinityFieldAngle = PI / 2.0;
    double mSalinityFieldOffset = 0.0;
    double mSalinityFieldSoftness = 0.0;
    int mFoodLipidLayerFieldType = DENSITY_FIELD_CONSTANT;
    double mFoodLipidLayerFieldBase = 0.0;
    double mFoodLipidLayerFieldStrength = 0.0;
    double mFoodLipidLayerFieldAngle = PI / 2.0;
    double mFoodLipidLayerFieldOffset = 0.0;
    double mFoodLipidLayerFieldSoftness = 0.0;
    // Authoritative serialized size. Never cache a second geometry beside it:
    // loaders, batches and tests may change this value directly.
    double mRadius = 0.0;
    double mInnerRadius = 0.0;
    double mTestMulticellularCoef = 0.0;
    double mTestMutationCoef = 0.10;
    double mTestFoodDecay = 0.003 * CellData::MIN_MASS / 0.036;
    double mTestIdleMetabolismScale = 1.0;
    double mTestFlagellocyteCostScale = 1.0;
    double mTestPhagocyteEatSpeedScale = 1.0;
    double mFrictionStatic = 0.0;
    double mFrictionDynamic = 0.0;
    float mSalinity = 0.0f;
    float mFoodLipidLayer = 0.0f;
    double mFoodLumpiness = 0.0;
    double mFoodLumpSize = 0.0;
    double mNitrates = 0.0;
    int maxCellCount = 0;
    int maxFoodCount = 0;
    int maxDeadSplashCount = 0;
    std::array<bool, CELL_TYPE_COUNT> mGenePool{};
    std::array<double, MUTATION_WEIGHT_COUNT> mMutationWeights{};
    bool IsOnlyTweakMutations = false;
    bool mutationOnlyAfterSplit = false;
    bool aging = false;
    bool addRandom = false;
    bool dieEdge = false;
    bool mobileFood = false;

    Environment();

    [[nodiscard]] cellularity::SubstrateGeometry geometry() const noexcept {
        return cellularity::SubstrateGeometry(mRadius, mInnerRadius);
    }

    void set(const Environment& environment);
    void resetLegacyMutationWeights();
    void syncGravityFieldFromLegacyGravity();
    void syncLegacyGravityFromVectorField();
    void getGravityVector(double positionX, double positionY, double& gravityX, double& gravityY) const;
    void syncDensityFieldFromLegacyGradient();
    void syncLegacyGradientFromDensityField();
    double getDensity(double positionX, double positionY) const;
    void syncSalinityFieldFromLegacyValue();
    void syncLegacySalinityFromField();
    double getSalinity(double positionX, double positionY) const;
    void syncFoodLipidLayerFieldFromLegacyValue();
    void syncLegacyFoodLipidLayerFromField();
    double getFoodLipidLayer(double positionX, double positionY) const;
    double evaluateScalarField(int fieldType,
            double base,
            double strength,
            double angle,
            double offset,
            double softness,
            double positionX,
            double positionY) const;
    double getLight(double positionX, double positionY) const;
    double getLightddx(double positionX, double positionY) const;
    double getLightddy(double positionX, double positionY) const;
    void updateLight(double deltaTime);

    static Environment constructDefaultEnvironment(int maxCellCount, int maxFoodCount, double radius);
    static Environment setupPreviewSimulationEnvironment(int previewMaxShow);
};
