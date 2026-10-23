#include "formats/LegacyEnvironmentCodec.h"

#include <algorithm>
#include <vector>

#include "formats/FormatLimits.h"
#include "formats/LegacyFormatConstants.h"
#include "formats/SnapshotCodecSupport.h"
#include "io/JavaObjectStreamReader.h"

namespace cellularity::snapshot_codec {

Environment readLegacyEnvironment(JavaObjectStreamReader& input,
        bool previewMode,
        Environment environment) {
    environment.mIsPreviewMode = previewMode;
    environment.mInnerRadius = 0.0;
    applySerializedMutationWeights(
            environment, {}, MutationWeightLayout::Legacy);
    const int version = input.readInt();
    if (version > legacy_format::kMaximumProductVersion) {
        failDecode(DecodeErrorCode::NewerProductVersion,
                "Unsupported newer legacy Environment product version");
    }
    if (version < legacy_format::kMinimumProductVersion) {
        failDecode(DecodeErrorCode::InvalidVersion,
                "Incompatible legacy Environment format");
    }

    environment.mFoodSpeed = input.readDouble();
    if (version == 1) {
        environment.mFoodSpeed *= 0.15;
    }
    if (version >= 2) {
        environment.mFoodSize = input.readDouble();
    }

    environment.mRadiation = input.readDouble();
    environment.mLightIntensity = input.readDouble();
    environment.mLightSpeed = input.readDouble();
    environment.mLightRange = input.readDouble();
    if (version <= 23) {
        input.readDouble();
    }
    environment.mViscosity = input.readDouble();

    environment.mGenePool.fill(false);
    const int geneTypeCount = input.readInt();
    validateSerializedCount(
            geneTypeCount,
            format_limits::kMaximumSerializedCollectionEntries,
            "legacy Environment gene pool");
    const int copiedGeneTypeCount =
            std::min(geneTypeCount, Environment::CELL_TYPE_COUNT);
    for (int index = 0; index < copiedGeneTypeCount; index++) {
        environment.mGenePool[static_cast<std::size_t>(index)] = input.readBoolean();
    }
    for (int index = copiedGeneTypeCount; index < geneTypeCount; index++) {
        input.readBoolean();
    }

    environment.addRandom = input.readBoolean();
    environment.mGravity = input.readDouble();
    environment.syncGravityFieldFromLegacyGravity();
    environment.mDensity = input.readDouble();
    environment.mDensityGradient = input.readDouble();
    environment.syncDensityFieldFromLegacyGradient();
    environment.dieEdge = input.readBoolean();
    environment.mNitrates = version >= 21
            ? input.readDouble()
            : (input.readBoolean() ? 1.0 : 0.0);

    if (version >= 3) {
        environment.maxCellCount = input.readInt();
        environment.maxFoodCount = input.readInt();
        environment.mRadius = input.readDouble();
    }
    if (version >= 9) {
        environment.mFrictionDynamic = input.readDouble();
        environment.mFrictionStatic = input.readDouble();
        environment.IsOnlyTweakMutations = input.readBoolean();
    }
    if (version >= 97) {
        environment.mutationOnlyAfterSplit = input.readBoolean();
    }
    if (version >= 98) {
        const int mutationWeightCount = input.readInt();
        validateSerializedCount(
                mutationWeightCount,
                format_limits::kMaximumSerializedCollectionEntries,
                "legacy Environment mutation weight");
        std::vector<double> serializedMutationWeights;
        serializedMutationWeights.reserve(
                static_cast<std::size_t>(mutationWeightCount));
        for (int index = 0; index < mutationWeightCount; index++) {
            serializedMutationWeights.push_back(input.readDouble());
        }
        applySerializedMutationWeights(
                environment,
                serializedMutationWeights,
                MutationWeightLayout::Legacy);
    }
    if (version >= 99) {
        if (version >= 101) {
            environment.mGravityFieldType = input.readInt();
            environment.mGravityFieldStrength = input.readDouble();
            environment.mGravityFieldAngle = input.readDouble();
        }
        environment.mDensityFieldType = input.readInt();
        environment.mDensityFieldBase = input.readDouble();
        environment.mDensityFieldStrength = input.readDouble();
        environment.mDensityFieldAngle = input.readDouble();
        environment.mDensityFieldOffset = input.readDouble();
        environment.mDensityFieldSoftness = input.readDouble();
    }
    if (version >= 10) {
        environment.mSalinity = input.readFloat();
    }
    if (version >= 14) {
        environment.aging = input.readBoolean();
        environment.mFoodLumpiness = input.readDouble();
        environment.mFoodLumpSize = input.readDouble();
    }
    if (version >= 22) {
        environment.mobileFood = input.readBoolean();
        environment.mFoodLipidLayer = input.readFloat();
    }
    if (version >= 100) {
        environment.mSalinityFieldType = input.readInt();
        environment.mSalinityFieldBase = input.readDouble();
        environment.mSalinityFieldStrength = input.readDouble();
        environment.mSalinityFieldAngle = input.readDouble();
        environment.mSalinityFieldOffset = input.readDouble();
        environment.mSalinityFieldSoftness = input.readDouble();
        environment.mFoodLipidLayerFieldType = input.readInt();
        environment.mFoodLipidLayerFieldBase = input.readDouble();
        environment.mFoodLipidLayerFieldStrength = input.readDouble();
        environment.mFoodLipidLayerFieldAngle = input.readDouble();
        environment.mFoodLipidLayerFieldOffset = input.readDouble();
        environment.mFoodLipidLayerFieldSoftness = input.readDouble();
    } else {
        environment.syncSalinityFieldFromLegacyValue();
        environment.syncFoodLipidLayerFieldFromLegacyValue();
    }
    if (version >= 102) {
        environment.mTestIdleMetabolismScale = input.readDouble();
        environment.mTestFlagellocyteCostScale = input.readDouble();
        environment.mTestPhagocyteEatSpeedScale = input.readDouble();
    }

    environment.maxDeadSplashCount = environment.maxCellCount / 4
            + (environment.maxCellCount % 4 != 0 ? 1 : 0);
    return environment;
}

} // namespace cellularity::snapshot_codec
