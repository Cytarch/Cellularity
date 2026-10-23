#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "core/Environment.h"
#include "core/EnvironmentBatch.h"
#include "io/NativeByteStream.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity::tests;

namespace {

void writePatchHeader(ByteWriter& writer, int64_t revision, int32_t changeCount) {
    writer.writeInt(EnvironmentBatch::PATCH_MAGIC);
    writer.writeInt(EnvironmentBatch::SCHEMA_VERSION);
    writer.writeLong(revision);
    writer.writeInt(changeCount);
}

void writeScalarFieldChange(ByteWriter& writer, int32_t property, double value) {
    writer.writeInt(2);
    writer.writeInt(property);
    writer.writeDouble(value);
}

void assertScalarField(ByteReader& reader,
        int32_t type,
        double base,
        double strength,
        double angle,
        double offset,
        double softness,
        const char* name) {
    assertTrue(reader.readInt() == type, std::string(name) + " type");
    assertNear(base, reader.readDouble(), 0.0, std::string(name) + " base");
    assertNear(strength, reader.readDouble(), 0.0, std::string(name) + " strength");
    assertNear(angle, reader.readDouble(), 0.0, std::string(name) + " angle");
    assertNear(offset, reader.readDouble(), 0.0, std::string(name) + " offset");
    assertNear(softness, reader.readDouble(), 0.0, std::string(name) + " softness");
}

void testSnapshotContainsEveryRuntimeField() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 2.0);
    environment.addRandom = true;
    environment.dieEdge = false;
    environment.IsOnlyTweakMutations = true;
    environment.mutationOnlyAfterSplit = false;
    environment.aging = true;
    environment.mobileFood = true;
    environment.mGravityFieldType = Environment::VECTOR_FIELD_RADIAL_INWARD;
    environment.mGravityFieldStrength = 0.11;
    environment.mGravityFieldAngle = -0.12;
    environment.mDensityFieldType = Environment::DENSITY_FIELD_RADIAL;
    environment.mDensityFieldBase = -0.31;
    environment.mDensityFieldStrength = 0.32;
    environment.mDensityFieldAngle = 0.33;
    environment.mDensityFieldOffset = -0.34;
    environment.mDensityFieldSoftness = 0.35;
    environment.mSalinityFieldType = Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL;
    environment.mSalinityFieldBase = 0.41;
    environment.mSalinityFieldStrength = -0.42;
    environment.mSalinityFieldAngle = 0.43;
    environment.mSalinityFieldOffset = 0.44;
    environment.mSalinityFieldSoftness = 0.45;
    environment.mFoodLipidLayerFieldType = Environment::DENSITY_FIELD_CONSTANT;
    environment.mFoodLipidLayerFieldBase = 0.51;
    environment.mFoodLipidLayerFieldStrength = -0.52;
    environment.mFoodLipidLayerFieldAngle = -0.53;
    environment.mFoodLipidLayerFieldOffset = 0.54;
    environment.mFoodLipidLayerFieldSoftness = 0.55;

    const std::array<double, 23> scalars{
            0.01, -1.2, 1.3, -1.4, 1.5, 1.6, 1.7, 1.8,
            1.9, 2.0, 2.1, 0.22, 2.3, 0.14, 0.25, 0.26,
            0.27, 0.28, 0.29, 0.30, -0.31, -0.32, 0.33
    };
    environment.mRadiation = scalars[0];
    environment.mTestMulticellularCoef = scalars[1];
    environment.mTestMutationCoef = scalars[2];
    environment.mTestFoodDecay = scalars[3];
    environment.mTestIdleMetabolismScale = scalars[4];
    environment.mTestFlagellocyteCostScale = scalars[5];
    environment.mTestPhagocyteEatSpeedScale = scalars[6];
    environment.mViscosity = scalars[7];
    environment.mFrictionDynamic = scalars[8];
    environment.mFrictionStatic = scalars[9];
    environment.mFoodSpeed = scalars[10];
    environment.mFoodSize = scalars[11];
    environment.mFoodLumpiness = scalars[12];
    environment.mFoodLumpSize = scalars[13];
    environment.mFoodLipidLayer = static_cast<float>(scalars[14]);
    environment.mSalinity = static_cast<float>(scalars[15]);
    environment.mLightIntensity = scalars[16];
    environment.mLightRange = scalars[17];
    environment.mLightSpeed = scalars[18];
    environment.mGravity = scalars[19];
    environment.mDensity = scalars[20];
    environment.mDensityGradient = scalars[21];
    environment.mNitrates = scalars[22];
    for (size_t index = 0; index < environment.mGenePool.size(); index++) {
        environment.mGenePool[index] = index % 3 == 0;
    }
    for (size_t index = 0; index < environment.mMutationWeights.size(); index++) {
        environment.mMutationWeights[index] = static_cast<double>(index) / 10.0;
    }

    ByteReader reader(EnvironmentBatch::writeState(environment, 77));
    assertTrue(reader.readInt() == EnvironmentBatch::STATE_MAGIC, "Environment state magic");
    assertTrue(reader.readInt() == EnvironmentBatch::SCHEMA_VERSION, "Environment state schema");
    assertTrue(reader.readLong() == 77, "Environment state native revision");
    assertTrue(reader.readBool(), "Environment state addRandom");
    assertTrue(!reader.readBool(), "Environment state dieEdge");
    assertTrue(reader.readBool(), "Environment state onlyTweakMutations");
    assertTrue(!reader.readBool(), "Environment state mutationOnlyAfterSplit");
    assertTrue(reader.readBool(), "Environment state aging");
    assertTrue(reader.readBool(), "Environment state mobileFood");
    assertTrue(reader.readInt() == environment.mGravityFieldType, "Environment gravity type");
    assertNear(environment.mGravityFieldStrength, reader.readDouble(), 0.0, "Environment gravity strength");
    assertNear(environment.mGravityFieldAngle, reader.readDouble(), 0.0, "Environment gravity angle");
    assertScalarField(reader, environment.mDensityFieldType, environment.mDensityFieldBase,
            environment.mDensityFieldStrength, environment.mDensityFieldAngle,
            environment.mDensityFieldOffset, environment.mDensityFieldSoftness,
            "Environment density field");
    assertScalarField(reader, environment.mSalinityFieldType, environment.mSalinityFieldBase,
            environment.mSalinityFieldStrength, environment.mSalinityFieldAngle,
            environment.mSalinityFieldOffset, environment.mSalinityFieldSoftness,
            "Environment salinity field");
    assertScalarField(reader, environment.mFoodLipidLayerFieldType,
            environment.mFoodLipidLayerFieldBase,
            environment.mFoodLipidLayerFieldStrength,
            environment.mFoodLipidLayerFieldAngle,
            environment.mFoodLipidLayerFieldOffset,
            environment.mFoodLipidLayerFieldSoftness,
            "Environment nutrient coating field");
    for (size_t index = 0; index < scalars.size(); index++) {
        const double tolerance = index == 14 || index == 15 ? 1e-7 : 0.0;
        assertNear(scalars[index], reader.readDouble(), tolerance,
                "Environment scalar field order");
    }
    assertTrue(reader.readInt() == Environment::CELL_TYPE_COUNT, "Environment gene pool count");
    for (bool expected : environment.mGenePool) {
        assertTrue(reader.readBool() == expected, "Environment gene pool value");
    }
    assertTrue(reader.readInt() == Environment::MUTATION_WEIGHT_COUNT,
            "Environment mutation weight count");
    for (double expected : environment.mMutationWeights) {
        assertNear(expected, reader.readDouble(), 0.0, "Environment mutation weight value");
    }
    assertTrue(reader.remaining() == 0, "Environment state has no trailing bytes");
}

void testPatchAppliesEveryChangeFamilyAsOneRevision() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 2.0);
    ByteWriter writer;
    constexpr int32_t scalarCount = 23;
    constexpr int32_t fieldValueCount = 17;
    constexpr int32_t changeCount = 6 + scalarCount + 4 + fieldValueCount + 3;
    writePatchHeader(writer, 4, changeCount);
    for (int32_t property = 1; property <= 6; property++) {
        writer.writeInt(1);
        writer.writeInt(property);
        writer.writeBool(property % 2 == 0);
    }
    const std::array<double, scalarCount> values{
            0.02, -2.0, 2.0, -3.0, 2.1, 2.2, 1.2, 25.0,
            2.3, 2.4, 3.0, 0.30, 3.1, 0.15, 0.35, 0.36,
            0.37, 0.38, 0.39, 0.40, -0.41, -0.42, 0.43
    };
    for (int32_t property = 1; property <= scalarCount; property++) {
        writeScalarFieldChange(writer, property, values[static_cast<size_t>(property - 1)]);
    }
    for (int32_t field = 1; field <= 4; field++) {
        writer.writeInt(3);
        writer.writeInt(field);
        writer.writeInt(field == 1 ? 2 : 1);
        writer.writeInt(2);
    }
    const auto writeFieldValue = [&writer](int32_t field, int32_t component, double value) {
        writer.writeInt(4);
        writer.writeInt(field);
        writer.writeInt(component);
        writer.writeDouble(value);
    };
    writeFieldValue(1, 2, 0.8);
    writeFieldValue(1, 3, 0.7);
    const std::array<std::array<double, 5>, 3> fieldValues{{
            {{0.6, -0.5, 0.4, -0.3, 0.2}},
            {{0.7, 0.6, 0.5, 0.4, 0.3}},
            {{0.2, -0.2, -0.4, 0.1, 0.9}},
    }};
    for (int32_t field = 2; field <= 4; field++) {
        for (int32_t component = 1; component <= 5; component++) {
            writeFieldValue(field, component,
                    fieldValues[static_cast<size_t>(field - 2)]
                               [static_cast<size_t>(component - 1)]);
        }
    }
    writer.writeInt(6); // Reset is an ordering barrier inside the same atomic patch.
    writer.writeInt(5);
    writer.writeInt(3);
    writer.writeDouble(0.75);
    writer.writeInt(7);
    writer.writeInt(0);
    writer.writeBool(false);

    EnvironmentBatch::ApplyResult result = EnvironmentBatch::applyPatch(
            environment, 4, writer.takeBytes());
    assertTrue(result.applied, "Environment batch applies");
    assertTrue(result.revision == 5, "Environment batch advances exactly one revision");
    assertTrue(result.rejections.empty(), "Applied Environment batch has no rejections");
    assertTrue(!environment.addRandom && environment.dieEdge,
            "Environment boolean changes route by stable ID");
    assertNear(values[0], environment.mRadiation, 0.0, "Environment radiation patch");
    assertNear(values[22], environment.mNitrates, 0.0, "Environment nitrates patch");
    assertTrue(environment.mGravityFieldType == Environment::VECTOR_FIELD_RADIAL_OUTWARD,
            "Changing gravity angle does not implicitly change its type");
    assertNear(0.8, environment.mGravityFieldStrength, 0.0, "Environment gravity field patch");
    assertTrue(environment.mDensityFieldType == Environment::DENSITY_FIELD_RADIAL,
            "Environment density field type patch");
    assertNear(0.6, environment.mDensityFieldBase, 0.0, "Environment density base patch");
    assertNear(0.7, environment.mSalinityFieldBase, 0.0, "Environment salinity base patch");
    assertNear(0.2, environment.mFoodLipidLayerFieldBase, 0.0,
            "Environment nutrient coating base patch");
    assertNear(0.75, environment.mMutationWeights[3], 0.0,
            "Environment mutation reset preserves following explicit value");
    assertTrue(!environment.mGenePool[0] && environment.mGenePool[1],
            "Environment gene pool patch preserves non-empty pool");

    ByteReader resultReader(EnvironmentBatch::writeApplyResult(result));
    assertTrue(resultReader.readInt() == EnvironmentBatch::RESULT_MAGIC,
            "Environment result magic");
    assertTrue(resultReader.readInt() == EnvironmentBatch::SCHEMA_VERSION,
            "Environment result schema");
    assertTrue(resultReader.readBool(), "Environment result applied status");
    assertTrue(resultReader.readLong() == 5, "Environment result applied revision");
    assertTrue(resultReader.readInt() == 0 && resultReader.remaining() == 0,
            "Environment result has no unexpected rejection data");
}

void testInvalidPatchIsRejectedWithoutPartialApplication() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 2.0);
    environment.mRadiation = 0.01;
    environment.mDensity = 0.25;
    const double oldWeight = environment.mMutationWeights[0];
    ByteWriter writer;
    writePatchHeader(writer, 9, 4);
    writeScalarFieldChange(writer, 1, 0.03);
    writeScalarFieldChange(writer, 21, 99.0);
    writer.writeInt(5);
    writer.writeInt(-1);
    writer.writeDouble(0.5);
    writeScalarFieldChange(writer, 3, std::numeric_limits<double>::quiet_NaN());

    EnvironmentBatch::ApplyResult result = EnvironmentBatch::applyPatch(
            environment, 9, writer.takeBytes());
    assertTrue(!result.applied, "Invalid Environment batch is rejected");
    assertTrue(result.revision == 9, "Rejected Environment batch keeps revision");
    assertTrue(result.rejections.size() == 3, "Environment batch returns every field rejection");
    assertTrue(result.rejections[0].changeIndex == 1
                    && result.rejections[0].reason == EnvironmentBatch::RejectionReason::OutOfRange,
            "Environment range rejection identifies its change");
    assertTrue(result.rejections[1].changeIndex == 2
                    && result.rejections[1].reason == EnvironmentBatch::RejectionReason::InvalidIndex,
            "Environment index rejection identifies its change");
    assertTrue(result.rejections[2].changeIndex == 3
                    && result.rejections[2].reason == EnvironmentBatch::RejectionReason::NonFiniteValue,
            "Environment finite rejection identifies its change");
    assertNear(0.01, environment.mRadiation, 0.0,
            "Valid sibling change is not partially committed");
    assertNear(0.25, environment.mDensity, 0.0,
            "Rejected scalar leaves Environment unchanged");
    assertNear(oldWeight, environment.mMutationWeights[0], 0.0,
            "Rejected index leaves mutation weights unchanged");
}

void testPatchLevelValidationAndGenePoolInvariant() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 2.0);

    ByteWriter conflict;
    writePatchHeader(conflict, 3, 1);
    writeScalarFieldChange(conflict, 1, 0.02);
    auto conflictResult = EnvironmentBatch::applyPatch(environment, 4, conflict.takeBytes());
    assertTrue(conflictResult.rejections[0].reason
                    == EnvironmentBatch::RejectionReason::RevisionConflict,
            "Environment batch rejects stale base revision");

    ByteWriter newerSchema;
    newerSchema.writeInt(EnvironmentBatch::PATCH_MAGIC);
    newerSchema.writeInt(EnvironmentBatch::SCHEMA_VERSION + 1);
    newerSchema.writeLong(4);
    newerSchema.writeInt(1);
    writeScalarFieldChange(newerSchema, 1, 0.02);
    auto schemaResult = EnvironmentBatch::applyPatch(environment, 4, newerSchema.takeBytes());
    assertTrue(schemaResult.rejections[0].reason
                    == EnvironmentBatch::RejectionReason::SchemaMismatch,
            "Environment batch rejects unknown schema");

    ByteWriter excessiveCount;
    writePatchHeader(excessiveCount, 4, EnvironmentBatch::MAX_PATCH_CHANGES + 1);
    auto countResult = EnvironmentBatch::applyPatch(environment, 4, excessiveCount.takeBytes());
    assertTrue(countResult.rejections[0].reason
                    == EnvironmentBatch::RejectionReason::InvalidCount,
            "Environment batch enforces bounded change count");

    auto malformedResult = EnvironmentBatch::applyPatch(environment, 4, {0x01, 0x02});
    assertTrue(malformedResult.rejections[0].reason
                    == EnvironmentBatch::RejectionReason::MalformedPatch,
            "Environment batch rejects truncated bytes");

    ByteWriter unknownKind;
    writePatchHeader(unknownKind, 4, 1);
    unknownKind.writeInt(99);
    auto kindResult = EnvironmentBatch::applyPatch(environment, 4, unknownKind.takeBytes());
    assertTrue(kindResult.rejections[0].changeIndex == 0
                    && kindResult.rejections[0].reason
                            == EnvironmentBatch::RejectionReason::InvalidChangeKind,
            "Environment batch rejects unknown change kind");

    environment.mGenePool.fill(false);
    environment.mGenePool[0] = true;
    ByteWriter emptyPool;
    writePatchHeader(emptyPool, 4, 1);
    emptyPool.writeInt(7);
    emptyPool.writeInt(0);
    emptyPool.writeBool(false);
    auto poolResult = EnvironmentBatch::applyPatch(environment, 4, emptyPool.takeBytes());
    assertTrue(poolResult.rejections[0].changeIndex == 0
                    && poolResult.rejections[0].reason
                            == EnvironmentBatch::RejectionReason::EmptyGenePool,
            "Environment batch rejects an empty gene pool");
    assertTrue(environment.mGenePool[0],
            "Rejected empty gene pool does not alter Environment");

    environment.mGenePool.fill(false); // Legacy/preview snapshots may contain an empty pool.
    ByteWriter unrelated;
    writePatchHeader(unrelated, 4, 1);
    writeScalarFieldChange(unrelated, 1, 0.02);
    auto unrelatedResult = EnvironmentBatch::applyPatch(environment, 4, unrelated.takeBytes());
    assertTrue(unrelatedResult.applied,
            "Empty legacy gene pool does not block unrelated Environment changes");
}

} // namespace

int main() {
    testSnapshotContainsEveryRuntimeField();
    testPatchAppliesEveryChangeFamilyAsOneRevision();
    testInvalidPatchIsRejectedWithoutPartialApplication();
    testPatchLevelValidationAndGenePoolInvariant();
    std::cout << "Environment batch contract tests passed\n";
    return 0;
}
