#include "core/EnvironmentBatch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "io/DecodeError.h"
#include "io/NativeByteStream.h"

namespace EnvironmentBatch {
namespace {

constexpr int32_t CHANGE_BOOLEAN = 1;
constexpr int32_t CHANGE_SCALAR = 2;
constexpr int32_t CHANGE_FIELD_TYPE = 3;
constexpr int32_t CHANGE_FIELD_VALUE = 4;
constexpr int32_t CHANGE_MUTATION_WEIGHT = 5;
constexpr int32_t CHANGE_RESET_MUTATION_WEIGHTS = 6;
constexpr int32_t CHANGE_GENE_POOL = 7;

constexpr int32_t FIELD_TYPE_SCALAR = 1;
constexpr int32_t FIELD_TYPE_VECTOR = 2;

constexpr int32_t FIELD_GRAVITY = 1;
constexpr int32_t FIELD_DENSITY = 2;
constexpr int32_t FIELD_SALINITY = 3;
constexpr int32_t FIELD_FOOD_LIPID_LAYER = 4;

constexpr int32_t COMPONENT_BASE = 1;
constexpr int32_t COMPONENT_STRENGTH = 2;
constexpr int32_t COMPONENT_ANGLE = 3;
constexpr int32_t COMPONENT_OFFSET = 4;
constexpr int32_t COMPONENT_SOFTNESS = 5;

static_assert(Environment::CELL_TYPE_COUNT == 18,
        "Environment batch schema requires 18 gene-pool entries");
static_assert(Environment::MUTATION_WEIGHT_COUNT == 100,
        "Environment batch schema requires 100 mutation weights");

constexpr double MIN_DENSITY = -3.3;
constexpr double MAX_DENSITY = 3.3;
constexpr double MIN_DENSITY_GRADIENT = -2.0;
constexpr double MAX_DENSITY_GRADIENT = 0.0;
constexpr double MAX_MUTATION_WEIGHT = 25.0;

ApplyResult rejected(int64_t revision,
        RejectionReason reason,
        int32_t changeIndex = -1) {
    ApplyResult result;
    result.revision = std::max<int64_t>(revision, 0);
    result.rejections.push_back({changeIndex, reason});
    return result;
}

void rejectChange(ApplyResult& result,
        int32_t changeIndex,
        RejectionReason reason) {
    result.rejections.push_back({changeIndex, reason});
}

bool inRange(double value, double minimum, double maximum) {
    return value >= minimum && value <= maximum;
}

bool validateFiniteRange(ApplyResult& result,
        int32_t changeIndex,
        double value,
        double minimum,
        double maximum) {
    if (!std::isfinite(value)) {
        rejectChange(result, changeIndex, RejectionReason::NonFiniteValue);
        return false;
    }
    if (!inRange(value, minimum, maximum)) {
        rejectChange(result, changeIndex, RejectionReason::OutOfRange);
        return false;
    }
    return true;
}

void applyBoolean(Environment& environment,
        ApplyResult& result,
        int32_t changeIndex,
        int32_t property,
        bool value) {
    switch (property) {
        case 1:
            environment.addRandom = value;
            return;
        case 2:
            environment.dieEdge = value;
            return;
        case 3:
            environment.IsOnlyTweakMutations = value;
            return;
        case 4:
            environment.mutationOnlyAfterSplit = value;
            return;
        case 5:
            environment.aging = value;
            return;
        case 6:
            environment.mobileFood = value;
            return;
        default:
            rejectChange(result, changeIndex, RejectionReason::InvalidProperty);
    }
}

void applyScalar(Environment& environment,
        ApplyResult& result,
        int32_t changeIndex,
        int32_t property,
        double value) {
    double minimum = 0.0;
    double maximum = 0.0;
    switch (property) {
        case 1:
            minimum = 0.0;
            maximum = Environment::MAX_RADIATION;
            break;
        case 2:
            minimum = -10.0;
            maximum = 10.0;
            break;
        case 3:
            minimum = 0.0;
            maximum = 5.0;
            break;
        case 4:
            minimum = -10.0;
            maximum = 10.0;
            break;
        case 5:
            minimum = 0.0;
            maximum = 5.0;
            break;
        case 6:
            minimum = 0.0;
            maximum = 5.0;
            break;
        case 7:
            minimum = 0.0;
            maximum = 2.0;
            break;
        case 8:
            minimum = 0.0;
            maximum = Environment::VISCOSITY_MAX;
            break;
        case 9:
        case 10:
            minimum = 0.0;
            maximum = Environment::MAX_FRICTION;
            break;
        case 11:
            minimum = 0.0;
            maximum = Environment::MAX_FOOD_SPEED;
            break;
        case 12:
            minimum = Environment::MIN_FOOD_SIZE;
            maximum = Environment::MAX_FOOD_SIZE;
            break;
        case 13:
            minimum = 0.0;
            maximum = Environment::MAX_FOOD_LUMPINESS;
            break;
        case 14:
            minimum = Environment::MIN_FOOD_LUMP_SIZE;
            maximum = Environment::MAX_LUMP_SIZE;
            break;
        case 15:
        case 16:
            minimum = 0.0;
            maximum = 1.0;
            break;
        case 17:
            minimum = 0.0;
            maximum = Environment::MAX_LIGHT;
            break;
        case 18:
            minimum = 0.0;
            maximum = Environment::MAX_LIGHT_RANGE;
            break;
        case 19:
            minimum = 0.0;
            maximum = Environment::MAX_LIGHT_SPEED;
            break;
        case 20:
            minimum = 0.0;
            maximum = 1.0;
            break;
        case 21:
            minimum = MIN_DENSITY;
            maximum = MAX_DENSITY;
            break;
        case 22:
            minimum = MIN_DENSITY_GRADIENT;
            maximum = MAX_DENSITY_GRADIENT;
            break;
        case 23:
            minimum = 0.0;
            maximum = 1.0;
            break;
        default:
            rejectChange(result, changeIndex, RejectionReason::InvalidProperty);
            return;
    }
    if (!validateFiniteRange(result, changeIndex, value, minimum, maximum)) {
        return;
    }

    switch (property) {
        case 1:
            environment.mRadiation = value;
            return;
        case 2:
            environment.mTestMulticellularCoef = value;
            return;
        case 3:
            environment.mTestMutationCoef = value;
            return;
        case 4:
            environment.mTestFoodDecay = value;
            return;
        case 5:
            environment.mTestIdleMetabolismScale = value;
            return;
        case 6:
            environment.mTestFlagellocyteCostScale = value;
            return;
        case 7:
            environment.mTestPhagocyteEatSpeedScale = value;
            return;
        case 8:
            environment.mViscosity = value;
            return;
        case 9:
            environment.mFrictionDynamic = value;
            return;
        case 10:
            environment.mFrictionStatic = value;
            return;
        case 11:
            environment.mFoodSpeed = value;
            return;
        case 12:
            environment.mFoodSize = value;
            return;
        case 13:
            environment.mFoodLumpiness = value;
            return;
        case 14:
            environment.mFoodLumpSize = value;
            return;
        case 15:
            environment.mFoodLipidLayer = static_cast<float>(value);
            environment.mFoodLipidLayerFieldBase = value;
            return;
        case 16:
            environment.mSalinity = static_cast<float>(value);
            environment.mSalinityFieldBase = value;
            return;
        case 17:
            environment.mLightIntensity = value;
            return;
        case 18:
            environment.mLightRange = value;
            return;
        case 19:
            environment.mLightSpeed = value;
            return;
        case 20:
            environment.mGravity = value;
            environment.mGravityFieldStrength = value;
            return;
        case 21:
            environment.mDensity = value;
            environment.mDensityFieldBase = value;
            return;
        case 22:
            environment.mDensityGradient = value;
            if (environment.mDensityFieldType == Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL) {
                environment.mDensityFieldStrength = value;
            }
            return;
        case 23:
            environment.mNitrates = value;
            return;
        default:
            return;
    }
}

bool validField(int32_t field) {
    return field >= FIELD_GRAVITY && field <= FIELD_FOOD_LIPID_LAYER;
}

void applyFieldType(Environment& environment,
        ApplyResult& result,
        int32_t changeIndex,
        int32_t field,
        int32_t family,
        int32_t value) {
    if (!validField(field)) {
        rejectChange(result, changeIndex, RejectionReason::InvalidProperty);
        return;
    }
    const int32_t expectedFamily = field == FIELD_GRAVITY
            ? FIELD_TYPE_VECTOR
            : FIELD_TYPE_SCALAR;
    if (family != expectedFamily || value < 0 || value > 2) {
        rejectChange(result, changeIndex, RejectionReason::InvalidFieldType);
        return;
    }
    switch (field) {
        case FIELD_GRAVITY:
            environment.mGravityFieldType = value;
            environment.syncLegacyGravityFromVectorField();
            return;
        case FIELD_DENSITY:
            environment.mDensityFieldType = value;
            environment.syncLegacyGradientFromDensityField();
            return;
        case FIELD_SALINITY:
            environment.mSalinityFieldType = value;
            environment.syncLegacySalinityFromField();
            return;
        case FIELD_FOOD_LIPID_LAYER:
            environment.mFoodLipidLayerFieldType = value;
            environment.syncLegacyFoodLipidLayerFromField();
            return;
        default:
            return;
    }
}

bool fieldComponentRange(int32_t field,
        int32_t component,
        double& minimum,
        double& maximum) {
    if (component == COMPONENT_ANGLE) {
        minimum = -Environment::PI;
        maximum = Environment::PI;
        return true;
    }
    if (field == FIELD_GRAVITY) {
        if (component != COMPONENT_STRENGTH)
            return false;
        minimum = 0.0;
        maximum = 1.0;
        return true;
    }
    if (component == COMPONENT_OFFSET) {
        minimum = -1.0;
        maximum = 1.0;
        return true;
    }
    if (component == COMPONENT_SOFTNESS) {
        minimum = 0.0;
        maximum = 1.0;
        return true;
    }
    if (field == FIELD_DENSITY) {
        if (component == COMPONENT_BASE) {
            minimum = MIN_DENSITY;
            maximum = MAX_DENSITY;
            return true;
        }
        if (component == COMPONENT_STRENGTH) {
            minimum = -2.0;
            maximum = 2.0;
            return true;
        }
        return false;
    }
    if (field == FIELD_SALINITY || field == FIELD_FOOD_LIPID_LAYER) {
        if (component == COMPONENT_BASE) {
            minimum = 0.0;
            maximum = 1.0;
            return true;
        }
        if (component == COMPONENT_STRENGTH) {
            minimum = -1.0;
            maximum = 1.0;
            return true;
        }
    }
    return false;
}

void syncField(Environment& environment, int32_t field) {
    switch (field) {
        case FIELD_GRAVITY:
            environment.syncLegacyGravityFromVectorField();
            return;
        case FIELD_DENSITY:
            environment.syncLegacyGradientFromDensityField();
            return;
        case FIELD_SALINITY:
            environment.syncLegacySalinityFromField();
            return;
        case FIELD_FOOD_LIPID_LAYER:
            environment.syncLegacyFoodLipidLayerFromField();
            return;
        default:
            return;
    }
}

void applyFieldValue(Environment& environment,
        ApplyResult& result,
        int32_t changeIndex,
        int32_t field,
        int32_t component,
        double value) {
    if (!validField(field)) {
        rejectChange(result, changeIndex, RejectionReason::InvalidProperty);
        return;
    }
    double minimum = 0.0;
    double maximum = 0.0;
    if (!fieldComponentRange(field, component, minimum, maximum)) {
        rejectChange(result, changeIndex, RejectionReason::InvalidFieldComponent);
        return;
    }
    if (!validateFiniteRange(result, changeIndex, value, minimum, maximum)) {
        return;
    }

    if (field == FIELD_GRAVITY) {
        if (component == COMPONENT_STRENGTH)
            environment.mGravityFieldStrength = value;
        if (component == COMPONENT_ANGLE)
            environment.mGravityFieldAngle = value;
        syncField(environment, field);
        return;
    }

    double* target = nullptr;
    if (field == FIELD_DENSITY) {
        if (component == COMPONENT_BASE)
            target = &environment.mDensityFieldBase;
        if (component == COMPONENT_STRENGTH)
            target = &environment.mDensityFieldStrength;
        if (component == COMPONENT_ANGLE)
            target = &environment.mDensityFieldAngle;
        if (component == COMPONENT_OFFSET)
            target = &environment.mDensityFieldOffset;
        if (component == COMPONENT_SOFTNESS)
            target = &environment.mDensityFieldSoftness;
    } else if (field == FIELD_SALINITY) {
        if (component == COMPONENT_BASE)
            target = &environment.mSalinityFieldBase;
        if (component == COMPONENT_STRENGTH)
            target = &environment.mSalinityFieldStrength;
        if (component == COMPONENT_ANGLE)
            target = &environment.mSalinityFieldAngle;
        if (component == COMPONENT_OFFSET)
            target = &environment.mSalinityFieldOffset;
        if (component == COMPONENT_SOFTNESS)
            target = &environment.mSalinityFieldSoftness;
    } else if (field == FIELD_FOOD_LIPID_LAYER) {
        if (component == COMPONENT_BASE)
            target = &environment.mFoodLipidLayerFieldBase;
        if (component == COMPONENT_STRENGTH)
            target = &environment.mFoodLipidLayerFieldStrength;
        if (component == COMPONENT_ANGLE)
            target = &environment.mFoodLipidLayerFieldAngle;
        if (component == COMPONENT_OFFSET)
            target = &environment.mFoodLipidLayerFieldOffset;
        if (component == COMPONENT_SOFTNESS)
            target = &environment.mFoodLipidLayerFieldSoftness;
    }
    if (target == nullptr) {
        rejectChange(result, changeIndex, RejectionReason::InvalidFieldComponent);
        return;
    }
    *target = value;
    syncField(environment, field);
}

void writeScalarFields(ByteWriter& writer, const Environment& environment) {
    writer.writeDouble(environment.mRadiation);
    writer.writeDouble(environment.mTestMulticellularCoef);
    writer.writeDouble(environment.mTestMutationCoef);
    writer.writeDouble(environment.mTestFoodDecay);
    writer.writeDouble(environment.mTestIdleMetabolismScale);
    writer.writeDouble(environment.mTestFlagellocyteCostScale);
    writer.writeDouble(environment.mTestPhagocyteEatSpeedScale);
    writer.writeDouble(environment.mViscosity);
    writer.writeDouble(environment.mFrictionDynamic);
    writer.writeDouble(environment.mFrictionStatic);
    writer.writeDouble(environment.mFoodSpeed);
    writer.writeDouble(environment.mFoodSize);
    writer.writeDouble(environment.mFoodLumpiness);
    writer.writeDouble(environment.mFoodLumpSize);
    writer.writeDouble(environment.mFoodLipidLayer);
    writer.writeDouble(environment.mSalinity);
    writer.writeDouble(environment.mLightIntensity);
    writer.writeDouble(environment.mLightRange);
    writer.writeDouble(environment.mLightSpeed);
    writer.writeDouble(environment.mGravity);
    writer.writeDouble(environment.mDensity);
    writer.writeDouble(environment.mDensityGradient);
    writer.writeDouble(environment.mNitrates);
}

} // namespace

std::vector<uint8_t> writeState(const Environment& environment, int64_t revision) {
    ByteWriter writer;
    writer.writeInt(STATE_MAGIC);
    writer.writeInt(SCHEMA_VERSION);
    writer.writeLong(revision);
    writer.writeBool(environment.addRandom);
    writer.writeBool(environment.dieEdge);
    writer.writeBool(environment.IsOnlyTweakMutations);
    writer.writeBool(environment.mutationOnlyAfterSplit);
    writer.writeBool(environment.aging);
    writer.writeBool(environment.mobileFood);

    writer.writeInt(environment.mGravityFieldType);
    writer.writeDouble(environment.mGravityFieldStrength);
    writer.writeDouble(environment.mGravityFieldAngle);

    const auto writeScalarField = [&writer](int32_t type,
                                          double base,
                                          double strength,
                                          double angle,
                                          double offset,
                                          double softness) {
        writer.writeInt(type);
        writer.writeDouble(base);
        writer.writeDouble(strength);
        writer.writeDouble(angle);
        writer.writeDouble(offset);
        writer.writeDouble(softness);
    };
    writeScalarField(environment.mDensityFieldType,
            environment.mDensityFieldBase,
            environment.mDensityFieldStrength,
            environment.mDensityFieldAngle,
            environment.mDensityFieldOffset,
            environment.mDensityFieldSoftness);
    writeScalarField(environment.mSalinityFieldType,
            environment.mSalinityFieldBase,
            environment.mSalinityFieldStrength,
            environment.mSalinityFieldAngle,
            environment.mSalinityFieldOffset,
            environment.mSalinityFieldSoftness);
    writeScalarField(environment.mFoodLipidLayerFieldType,
            environment.mFoodLipidLayerFieldBase,
            environment.mFoodLipidLayerFieldStrength,
            environment.mFoodLipidLayerFieldAngle,
            environment.mFoodLipidLayerFieldOffset,
            environment.mFoodLipidLayerFieldSoftness);
    writeScalarFields(writer, environment);

    writer.writeInt(static_cast<int32_t>(environment.mGenePool.size()));
    for (bool value : environment.mGenePool)
        writer.writeBool(value);
    writer.writeInt(static_cast<int32_t>(environment.mMutationWeights.size()));
    for (double value : environment.mMutationWeights)
        writer.writeDouble(value);
    return writer.takeBytes();
}

ApplyResult applyPatch(Environment& environment,
        int64_t currentRevision,
        std::vector<uint8_t> patchBytes) {
    if (patchBytes.size() > MAX_WIRE_BYTES) {
        return rejected(currentRevision, RejectionReason::InvalidCount);
    }
    try {
        ByteReader reader(std::move(patchBytes));
        if (reader.readInt() != PATCH_MAGIC) {
            return rejected(currentRevision, RejectionReason::MalformedPatch);
        }
        if (reader.readInt() != SCHEMA_VERSION) {
            return rejected(currentRevision, RejectionReason::SchemaMismatch);
        }
        const int64_t baseRevision = reader.readLong();
        if (baseRevision != currentRevision) {
            return rejected(currentRevision, RejectionReason::RevisionConflict);
        }
        const int32_t changeCount = reader.readInt();
        if (changeCount <= 0 || changeCount > MAX_PATCH_CHANGES) {
            return rejected(currentRevision, RejectionReason::InvalidCount);
        }
        if (currentRevision == std::numeric_limits<int64_t>::max()) {
            return rejected(currentRevision, RejectionReason::OutOfRange);
        }

        Environment candidate = environment;
        ApplyResult result;
        result.revision = currentRevision;
        int32_t lastGenePoolChange = -1;

        for (int32_t changeIndex = 0; changeIndex < changeCount; changeIndex++) {
            const int32_t kind = reader.readInt();
            switch (kind) {
                case CHANGE_BOOLEAN: {
                    const int32_t property = reader.readInt();
                    const bool value = reader.readBool();
                    applyBoolean(candidate, result, changeIndex, property, value);
                    break;
                }
                case CHANGE_SCALAR: {
                    const int32_t property = reader.readInt();
                    const double value = reader.readDouble();
                    applyScalar(candidate, result, changeIndex, property, value);
                    break;
                }
                case CHANGE_FIELD_TYPE: {
                    const int32_t field = reader.readInt();
                    const int32_t family = reader.readInt();
                    const int32_t value = reader.readInt();
                    applyFieldType(candidate, result, changeIndex, field, family, value);
                    break;
                }
                case CHANGE_FIELD_VALUE: {
                    const int32_t field = reader.readInt();
                    const int32_t component = reader.readInt();
                    const double value = reader.readDouble();
                    applyFieldValue(candidate, result, changeIndex, field, component, value);
                    break;
                }
                case CHANGE_MUTATION_WEIGHT: {
                    const int32_t index = reader.readInt();
                    const double value = reader.readDouble();
                    if (index < 0 || index >= Environment::MUTATION_WEIGHT_COUNT) {
                        rejectChange(result, changeIndex, RejectionReason::InvalidIndex);
                    } else if (validateFiniteRange(result, changeIndex, value,
                                       0.0, MAX_MUTATION_WEIGHT)) {
                        candidate.mMutationWeights[static_cast<size_t>(index)] = value;
                    }
                    break;
                }
                case CHANGE_RESET_MUTATION_WEIGHTS:
                    candidate.resetLegacyMutationWeights();
                    break;
                case CHANGE_GENE_POOL: {
                    const int32_t index = reader.readInt();
                    const bool enabled = reader.readBool();
                    if (index < 0 || index >= Environment::CELL_TYPE_COUNT) {
                        rejectChange(result, changeIndex, RejectionReason::InvalidIndex);
                    } else {
                        candidate.mGenePool[static_cast<size_t>(index)] = enabled;
                        lastGenePoolChange = changeIndex;
                    }
                    break;
                }
                default:
                    return rejected(currentRevision,
                            RejectionReason::InvalidChangeKind,
                            changeIndex);
            }
        }
        if (reader.remaining() != 0) {
            return rejected(currentRevision, RejectionReason::MalformedPatch);
        }
        if (lastGenePoolChange >= 0
                && std::none_of(candidate.mGenePool.begin(), candidate.mGenePool.end(),
                        [](bool enabled) {
                            return enabled;
                        })) {
            rejectChange(result, lastGenePoolChange, RejectionReason::EmptyGenePool);
        }
        if (!result.rejections.empty()) {
            return result;
        }

        environment = std::move(candidate);
        result.applied = true;
        result.revision = currentRevision + 1;
        return result;
    } catch (const DecodeError&) {
        return rejected(currentRevision, RejectionReason::MalformedPatch);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::length_error&) {
        throw;
    }
}

std::vector<uint8_t> writeApplyResult(const ApplyResult& result) {
    ByteWriter writer;
    writer.writeInt(RESULT_MAGIC);
    writer.writeInt(SCHEMA_VERSION);
    writer.writeBool(result.applied);
    writer.writeLong(result.revision);
    writer.writeInt(static_cast<int32_t>(result.rejections.size()));
    for (const Rejection& rejection : result.rejections) {
        writer.writeInt(rejection.changeIndex);
        writer.writeInt(static_cast<int32_t>(rejection.reason));
    }
    return writer.takeBytes();
}

} // namespace EnvironmentBatch
