#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/CanonicalGene.h"
#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/GenomeMetadata.h"
#include "core/SimulationRandom.h"
#include "formats/CellSnapshotWireCodec.h"
#include "formats/GenomeDraftCodec.h"
#include "formats/InternalWireFormatConstants.h"
#include "formats/LegacyGeneCodec.h"
#include "formats/LegacySubstrateCodec.h"
#include "formats/NativeSubstrateLoader.h"
#include "formats/SubstrateFileCodec.h"
#include "formats/SubstrateFormatTypes.h"
#include "formats/release/ReleaseGenomeCodec.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "io/JavaObjectStreamReader.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderSourceFrame.h"
#include "tests/fixtures/FormatFixtureCorpus.h"
#include "tests/support/StableDigest.h"

namespace {

using cellularity::GeneBuilder;
using cellularity::GeneIndex;
using cellularity::GeneView;

constexpr double DOUBLE_TOLERANCE = 1e-12;
constexpr double FLOAT_TOLERANCE = 1e-6;
constexpr int CELL_MAGIC = 0x434c4353;
const std::filesystem::path CORPUS_DIRECTORY = CELLULARITY_FIXTURE_CORPUS_DIR;
const std::filesystem::path LEGACY_EFFECTIVE_STATE_ORACLE =
        CELLULARITY_LEGACY_EFFECTIVE_STATE_ORACLE_PATH;

[[noreturn]] void fail(const std::string& message);

class ScopedTemporaryFile {
public:
    explicit ScopedTemporaryFile(const std::string& suffix) {
        static int nextId = 0;
        path = std::filesystem::temp_directory_path()
                / ("cellularity-format-" + std::to_string(nextId++) + suffix);
    }

    ~ScopedTemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void write(const std::vector<uint8_t>& bytes) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!output)
            fail("Could not write temporary format fixture");
    }

    std::vector<uint8_t> read() const {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            fail("Could not open temporary format fixture");
        const std::streamoff size = input.tellg();
        if (size < 0)
            fail("Could not determine temporary format fixture size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input)
            fail("Could not read temporary format fixture");
        return bytes;
    }

    std::filesystem::path path;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Actual, typename Expected>
void assertEqual(const Actual& actual,
        const Expected& expected,
        const std::string& message) {
    if (actual != expected) {
        fail(message + ": values differ");
    }
}

void assertNear(double actual,
        double expected,
        double tolerance,
        const std::string& message) {
    if (!std::isfinite(actual) || !std::isfinite(expected)
            || std::abs(actual - expected) > tolerance) {
        std::ostringstream detail;
        detail.precision(17);
        detail << message << ": expected " << expected << " +/- " << tolerance
               << ", got " << actual;
        fail(detail.str());
    }
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail("Cannot open fixture " + path.string());
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        fail("Cannot determine fixture size " + path.string());
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        fail("Cannot read fixture " + path.string());
    }
    return bytes;
}

const cellularity::test_fixtures::FixtureSpec& fixtureById(const std::string& id) {
    const auto& specs = cellularity::test_fixtures::formatFixtureSpecs();
    const auto found = std::find_if(specs.begin(), specs.end(), [&id](const auto& spec) {
        return spec.id == id;
    });
    if (found == specs.end()) {
        fail("Unknown fixture " + id);
    }
    return *found;
}

std::vector<uint8_t> fixtureBytes(const cellularity::test_fixtures::FixtureSpec& spec) {
    return readFile(CORPUS_DIRECTORY / (spec.id + ".bin"));
}

std::map<std::string, std::string> semanticValues(
        const cellularity::test_fixtures::FixtureSpec& spec) {
    std::map<std::string, std::string> result;
    size_t start = 0;
    while (start <= spec.semanticValues.size()) {
        const size_t separator = spec.semanticValues.find(';', start);
        const std::string item = spec.semanticValues.substr(
                start,
                separator == std::string::npos ? std::string::npos : separator - start);
        const size_t equals = item.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 >= item.size()) {
            fail("Invalid semantic marker for " + spec.id + ": " + item);
        }
        if (!result.emplace(item.substr(0, equals), item.substr(equals + 1)).second) {
            fail("Duplicate semantic marker for " + spec.id + ": " + item);
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return result;
}

const std::string& requiredSemantic(
        const cellularity::test_fixtures::FixtureSpec& spec,
        const std::map<std::string, std::string>& values,
        const std::string& key) {
    const auto found = values.find(key);
    if (found == values.end()) {
        fail("Missing semantic marker " + key + " for " + spec.id);
    }
    return found->second;
}

double semanticDouble(const cellularity::test_fixtures::FixtureSpec& spec,
        const std::map<std::string, std::string>& values,
        const std::string& key) {
    const std::string& text = requiredSemantic(spec, values, key);
    size_t parsed = 0;
    const double result = std::stod(text, &parsed);
    if (parsed != text.size()) {
        fail("Invalid double semantic marker " + key + " for " + spec.id);
    }
    return result;
}

int64_t semanticLong(const cellularity::test_fixtures::FixtureSpec& spec,
        const std::map<std::string, std::string>& values,
        const std::string& key) {
    const std::string& text = requiredSemantic(spec, values, key);
    size_t parsed = 0;
    const int64_t result = std::stoll(text, &parsed);
    if (parsed != text.size()) {
        fail("Invalid integer semantic marker " + key + " for " + spec.id);
    }
    return result;
}

int32_t firstInt(const std::vector<uint8_t>& bytes, size_t offset = 0) {
    if (offset + 4 > bytes.size()) {
        fail("Cannot read fixture header integer");
    }
    const uint32_t value = (static_cast<uint32_t>(bytes[offset]) << 24)
            | (static_cast<uint32_t>(bytes[offset + 1]) << 16)
            | (static_cast<uint32_t>(bytes[offset + 2]) << 8)
            | static_cast<uint32_t>(bytes[offset + 3]);
    return static_cast<int32_t>(value);
}

bool isReleaseFile(const std::vector<std::uint8_t>& bytes) {
    return bytes.size() >= 4
            && bytes[0] == 'C'
            && bytes[1] == 'L'
            && bytes[2] == 'T'
            && bytes[3] == 'Y';
}

Environment legacyFallbackEnvironment() {
    Environment environment = Environment::constructDefaultEnvironment(8, 8, 1.0);
    environment.mLightAngle = 0.75;
    environment.mLightAngleCos = std::cos(environment.mLightAngle);
    environment.mLightAngleSin = std::sin(environment.mLightAngle);
    environment.mFoodSize = 0.333;
    environment.mFrictionDynamic = 0.31;
    environment.mFrictionStatic = 0.41;
    environment.mSalinity = 0.15f;
    environment.syncSalinityFieldFromLegacyValue();
    environment.aging = false;
    environment.mFoodLumpiness = 0.17;
    environment.mFoodLumpSize = 0.027;
    environment.mobileFood = false;
    environment.mFoodLipidLayer = 0.17f;
    environment.syncFoodLipidLayerFieldFromLegacyValue();
    environment.mTestMulticellularCoef = 0.7;
    environment.mTestMutationCoef = 0.8;
    environment.mTestFoodDecay = 0.9;
    environment.mTestIdleMetabolismScale = 0.7;
    environment.mTestFlagellocyteCostScale = 0.8;
    environment.mTestPhagocyteEatSpeedScale = 0.9;
    environment.mMutationWeights.fill(0.5);
    environment.mRandom.setSeed(1234567);
    return environment;
}

std::unique_ptr<SimulationWorld> decodeSubstrate(
        std::vector<uint8_t> bytes,
        const Environment& fallback,
        int* formatVersion = nullptr) {
    if (isReleaseFile(bytes)) {
        if (formatVersion != nullptr) {
            *formatVersion = static_cast<int>(
                    cellularity::save_format::release::
                            kSubstrateContentSchemaMajor);
        }
        return cellularity::save_format::release::decodeReleaseSubstrate(
                bytes, false);
    }
    auto loaded = cellularity::snapshot_codec::loadLegacySubstrateBytesDirect(
            std::move(bytes), false, fallback);
    if (formatVersion != nullptr) {
        *formatVersion = loaded.productVersion;
    }
    return std::move(loaded.world);
}

void assertGeneEquivalent(const Gene& actual,
        const Gene& expected,
        const std::string& context,
        bool compareInitial = true) {
    const GeneView actualView(actual);
    const GeneView expectedView(expected);
    for (int i = 0; i < 3; i++) {
        assertNear(actualView.color(i), expectedView.color(i), FLOAT_TOLERANCE,
                context + ".color[" + std::to_string(i) + "]");
    }
    assertNear(actualView.splitMass(), expectedView.splitMass(), FLOAT_TOLERANCE, context + ".splitMass");
    assertNear(actualView.splitRatio(), expectedView.splitRatio(), FLOAT_TOLERANCE, context + ".splitRatio");
    assertNear(actualView.splitAngle(), expectedView.splitAngle(), FLOAT_TOLERANCE, context + ".splitAngle");
    assertNear(actualView.child1Angle(), expectedView.child1Angle(), FLOAT_TOLERANCE, context + ".child1Angle");
    assertNear(actualView.child2Angle(), expectedView.child2Angle(), FLOAT_TOLERANCE, context + ".child2Angle");
    assertNear(actualView.massFlowPriority(), expectedView.massFlowPriority(), FLOAT_TOLERANCE,
            context + ".massFlowPriority");
    assertNear(actualView.linkStrength(), expectedView.linkStrength(), FLOAT_TOLERANCE,
            context + ".linkStrength");
    assertEqual(actualView.child1GeneIndex(), expectedView.child1GeneIndex(), context + ".child1GeneIndex");
    assertEqual(actualView.child2GeneIndex(), expectedView.child2GeneIndex(), context + ".child2GeneIndex");
    assertEqual(actualView.makesLink(), expectedView.makesLink(), context + ".isMakeLink");
    assertEqual(actualView.child1KeepsLinks(), expectedView.child1KeepsLinks(), context + ".child1KeepLinks");
    assertEqual(actualView.child2KeepsLinks(), expectedView.child2KeepsLinks(), context + ".child2KeepLinks");
    assertEqual(actualView.staysAlive(), expectedView.staysAlive(), context + ".stayAlive");
    if (compareInitial) {
        assertEqual(actualView.initialCompatibilityFlag(), expectedView.initialCompatibilityFlag(), context + ".isInitial");
    }
    assertEqual(actualView.child1Mirrored(), expectedView.child1Mirrored(), context + ".child1Mirror");
    assertEqual(actualView.child2Mirrored(), expectedView.child2Mirrored(), context + ".child2Mirror");
    assertEqual(CellTypes::ordinal(actualView.cellType()), CellTypes::ordinal(expectedView.cellType()),
            context + ".cellType");
    for (int i = 0; i < Gene::MAX_PARAMETRIC_VALUES_COUNT; i++) {
        assertNear(actualView.parametricVariableA(i),
                expectedView.parametricVariableA(i),
                FLOAT_TOLERANCE,
                context + ".parametricA[" + std::to_string(i) + "]");
        assertNear(actualView.parametricVariableB(i),
                expectedView.parametricVariableB(i),
                FLOAT_TOLERANCE,
                context + ".parametricB[" + std::to_string(i) + "]");
        assertNear(actualView.parametricVariableC(i),
                expectedView.parametricVariableC(i),
                FLOAT_TOLERANCE,
                context + ".parametricC[" + std::to_string(i) + "]");
        assertEqual(actualView.parametricSignalChannel(i),
                expectedView.parametricSignalChannel(i),
                context + ".parametricChannel[" + std::to_string(i) + "]");
        assertEqual(actualView.parametricFormulaType(i),
                expectedView.parametricFormulaType(i),
                context + ".parametricFormula[" + std::to_string(i) + "]");
    }
    for (int i = 0; i < Gene::MAX_INTEGERS_COUNT; i++) {
        assertEqual(actualView.integerValue(i), expectedView.integerValue(i),
                context + ".integer[" + std::to_string(i) + "]");
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
    for (size_t i = 0; i < floatTypes.size(); i++) {
        assertNear(actualView.floatValue(floatTypes[i]),
                expectedView.floatValue(floatTypes[i]), FLOAT_TOLERANCE,
                context + ".floatValue[" + std::to_string(i) + "]");
    }
}

void assertEnvironmentEquivalent(const Environment& actual,
        const Environment& expected,
        const std::string& context) {
    assertEqual(actual.mIsPreviewMode, expected.mIsPreviewMode, context + ".preview");
    const std::array<std::pair<double, double>, 35> doubles{{{actual.mLightAngle, expected.mLightAngle},
            {actual.mLightAngleCos, expected.mLightAngleCos},
            {actual.mLightAngleSin, expected.mLightAngleSin},
            {actual.mFoodSpeed, expected.mFoodSpeed},
            {actual.mFoodSize, expected.mFoodSize},
            {actual.mRadiation, expected.mRadiation},
            {actual.mLightIntensity, expected.mLightIntensity},
            {actual.mLightSpeed, expected.mLightSpeed},
            {actual.mLightRange, expected.mLightRange},
            {actual.mViscosity, expected.mViscosity},
            {actual.mDensity, expected.mDensity},
            {actual.mGravity, expected.mGravity},
            {actual.mDensityGradient, expected.mDensityGradient},
            {actual.mGravityFieldStrength, expected.mGravityFieldStrength},
            {actual.mGravityFieldAngle, expected.mGravityFieldAngle},
            {actual.mDensityFieldBase, expected.mDensityFieldBase},
            {actual.mDensityFieldStrength, expected.mDensityFieldStrength},
            {actual.mDensityFieldAngle, expected.mDensityFieldAngle},
            {actual.mDensityFieldOffset, expected.mDensityFieldOffset},
            {actual.mDensityFieldSoftness, expected.mDensityFieldSoftness},
            {actual.mSalinityFieldBase, expected.mSalinityFieldBase},
            {actual.mSalinityFieldStrength, expected.mSalinityFieldStrength},
            {actual.mSalinityFieldAngle, expected.mSalinityFieldAngle},
            {actual.mSalinityFieldOffset, expected.mSalinityFieldOffset},
            {actual.mSalinityFieldSoftness, expected.mSalinityFieldSoftness},
            {actual.mFoodLipidLayerFieldBase, expected.mFoodLipidLayerFieldBase},
            {actual.mFoodLipidLayerFieldStrength, expected.mFoodLipidLayerFieldStrength},
            {actual.mFoodLipidLayerFieldAngle, expected.mFoodLipidLayerFieldAngle},
            {actual.mFoodLipidLayerFieldOffset, expected.mFoodLipidLayerFieldOffset},
            {actual.mFoodLipidLayerFieldSoftness, expected.mFoodLipidLayerFieldSoftness},
            {actual.mRadius, expected.mRadius},
            {actual.mTestMulticellularCoef, expected.mTestMulticellularCoef},
            {actual.mTestMutationCoef, expected.mTestMutationCoef},
            {actual.mTestFoodDecay, expected.mTestFoodDecay},
            {actual.mFrictionStatic, expected.mFrictionStatic}}};
    for (size_t i = 0; i < doubles.size(); i++) {
        assertNear(doubles[i].first, doubles[i].second, DOUBLE_TOLERANCE,
                context + ".double[" + std::to_string(i) + "]");
    }
    const std::array<std::pair<double, double>, 7> remainingDoubles{{{actual.mFrictionDynamic, expected.mFrictionDynamic},
            {actual.mFoodLumpiness, expected.mFoodLumpiness},
            {actual.mFoodLumpSize, expected.mFoodLumpSize},
            {actual.mNitrates, expected.mNitrates},
            {actual.mTestIdleMetabolismScale, expected.mTestIdleMetabolismScale},
            {actual.mTestFlagellocyteCostScale, expected.mTestFlagellocyteCostScale},
            {actual.mTestPhagocyteEatSpeedScale, expected.mTestPhagocyteEatSpeedScale}}};
    for (size_t i = 0; i < remainingDoubles.size(); i++) {
        assertNear(remainingDoubles[i].first, remainingDoubles[i].second, DOUBLE_TOLERANCE,
                context + ".remainingDouble[" + std::to_string(i) + "]");
    }
    assertNear(actual.mSalinity, expected.mSalinity, FLOAT_TOLERANCE, context + ".salinity");
    assertNear(actual.mFoodLipidLayer, expected.mFoodLipidLayer, FLOAT_TOLERANCE,
            context + ".foodLipidLayer");
    assertEqual(actual.mRandom.getSeed(), expected.mRandom.getSeed(), context + ".randomSeed");
    assertEqual(actual.mGravityFieldType, expected.mGravityFieldType, context + ".gravityFieldType");
    assertEqual(actual.mDensityFieldType, expected.mDensityFieldType, context + ".densityFieldType");
    assertEqual(actual.mSalinityFieldType, expected.mSalinityFieldType, context + ".salinityFieldType");
    assertEqual(actual.mFoodLipidLayerFieldType, expected.mFoodLipidLayerFieldType,
            context + ".foodLipidFieldType");
    assertEqual(actual.maxCellCount, expected.maxCellCount, context + ".maxCellCount");
    assertEqual(actual.maxFoodCount, expected.maxFoodCount, context + ".maxFoodCount");
    assertEqual(actual.maxDeadSplashCount, expected.maxDeadSplashCount,
            context + ".maxDeadSplashCount");
    assertEqual(actual.IsOnlyTweakMutations, expected.IsOnlyTweakMutations,
            context + ".onlyTweakMutations");
    assertEqual(actual.mutationOnlyAfterSplit, expected.mutationOnlyAfterSplit,
            context + ".mutationOnlyAfterSplit");
    assertEqual(actual.aging, expected.aging, context + ".aging");
    assertEqual(actual.addRandom, expected.addRandom, context + ".addRandom");
    assertEqual(actual.dieEdge, expected.dieEdge, context + ".dieEdge");
    assertEqual(actual.mobileFood, expected.mobileFood, context + ".mobileFood");
    for (size_t i = 0; i < actual.mGenePool.size(); i++) {
        assertEqual(actual.mGenePool[i], expected.mGenePool[i],
                context + ".genePool[" + std::to_string(i) + "]");
    }
    for (size_t i = 0; i < actual.mMutationWeights.size(); i++) {
        assertNear(actual.mMutationWeights[i], expected.mMutationWeights[i], DOUBLE_TOLERANCE,
                context + ".mutationWeight[" + std::to_string(i) + "]");
    }
}

void assertTopologyEquivalent(const CellTopology& actual,
        const CellTopology& expected,
        const std::string& context) {
    assertEqual(actual.edgeCount(), expected.edgeCount(), context + ".edgeCount");
    assertEqual(actual.boundaryCount(), expected.boundaryCount(), context + ".boundaryCount");
    for (int i = 0; i < actual.edgeCount(); i++) {
        assertEqual(actual.edgeCellA(i), expected.edgeCellA(i), context + ".edgeCellA");
        assertEqual(actual.edgeCellB(i), expected.edgeCellB(i), context + ".edgeCellB");
        assertEqual(
                actual.edgeCreationReason(i),
                expected.edgeCreationReason(i),
                context + ".edgeCreationReason");
        assertNear(actual.edgeAngleA(i), expected.edgeAngleA(i), DOUBLE_TOLERANCE,
                context + ".edgeAngleA");
        assertNear(actual.edgeAngleB(i), expected.edgeAngleB(i), DOUBLE_TOLERANCE,
                context + ".edgeAngleB");
        assertNear(actual.edgeTorsionalStiffness(i), expected.edgeTorsionalStiffness(i),
                FLOAT_TOLERANCE, context + ".edgeStiffness");
        assertNear(actual.edgeRestLength(i), expected.edgeRestLength(i), FLOAT_TOLERANCE,
                context + ".edgeRestLength");
        assertNear(actual.edgeRenderStartOffsetAX(i), expected.edgeRenderStartOffsetAX(i),
                FLOAT_TOLERANCE, context + ".edgeRenderStartX");
        assertNear(actual.edgeRenderStartOffsetAY(i), expected.edgeRenderStartOffsetAY(i),
                FLOAT_TOLERANCE, context + ".edgeRenderStartY");
        assertNear(actual.edgeRenderEndOffsetAX(i), expected.edgeRenderEndOffsetAX(i),
                FLOAT_TOLERANCE, context + ".edgeRenderEndX");
        assertNear(actual.edgeRenderEndOffsetAY(i), expected.edgeRenderEndOffsetAY(i),
                FLOAT_TOLERANCE, context + ".edgeRenderEndY");
    }
    for (int i = 0; i < actual.boundaryCount(); i++) {
        assertEqual(actual.boundaryCell(i), expected.boundaryCell(i), context + ".boundaryCell");
        assertNear(actual.boundaryAngleA(i), expected.boundaryAngleA(i), DOUBLE_TOLERANCE,
                context + ".boundaryAngle");
        assertNear(actual.boundaryAnchorX(i), expected.boundaryAnchorX(i), DOUBLE_TOLERANCE,
                context + ".boundaryAnchorX");
        assertNear(actual.boundaryAnchorY(i), expected.boundaryAnchorY(i), DOUBLE_TOLERANCE,
                context + ".boundaryAnchorY");
        assertNear(actual.boundaryTorsionalStiffness(i),
                expected.boundaryTorsionalStiffness(i), FLOAT_TOLERANCE,
                context + ".boundaryStiffness");
        assertNear(actual.boundaryRestLength(i), expected.boundaryRestLength(i),
                FLOAT_TOLERANCE, context + ".boundaryRestLength");
        assertNear(actual.boundaryRenderStartOffsetX(i),
                expected.boundaryRenderStartOffsetX(i), FLOAT_TOLERANCE,
                context + ".boundaryRenderX");
        assertNear(actual.boundaryRenderStartOffsetY(i),
                expected.boundaryRenderStartOffsetY(i), FLOAT_TOLERANCE,
                context + ".boundaryRenderY");
    }
}

void assertWorldEquivalent(const SimulationWorld& actual,
        const SimulationWorld& expected,
        const std::string& context) {
    assertEnvironmentEquivalent(actual.mEnvironment, expected.mEnvironment, context + ".environment");
    assertNear(actual.mTime, expected.mTime, DOUBLE_TOLERANCE, context + ".time");
    const CellData& a = actual.mCellData;
    const CellData& e = expected.mCellData;
    assertEqual(a.mCellCount, e.mCellCount, context + ".cellCount");
    assertEqual(a.nextOrganismId(), e.nextOrganismId(), context + ".nextOrganismId");
    for (int cell = 0; cell < a.mCellCount; cell++) {
        const std::array<std::pair<double, double>, 22> doubles{{{a.mPositionX[cell], e.mPositionX[cell]},
                {a.mPositionY[cell], e.mPositionY[cell]},
                {a.mPositionZ[cell], e.mPositionZ[cell]},
                {a.mRadius[cell], e.mRadius[cell]},
                {a.mVelocityX[cell], e.mVelocityX[cell]},
                {a.mVelocityY[cell], e.mVelocityY[cell]},
                {a.mForceX[cell], e.mForceX[cell]},
                {a.mForceY[cell], e.mForceY[cell]},
                {a.mForceZ[cell], e.mForceZ[cell]},
                {a.mTorque[cell], e.mTorque[cell]},
                {a.mAngle[cell], e.mAngle[cell]},
                {a.mAngleCytoplasm[cell], e.mAngleCytoplasm[cell]},
                {a.mAngularVelocity[cell], e.mAngularVelocity[cell]},
                {a.mAngularVelocityCytoplasm[cell], e.mAngularVelocityCytoplasm[cell]},
                {a.mAge[cell], e.mAge[cell]},
                {a.mNitrates[cell], e.mNitrates[cell]},
                {a.mHairRotation[cell], e.mHairRotation[cell]},
                {a.hairX[cell], e.hairX[cell]},
                {a.hairY[cell], e.hairY[cell]},
                {a.mMass[cell], e.mMass[cell]},
                {a.mNewMass[cell], e.mNewMass[cell]},
                {a.mNewNitrates[cell], e.mNewNitrates[cell]}}};
        for (size_t i = 0; i < doubles.size(); i++) {
            assertNear(doubles[i].first, doubles[i].second, DOUBLE_TOLERANCE,
                    context + ".cell[" + std::to_string(cell) + "].double["
                            + std::to_string(i) + "]");
        }
        assertNear(a.mRadiusSq[cell], e.mRadiusSq[cell], DOUBLE_TOLERANCE,
                context + ".radiusSq");
        assertNear(a.mSwimAnimationTime[cell], e.mSwimAnimationTime[cell], FLOAT_TOLERANCE,
                context + ".swimAnimationTime");
        assertNear(a.mSwimAnimationTime2[cell], e.mSwimAnimationTime2[cell], FLOAT_TOLERANCE,
                context + ".swimAnimationTime2");
        assertEqual(a.mIsDead[cell], e.mIsDead[cell], context + ".isDead");
        assertEqual(a.mIsMirrored[cell], e.mIsMirrored[cell], context + ".isMirrored");
        assertEqual(a.mIsStuck[cell], e.mIsStuck[cell], context + ".isStuck");
        assertEqual(
                a.activeGeneIndex(cell).value(),
                e.activeGeneIndex(cell).value(),
                context + ".geneIndex");
        assertEqual(a.mOriginTag[cell], e.mOriginTag[cell], context + ".originTag");
        assertEqual(a.mOrganismIds[cell], e.mOrganismIds[cell], context + ".organismId");
        assertEqual(a.mParentOrganismIds[cell], e.mParentOrganismIds[cell], context + ".parentOrganismId");
        assertEqual(a.mSelectTag[cell], e.mSelectTag[cell], context + ".selectTag");
        assertEqual(a.mMutationCount[cell], e.mMutationCount[cell], context + ".mutationCount");
        assertEqual(a.mPendingMutationCount[cell], e.mPendingMutationCount[cell],
                context + ".pendingMutationCount");
        assertEqual(a.mSeldomCounter[cell], e.mSeldomCounter[cell], context + ".seldomCounter");
        assertEqual(a.mOldLinkCount[cell], e.mOldLinkCount[cell], context + ".oldLinkCount");
        assertNear(a.mLipids[cell], e.mLipids[cell], FLOAT_TOLERANCE, context + ".lipids");
        assertEqual(a.mTelomeresCount[cell], e.mTelomeresCount[cell], context + ".telomeres");
        assertEqual(a.mFertilizationTarget[cell], e.mFertilizationTarget[cell],
                context + ".fertilizationTarget");
        assertEqual(CellTypes::ordinal(a.mActiveCellType[cell]),
                CellTypes::ordinal(e.mActiveCellType[cell]), context + ".activeCellType");
        for (size_t i = 0; i < a.mColor[cell].size(); i++) {
            assertNear(a.mColor[cell][i], e.mColor[cell][i], FLOAT_TOLERANCE,
                    context + ".color");
        }
        const cellularity::GenomeData actualGenome =
                a.materializeGenome(cell);
        const cellularity::GenomeData expectedGenome =
                e.materializeGenome(cell);
        assertTrue(actualGenome.size() == expectedGenome.size(),
                "Round-tripped Cell Genome length matches");
        for (std::size_t gene = 0; gene < actualGenome.size(); gene++) {
            assertGeneEquivalent(actualGenome[gene], expectedGenome[gene],
                    context + ".gene[" + std::to_string(cell) + "]["
                            + std::to_string(gene) + "]");
        }
        for (size_t i = 0; i < a.mSignalSubstance[cell].size(); i++) {
            assertNear(a.mSignalSubstance[cell][i], e.mSignalSubstance[cell][i],
                    FLOAT_TOLERANCE, context + ".signalSubstance");
        }
        for (size_t i = 0; i < a.mNewSignalSubstance[cell].size(); i++) {
            assertNear(a.mNewSignalSubstance[cell][i], e.mNewSignalSubstance[cell][i],
                    FLOAT_TOLERANCE, context + ".newSignalSubstance");
            assertNear(a.mSignalSubstanceOut[cell][i], e.mSignalSubstanceOut[cell][i],
                    FLOAT_TOLERANCE, context + ".signalSubstanceOut");
        }
        const std::array<std::pair<float, float>, 6> floats{{{a.mToxin[cell], e.mToxin[cell]},
                {a.mToxication[cell], e.mToxication[cell]},
                {a.mNewToxin[cell], e.mNewToxin[cell]},
                {a.mMyocyteContraction[cell], e.mMyocyteContraction[cell]},
                {a.mMyocyteBending[cell], e.mMyocyteBending[cell]},
                {a.mMyocyteLifting[cell], e.mMyocyteLifting[cell]}}};
        for (size_t i = 0; i < floats.size(); i++) {
            assertNear(floats[i].first, floats[i].second, FLOAT_TOLERANCE,
                    context + ".cellFloat[" + std::to_string(i) + "]");
        }
    }
    assertTopologyEquivalent(a.mTopology, e.mTopology, context + ".topology");

    assertEqual(actual.mFoodData.mFoodCount, expected.mFoodData.mFoodCount,
            context + ".foodCount");
    for (int i = 0; i < actual.mFoodData.mFoodCount; i++) {
        assertNear(actual.mFoodData.mPositionX[i], expected.mFoodData.mPositionX[i],
                FLOAT_TOLERANCE, context + ".foodX");
        assertNear(actual.mFoodData.mPositionY[i], expected.mFoodData.mPositionY[i],
                FLOAT_TOLERANCE, context + ".foodY");
        assertNear(actual.mFoodData.mMass[i], expected.mFoodData.mMass[i],
                FLOAT_TOLERANCE, context + ".foodMass");
        assertNear(actual.mFoodData.mVelocityX[i], expected.mFoodData.mVelocityX[i],
                FLOAT_TOLERANCE, context + ".foodVelocityX");
        assertNear(actual.mFoodData.mVelocityY[i], expected.mFoodData.mVelocityY[i],
                FLOAT_TOLERANCE, context + ".foodVelocityY");
        assertNear(actual.mFoodData.mLipidLayer[i], expected.mFoodData.mLipidLayer[i],
                FLOAT_TOLERANCE, context + ".foodLipidLayer");
    }
    assertEqual(actual.mDeadSplashData.mDeadSplashCount,
            expected.mDeadSplashData.mDeadSplashCount, context + ".deadSplashCount");
    for (int i = 0; i < actual.mDeadSplashData.mDeadSplashCount; i++) {
        assertNear(actual.mDeadSplashData.mPositionX[i], expected.mDeadSplashData.mPositionX[i],
                FLOAT_TOLERANCE, context + ".deadSplashX");
        assertNear(actual.mDeadSplashData.mPositionY[i], expected.mDeadSplashData.mPositionY[i],
                FLOAT_TOLERANCE, context + ".deadSplashY");
        assertNear(actual.mDeadSplashData.mRadius[i], expected.mDeadSplashData.mRadius[i],
                FLOAT_TOLERANCE, context + ".deadSplashRadius");
        assertNear(actual.mDeadSplashData.mAge[i], expected.mDeadSplashData.mAge[i],
                FLOAT_TOLERANCE, context + ".deadSplashAge");
        for (size_t color = 0; color < actual.mDeadSplashData.mColor[i].size(); color++) {
            assertNear(actual.mDeadSplashData.mColor[i][color],
                    expected.mDeadSplashData.mColor[i][color], FLOAT_TOLERANCE,
                    context + ".deadSplashColor");
        }
    }
}

void assertLegacyEnvironment(const Environment& environment,
        const cellularity::test_fixtures::FixtureSpec& spec) {
    const int version = spec.environmentVersion;
    const Environment fallback = legacyFallbackEnvironment();
    assertNear(environment.mFoodSpeed, version == 1 ? 0.03 : 0.2, DOUBLE_TOLERANCE,
            spec.id + ".environment.foodSpeed");
    assertNear(environment.mFoodSize, version >= 2 ? 0.125 : fallback.mFoodSize,
            DOUBLE_TOLERANCE, spec.id + ".environment.foodSize");
    assertNear(environment.mRadiation, 0.01, DOUBLE_TOLERANCE, spec.id + ".environment.radiation");
    assertNear(environment.mLightIntensity, 0.8, DOUBLE_TOLERANCE,
            spec.id + ".environment.lightIntensity");
    assertNear(environment.mLightSpeed, 0.02, DOUBLE_TOLERANCE,
            spec.id + ".environment.lightSpeed");
    assertNear(environment.mLightRange, 0.4, DOUBLE_TOLERANCE,
            spec.id + ".environment.lightRange");
    assertNear(environment.mViscosity, 60.0, DOUBLE_TOLERANCE,
            spec.id + ".environment.viscosity");
    assertTrue(environment.mGenePool[0] && !environment.mGenePool[1],
            spec.id + ".environment.genePool prefix");
    assertNear(environment.mGravity, 0.05, DOUBLE_TOLERANCE, spec.id + ".environment.gravity");
    assertNear(environment.mDensity, 0.1, DOUBLE_TOLERANCE, spec.id + ".environment.density");
    assertNear(environment.mDensityGradient, -0.2, DOUBLE_TOLERANCE,
            spec.id + ".environment.densityGradient");
    assertNear(environment.mNitrates, version >= 21 ? 0.75 : 1.0, DOUBLE_TOLERANCE,
            spec.id + ".environment.nitrates");
    assertEqual(environment.maxCellCount, 8, spec.id + ".environment.maxCellCount");
    assertEqual(environment.maxFoodCount, 8, spec.id + ".environment.maxFoodCount");
    assertEqual(environment.maxDeadSplashCount, 2, spec.id + ".environment.maxDeadSplashCount");
    assertNear(environment.mFrictionDynamic, version >= 9 ? 0.3 : fallback.mFrictionDynamic,
            DOUBLE_TOLERANCE, spec.id + ".environment.frictionDynamic");
    assertNear(environment.mFrictionStatic, version >= 9 ? 0.4 : fallback.mFrictionStatic,
            DOUBLE_TOLERANCE, spec.id + ".environment.frictionStatic");
    assertEqual(environment.IsOnlyTweakMutations, version >= 9,
            spec.id + ".environment.onlyTweakMutations");
    assertEqual(environment.mutationOnlyAfterSplit, version >= 97,
            spec.id + ".environment.mutationOnlyAfterSplit");
    assertNear(environment.mSalinity, version >= 10 ? 0.25 : fallback.mSalinity,
            FLOAT_TOLERANCE, spec.id + ".environment.salinity");
    assertEqual(environment.aging, version >= 14, spec.id + ".environment.aging");
    assertNear(environment.mFoodLumpiness,
            version >= 14 ? 0.2 : fallback.mFoodLumpiness,
            DOUBLE_TOLERANCE, spec.id + ".environment.foodLumpiness");
    assertNear(environment.mFoodLumpSize,
            version >= 14 ? 0.03 : fallback.mFoodLumpSize,
            DOUBLE_TOLERANCE, spec.id + ".environment.foodLumpSize");
    assertEqual(environment.mobileFood, version >= 22, spec.id + ".environment.mobileFood");
    assertNear(environment.mFoodLipidLayer,
            version >= 22 ? 0.35 : fallback.mFoodLipidLayer,
            FLOAT_TOLERANCE, spec.id + ".environment.foodLipidLayer");

    int mutationWeightCount = 0;
    if (version >= 98) {
        mutationWeightCount = spec.id.find("weights-short") != std::string::npos
                ? 1
                : spec.id.find("weights-excess") != std::string::npos ? 128
                                                                      : 3;
    }
    for (int i = 0; i < Environment::MUTATION_WEIGHT_COUNT; i++) {
        double expected = 0.5;
        if (i == Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX
                || i == Environment::MUTATION_DELETE_GENE_WEIGHT_INDEX) {
            expected = 0.0;
        } else if (i < mutationWeightCount
                && i < Environment::MUTATION_GENOME_LENGTH_WEIGHT_OFFSET) {
            expected = 1.0 + i / 100.0;
        }
        assertNear(environment.mMutationWeights[i], expected, DOUBLE_TOLERANCE,
                spec.id + ".environment.mutationWeight[" + std::to_string(i) + "]");
    }
    if (version >= 99) {
        assertEqual(environment.mDensityFieldType, 2, spec.id + ".densityFieldType");
        assertNear(environment.mDensityFieldBase, 0.15, DOUBLE_TOLERANCE,
                spec.id + ".densityFieldBase");
        assertNear(environment.mDensityFieldSoftness, 0.3, DOUBLE_TOLERANCE,
                spec.id + ".densityFieldSoftness");
    } else {
        assertEqual(environment.mDensityFieldType, Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL,
                spec.id + ".synthesizedDensityFieldType");
        assertNear(environment.mDensityFieldBase, 0.1, DOUBLE_TOLERANCE,
                spec.id + ".synthesizedDensityFieldBase");
        assertNear(environment.mDensityFieldSoftness, 0.0, DOUBLE_TOLERANCE,
                spec.id + ".synthesizedDensityFieldSoftness");
    }
    if (version >= 100) {
        assertEqual(environment.mSalinityFieldType, 1, spec.id + ".salinityFieldType");
        assertNear(environment.mSalinityFieldBase, 0.26, DOUBLE_TOLERANCE,
                spec.id + ".salinityFieldBase");
        assertEqual(environment.mFoodLipidLayerFieldType, 2, spec.id + ".lipidFieldType");
        assertNear(environment.mFoodLipidLayerFieldBase, 0.36, DOUBLE_TOLERANCE,
                spec.id + ".lipidFieldBase");
    } else {
        assertNear(environment.mSalinityFieldBase, environment.mSalinity, FLOAT_TOLERANCE,
                spec.id + ".synthesizedSalinityFieldBase");
        assertNear(environment.mFoodLipidLayerFieldBase, environment.mFoodLipidLayer,
                FLOAT_TOLERANCE, spec.id + ".synthesizedLipidFieldBase");
    }
    if (version >= 101) {
        assertEqual(environment.mGravityFieldType, 2, spec.id + ".gravityFieldType");
        assertNear(environment.mGravityFieldStrength, 0.07, DOUBLE_TOLERANCE,
                spec.id + ".gravityFieldStrength");
        assertNear(environment.mGravityFieldAngle, 0.4, DOUBLE_TOLERANCE,
                spec.id + ".gravityFieldAngle");
    } else {
        assertEqual(environment.mGravityFieldType, Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL,
                spec.id + ".synthesizedGravityFieldType");
        assertNear(environment.mGravityFieldStrength, 0.05, DOUBLE_TOLERANCE,
                spec.id + ".synthesizedGravityFieldStrength");
    }
    assertNear(environment.mTestIdleMetabolismScale,
            version >= 102 ? 0.9 : fallback.mTestIdleMetabolismScale,
            DOUBLE_TOLERANCE, spec.id + ".idleMetabolismScale");
    assertNear(environment.mTestFlagellocyteCostScale,
            version >= 102 ? 1.1 : fallback.mTestFlagellocyteCostScale,
            DOUBLE_TOLERANCE, spec.id + ".flagellocyteCostScale");
    assertNear(environment.mTestPhagocyteEatSpeedScale,
            version >= 102 ? 1.2 : fallback.mTestPhagocyteEatSpeedScale,
            DOUBLE_TOLERANCE, spec.id + ".phagocyteEatSpeedScale");
}

void assertLegacyGene(const Gene& gene, int version, const std::string& context) {
    const GeneView view(gene);
    assertNear(view.color(0), 0.2, FLOAT_TOLERANCE, context + ".colorR");
    assertNear(view.color(1), 0.4, FLOAT_TOLERANCE, context + ".colorG");
    assertNear(view.color(2), 0.6, FLOAT_TOLERANCE, context + ".colorB");
    assertNear(view.splitMass(), 0.12, FLOAT_TOLERANCE, context + ".splitMass");
    assertNear(view.splitRatio(), version == 7 || version == 8 ? 0.6 : 0.4,
            FLOAT_TOLERANCE, context + ".splitRatio");
    const double expectedSplitAngle = version < 9 ? 0.1 + Environment::PI : 0.1;
    const double expectedChild1Angle = version == 5
            ? 0.2
            : version < 9 ? 0.2 + Environment::PI
                          : 0.2;
    const double expectedChild2Angle = version < 9 ? 0.3 + Environment::PI : 0.3;
    assertNear(view.splitAngle(), expectedSplitAngle, FLOAT_TOLERANCE, context + ".splitAngle");
    assertNear(view.child1Angle(), expectedChild1Angle, FLOAT_TOLERANCE, context + ".child1Angle");
    assertNear(view.child2Angle(), expectedChild2Angle, FLOAT_TOLERANCE, context + ".child2Angle");
    assertNear(view.linkStrength(), version >= 4 ? 7.0 : 0.0, FLOAT_TOLERANCE,
            context + ".linkStrength");
    assertEqual(CellTypes::ordinal(view.cellType()), 0, context + ".cellType");
    assertEqual(view.integerValue(Gene::MAX_CONNECTION_COUNT_INDEX),
            version < 9 ? CellData::MAX_NEIGHBORS - 1 : version < 15 ? 18
                                                                     : 5,
            context + ".maxConnections");
    assertEqual(view.integerValue(Gene::SECRETE_TYPE_INTEGER_INDEX), version == 10 ? 1 : 0,
            context + ".secreteType");
    assertNear(view.floatValue(Gene::FloatType::ADHESIN_LENGTH), version >= 16 ? 0.01 : 0.0,
            FLOAT_TOLERANCE, context + ".adhesinLength");
    assertNear(view.floatValue(Gene::FloatType::CYTOSKELETON), version >= 14 ? 1.25 : 1.0,
            FLOAT_TOLERANCE, context + ".cytoskeleton");
    if (version >= 9) {
        assertEqual(view.parametricFormulaType(0), version >= 17 ? 0 : 1,
                context + ".formulaType");
        assertNear(view.floatValue(Gene::FloatType::SMELL_AMPLITUDE), 0.1, FLOAT_TOLERANCE,
                context + ".smellAmplitude");
    }
}

void assertLegacyWorld(const SimulationWorld& world,
        const cellularity::test_fixtures::FixtureSpec& spec) {
    const auto declared = semanticValues(spec);
    assertNear(world.mTime, semanticDouble(spec, declared, "time"), DOUBLE_TOLERANCE,
            spec.id + ".declaredTime");
    assertNear(world.mEnvironment.mLightAngle,
            semanticDouble(spec, declared, "light_angle"), DOUBLE_TOLERANCE,
            spec.id + ".declaredLightAngle");
    assertEqual(world.mEnvironment.mRandom.getSeed(),
            semanticLong(spec, declared, "random_seed"), spec.id + ".declaredRandomSeed");
    assertEqual(world.mCellData.mCellCount,
            static_cast<int>(semanticLong(spec, declared, "cell_count")),
            spec.id + ".declaredCellCount");
    assertEqual(world.mFoodData.mFoodCount,
            static_cast<int>(semanticLong(spec, declared, "food_count")),
            spec.id + ".declaredFoodCount");
    assertEqual(world.mCellData.mTopology.boundaryCount(),
            static_cast<int>(semanticLong(spec, declared, "topology_boundary_count")),
            spec.id + ".declaredBoundaryCount");
    assertNear(world.mEnvironment.mLightAngleCos, std::cos(world.mEnvironment.mLightAngle),
            DOUBLE_TOLERANCE, spec.id + ".lightCos");
    assertNear(world.mEnvironment.mLightAngleSin, std::sin(world.mEnvironment.mLightAngle),
            DOUBLE_TOLERANCE, spec.id + ".lightSin");
    assertLegacyEnvironment(world.mEnvironment, spec);

    for (int cellIndex = 0;
            cellIndex < world.mCellData.mCellCount;
            ++cellIndex) {
        assertEqual(
                world.mCellData.organismId(cellIndex),
                kUnknownOrganismId,
                spec.id + ".cell.organismIdentityUnknown");
        assertEqual(
                world.mCellData.parentOrganismId(cellIndex),
                kUnknownOrganismId,
                spec.id + ".cell.parentOrganismIdentityUnknown");
    }
    const CellData::OrganismIdentitySummary organismSummary =
            world.mCellData.organismIdentitySummary();
    assertEqual(
            organismSummary.knownOrganismCount,
            std::uint64_t{0},
            spec.id + ".knownOrganismCount");
    assertEqual(
            organismSummary.complete,
            world.mCellData.mCellCount == 0,
            spec.id + ".organismIdentityComplete");
    assertEqual(
            world.mCellData.nextOrganismId(),
            OrganismId{1},
            spec.id + ".nextOrganismIdentity");

    if (spec.cellCount > 0) {
        const CellData& cellData = world.mCellData;
        assertNear(cellData.mPositionX[0], 0.1, DOUBLE_TOLERANCE, spec.id + ".cell.positionX");
        assertNear(cellData.mPositionY[0], -0.1, DOUBLE_TOLERANCE, spec.id + ".cell.positionY");
        assertNear(cellData.mPositionZ[0], spec.cellVersion >= 23 ? 0.005 : 0.0,
                DOUBLE_TOLERANCE, spec.id + ".cell.positionZ");
        assertNear(cellData.mRadius[0], 0.03, DOUBLE_TOLERANCE, spec.id + ".cell.radius");
        assertNear(cellData.mRadiusSq[0], 0.0009, DOUBLE_TOLERANCE, spec.id + ".cell.radiusSq");
        assertNear(cellData.mMass[0], 0.2, DOUBLE_TOLERANCE, spec.id + ".cell.mass");
        assertNear(cellData.mNewMass[0], 0.2, DOUBLE_TOLERANCE, spec.id + ".cell.newMass");
        assertNear(cellData.mNitrates[0], 0.8, DOUBLE_TOLERANCE, spec.id + ".cell.nitrates");
        assertNear(cellData.mNewNitrates[0], 0.8, DOUBLE_TOLERANCE,
                spec.id + ".cell.newNitrates");
        assertEqual(cellData.mIsMirrored[0], spec.cellVersion >= 4, spec.id + ".cell.mirrored");
        assertEqual(cellData.mIsStuck[0], static_cast<uint8_t>(0), spec.id + ".cell.stuck");
        assertNear(cellData.mLipids[0], spec.cellVersion >= 12 ? 0.06 : 0.0,
                FLOAT_TOLERANCE, spec.id + ".cell.lipids");
        assertEqual(cellData.mMutationCount[0], spec.cellVersion >= 15 ? 3 : 0,
                spec.id + ".cell.mutationCount");
        assertEqual(cellData.mTelomeresCount[0], spec.cellVersion >= 19 ? 17 : -1,
                spec.id + ".cell.telomeres");
        assertEqual(cellData.mFertilizationTarget[0], -1, spec.id + ".cell.fertilizationTarget");
        assertEqual(cellData.mTopology.edgeCount(), 0, spec.id + ".topology.edgeCount");
        assertEqual(cellData.mTopology.boundaryCount(), 1, spec.id + ".topology.boundaryCount");
        assertEqual(cellData.mTopology.boundaryCell(0), 0, spec.id + ".topology.boundaryCell");
        assertNear(cellData.mTopology.boundaryAngleA(0), 0.2, DOUBLE_TOLERANCE,
                spec.id + ".topology.boundaryAngle");
        assertNear(cellData.mTopology.boundaryAnchorX(0), 0.5, DOUBLE_TOLERANCE,
                spec.id + ".topology.boundaryX");
        assertNear(cellData.mTopology.boundaryAnchorY(0), -0.25, DOUBLE_TOLERANCE,
                spec.id + ".topology.boundaryY");
        assertNear(cellData.mTopology.boundaryTorsionalStiffness(0),
                spec.cellVersion >= 4 ? 0.5 : 0.0, FLOAT_TOLERANCE,
                spec.id + ".topology.boundaryStiffness");
        assertNear(cellData.mTopology.boundaryRestLength(0),
                spec.cellVersion >= 16 ? 0.02 : 0.0, FLOAT_TOLERANCE,
                spec.id + ".topology.boundaryRestLength");
        const cellularity::GenomeData genome = cellData.materializeGenome(0);
        assertLegacyGene(genome[0], spec.geneVersion, spec.id + ".gene0");
        int initialCount = 0;
        for (const Gene& gene : genome) {
            initialCount += GeneView(gene).initialCompatibilityFlag() ? 1 : 0;
        }
        assertEqual(initialCount, 1, spec.id + ".gene.initialCount");
    }

    if (spec.foodCount > 0) {
        const FoodData& food = world.mFoodData;
        assertNear(food.mPositionX[0], 0.2, FLOAT_TOLERANCE, spec.id + ".food.positionX");
        assertNear(food.mPositionY[0], -0.3, FLOAT_TOLERANCE, spec.id + ".food.positionY");
        assertNear(food.mMass[0], 0.04, FLOAT_TOLERANCE, spec.id + ".food.mass");
        assertNear(food.mVelocityX[0], spec.outerVersion >= 22 ? 0.01 : 0.0,
                FLOAT_TOLERANCE, spec.id + ".food.velocityX");
        assertNear(food.mVelocityY[0], spec.outerVersion >= 22 ? -0.02 : 0.0,
                FLOAT_TOLERANCE, spec.id + ".food.velocityY");
        assertNear(food.mLipidLayer[0], spec.outerVersion >= 22 ? 0.3 : 0.0,
                FLOAT_TOLERANCE, spec.id + ".food.lipidLayer");
    }
}

void testLegacySubstrateCorpusAndMigration() {
    const auto& specs = cellularity::test_fixtures::formatFixtureSpecs();
    int tested = 0;
    for (const auto& spec : specs) {
        if (spec.family != "legacy-substrate" || spec.expected != "accept-native") {
            continue;
        }
        SimulationRandom::global.setSeed(0x12345678);
        int formatVersion = 0;
        Environment fallback = legacyFallbackEnvironment();
        fallback.mInnerRadius = 0.1;
        std::unique_ptr<SimulationWorld> loaded = decodeSubstrate(
                fixtureBytes(spec), fallback, &formatVersion);
        assertNear(0.0, loaded->mEnvironment.mInnerRadius, 0.0, "Legacy substrates never inherit a hole from the fallback environment");
        assertEqual(formatVersion, spec.outerVersion,
                spec.id + ".legacyFormatVersion");
        assertLegacyWorld(*loaded, spec);

        SimulationRandom::global.setSeed(0x12345678);
        auto direct = cellularity::snapshot_codec::
                loadLegacySubstrateBytesDirect(
                        fixtureBytes(spec),
                        false,
                        fallback);
        assertEqual(direct.productVersion,
                spec.outerVersion,
                spec.id + ".directLegacyVersion");
        assertWorldEquivalent(
                *direct.world, *loaded, spec.id + ".directLegacyAdapter");
        const auto release = cellularity::save_format::release::
                encodeReleaseSubstrate(*direct.world);
        std::unique_ptr<SimulationWorld> releaseReloaded =
                cellularity::save_format::release::decodeReleaseSubstrate(
                        release.bytes, false);
        assertEqual(
                cellularity::save_format::release::substrateSemanticDigest(
                        *releaseReloaded),
                cellularity::save_format::release::substrateSemanticDigest(
                        *direct.world),
                spec.id + ".releaseSemanticDigest");

        const std::vector<uint8_t> current =
                cellularity::save_format::release::encodeReleaseSubstrate(
                        *loaded)
                        .bytes;
        assertTrue(isReleaseFile(current),
                spec.id + ".migration.releaseMagic");
        std::unique_ptr<SimulationWorld> reloaded = decodeSubstrate(
                current, legacyFallbackEnvironment());
        assertWorldEquivalent(*reloaded, *loaded, spec.id + ".migrationRoundTrip");
        tested++;
    }
    assertEqual(tested, 36, "legacy substrate semantic fixture count");

    const auto inspectionSpec = fixtureById("legacy-substrate-v103");
    const std::vector<uint8_t> inspectionBytes = fixtureBytes(inspectionSpec);
    const NativeSubstrateInspection headerInspection = inspectSubstrateHeader(
            inspectionBytes, false, legacyFallbackEnvironment());
    assertEqual(headerInspection.familyLocalVersion, inspectionSpec.outerVersion,
            "legacy header inspection format version");
    assertTrue(
            headerInspection.formatFamily
                            == NativeSubstrateFormatFamily::OriginalJava
                    && headerInspection.loadSupported,
            "legacy header inspection compatibility");
    assertNear(headerInspection.time, 12.5, DOUBLE_TOLERANCE,
            "legacy header inspection time");
    assertEqual(headerInspection.cellCount, inspectionSpec.cellCount,
            "legacy header inspection cell count");
    assertEqual(headerInspection.environment.maxCellCount, 8,
            "legacy header inspection Environment");
    ScopedTemporaryFile inspectionFile("-legacy.substrate");
    inspectionFile.write(inspectionBytes);
    const NativeSubstrateInspection inspection = inspectSubstratePath(
            inspectionFile.path.string(), false, legacyFallbackEnvironment());
    assertEqual(inspection.familyLocalVersion, inspectionSpec.outerVersion,
            "legacy inspection format version");
    assertTrue(
            inspection.formatFamily
                            == NativeSubstrateFormatFamily::OriginalJava
                    && inspection.loadSupported,
            "legacy path inspection compatibility");
    assertNear(inspection.time, 12.5, DOUBLE_TOLERANCE,
            "legacy inspection time");
    assertEqual(inspection.cellCount, inspectionSpec.cellCount,
            "legacy inspection cell count");
    assertEqual(inspection.environment.maxCellCount, 8,
            "legacy inspection Environment");
}

std::map<std::string, std::string> legacyEffectiveStateDigests() {
    std::map<std::string, std::string> digests;
    for (const auto& spec : cellularity::test_fixtures::formatFixtureSpecs()) {
        if (spec.expected != "accept-native") {
            continue;
        }
        if (spec.family == "legacy-substrate") {
            SimulationRandom::global.setSeed(0x12345678);
            std::unique_ptr<SimulationWorld> loaded = decodeSubstrate(
                    fixtureBytes(spec), legacyFallbackEnvironment());
            digests.emplace(
                    spec.id,
                    cellularity::save_format::release::
                            substrateSemanticDigest(*loaded));
        } else if (spec.family == "legacy-genome") {
            const auto genome = cellularity::snapshot_codec::
                    decodeLegacyGenomeBytes(fixtureBytes(spec));
            assertTrue(genome.size() == CellData::MAX_GENES_COUNT,
                    spec.id + ".effectiveLegacyLength");
            const cellularity::GenomeData dynamicGenome(
                    genome.begin(), genome.end());
            digests.emplace(
                    spec.id,
                    cellularity::test_support::stableDigest(
                            writeGenomeDraft(dynamicGenome)));
        }
    }
    return digests;
}

void assertLegacyGenomeCanonicalizable(
        const cellularity::GenomeData& genome,
        const std::string& context) {
    const auto metadata = cellularity::extractGenomeMetadataFromCompatibilityFlags(
            genome.data(), genome.size());
    assertTrue(metadata.has_value(), context + ".metadata");
    for (std::size_t index = 0; index < genome.size(); index++) {
        const auto canonical = cellularity::canonicalizeGene(
                genome[index],
                cellularity::GeneIndex(
                        static_cast<cellularity::GeneIndex::Value>(index)),
                genome.size());
        assertTrue(canonical.hasValue(),
                context + ".canonical[" + std::to_string(index) + "]");
    }
}

void testAcceptedLegacyGenesEnterCanonicalBoundary() {
    int substrateCount = 0;
    int standaloneCount = 0;
    for (const auto& spec : cellularity::test_fixtures::formatFixtureSpecs()) {
        if (spec.expected != "accept-native") {
            continue;
        }
        if (spec.family == "legacy-substrate") {
            SimulationRandom::global.setSeed(0x12345678);
            const std::unique_ptr<SimulationWorld> loaded = decodeSubstrate(
                    fixtureBytes(spec), legacyFallbackEnvironment());
            for (int cellIndex = 0;
                    cellIndex < loaded->mCellData.mCellCount;
                    cellIndex++) {
                assertLegacyGenomeCanonicalizable(
                        loaded->mCellData.materializeGenome(cellIndex),
                        spec.id + ".cell[" + std::to_string(cellIndex) + "]");
            }
            substrateCount++;
        } else if (spec.family == "legacy-genome") {
            const auto genome = cellularity::snapshot_codec::
                    decodeLegacyGenomeBytes(fixtureBytes(spec));
            assertLegacyGenomeCanonicalizable(
                    cellularity::GenomeData(genome.begin(), genome.end()),
                    spec.id);
            standaloneCount++;
        }
    }
    assertEqual(substrateCount, 36, "canonical legacy substrate fixture count");
    assertEqual(standaloneCount, 6, "canonical legacy Genome fixture count");
}

std::map<std::string, std::string> readLegacyEffectiveStateOracle() {
    std::ifstream input(LEGACY_EFFECTIVE_STATE_ORACLE);
    if (!input) {
        fail("Could not open legacy effective-state oracle: "
                + LEGACY_EFFECTIVE_STATE_ORACLE.string());
    }

    std::map<std::string, std::string> expected;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        lineNumber++;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t separator = line.find('\t');
        if (separator == std::string::npos || line.find('\t', separator + 1) != std::string::npos) {
            fail("Malformed legacy effective-state oracle line "
                    + std::to_string(lineNumber));
        }
        const std::string id = line.substr(0, separator);
        const std::string digest = line.substr(separator + 1);
        if (id.empty() || digest.size() != 64 || !expected.emplace(id, digest).second) {
            fail("Invalid legacy effective-state oracle line "
                    + std::to_string(lineNumber));
        }
    }
    return expected;
}

void testLegacyEffectiveStateOracle() {
    const auto actual = legacyEffectiveStateDigests();
    const auto expected = readLegacyEffectiveStateOracle();
    assertEqual(actual.size(), static_cast<size_t>(42),
            "legacy effective-state fixture count");
    assertEqual(expected.size(), actual.size(),
            "legacy effective-state oracle entry count");
    for (const auto& [id, actualDigest] : actual) {
        const auto expectedDigest = expected.find(id);
        assertTrue(expectedDigest != expected.end(),
                "legacy effective-state oracle is missing " + id);
        assertEqual(actualDigest, expectedDigest->second,
                id + ".effectiveStateDigest");
    }
}

void printLegacyEffectiveStateDigests() {
    std::cout << "# G-09 oracle: legacy fixture id and SHA-256 of its canonical "
                 "dynamic effective state.\n";
    for (const auto& [id, digest] : legacyEffectiveStateDigests()) {
        std::cout << id << '\t' << digest << '\n';
    }
}

void testSemanticOracleRejectsChangedExpectedField() {
    cellularity::test_fixtures::FixtureSpec altered = fixtureById("legacy-substrate-v103");
    const std::string expected = "time=12.5";
    const size_t position = altered.semanticValues.find(expected);
    assertTrue(position != std::string::npos, "semantic oracle fixture declares time");
    altered.semanticValues.replace(position, expected.size(), "time=12.6");

    SimulationRandom::global.setSeed(0x12345678);
    std::unique_ptr<SimulationWorld> loaded = decodeSubstrate(
            fixtureBytes(altered), legacyFallbackEnvironment());
    bool rejected = false;
    try {
        assertLegacyWorld(*loaded, altered);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assertTrue(rejected, "semantic oracle must reject one intentionally changed field");
}

void testLegacyGenomeCorpus() {
    int legacyTested = 0;
    for (const auto& spec : cellularity::test_fixtures::formatFixtureSpecs()) {
        if (spec.family == "legacy-genome" && spec.expected == "accept-native") {
            const auto genome = cellularity::snapshot_codec::
                    decodeLegacyGenomeBytes(fixtureBytes(spec));
            assertTrue(genome.size() == CellData::MAX_GENES_COUNT,
                    spec.id + ".effectiveLegacyLength");
            const cellularity::GenomeData dynamicGenome(
                    genome.begin(), genome.end());
            validateGenomeDraft(dynamicGenome);
            assertLegacyGene(genome[0], spec.geneVersion, spec.id + ".gene0");
            int initialCount = 0;
            for (const Gene& gene : genome) {
                initialCount += GeneView(gene).initialCompatibilityFlag() ? 1 : 0;
            }
            assertEqual(initialCount, 1, spec.id + ".initialCount");
            if (spec.geneCount < CellData::MAX_GENES_COUNT) {
                assertGeneEquivalent(genome[spec.geneCount], genome[0],
                        spec.id + ".expandedSlot", false);
                assertEqual(GeneView(genome[spec.geneCount]).initialCompatibilityFlag(), false,
                        spec.id + ".expandedSlot.initial");
            }
            legacyTested++;
        }
    }
    assertEqual(legacyTested, 6, "legacy Genome semantic fixture count");
}

Gene richGene(int cell, int index) {
    Gene gene;
    GeneBuilder builder(gene);
    builder.setColor(0, 0.1f + cell * 0.1f);
    builder.setColor(1, 0.2f + index * 0.001f);
    builder.setColor(2, 0.3f);
    builder.setSplitMass(0.12f);
    builder.setSplitRatio(0.4f);
    builder.setSplitAngle(0.1f);
    builder.setChild1Angle(0.2f);
    builder.setChild2Angle(0.3f);
    builder.setMassFlowPriority(1.5f);
    builder.setLinkStrength(6.5f);
    builder.setChild1GeneIndex(GeneIndex((index + 1) % CellData::MAX_GENES_COUNT));
    builder.setChild2GeneIndex(GeneIndex((index + 2) % CellData::MAX_GENES_COUNT));
    builder.setMakesLink(true);
    builder.setChild1KeepsLinks(true);
    builder.setChild2KeepsLinks(false);
    builder.setStaysAlive(true);
    builder.setInitialCompatibilityFlag(index == 0);
    builder.setChild1Mirrored(false);
    builder.setChild2Mirrored(true);
    builder.setCellType(index % 2 == 0 ? CellType::PHAGOCYTE : CellType::GLUEOCYTE);
    for (int i = 0; i < Gene::MAX_PARAMETRIC_VALUES_COUNT; i++) {
        builder.setParametricVariableA(i, 0.01f * i);
        builder.setParametricVariableB(i, -0.02f * i);
        builder.setParametricVariableC(i, 0.03f * i);
        builder.setParametricSignalChannel(i, i % 4);
        builder.setParametricFormulaType(i, i % 3);
    }
    for (int i = 0; i < Gene::MAX_INTEGERS_COUNT; i++) {
        builder.setIntegerValue(i, i == Gene::MAX_CONNECTION_COUNT_INDEX ? 4 : 0);
    }
    builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 0.1f);
    builder.setFloatValue(Gene::FloatType::SMELL_RED, 0.2f);
    builder.setFloatValue(Gene::FloatType::SMELL_GREEN, 0.3f);
    builder.setFloatValue(Gene::FloatType::SMELL_BLUE, 0.4f);
    builder.setFloatValue(Gene::FloatType::SMELL_THRESHOLD, 0.5f);
    builder.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.006f);
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.4f);
    return gene;
}

std::unique_ptr<SimulationWorld> richWorld() {
    Environment environment = Environment::constructDefaultEnvironment(4, 4, 1.25);
    environment.mIsPreviewMode = false;
    environment.mLightAngle = 0.37;
    environment.mLightAngleCos = std::cos(environment.mLightAngle);
    environment.mLightAngleSin = std::sin(environment.mLightAngle);
    environment.mFoodSpeed = 0.11;
    environment.mFoodSize = 0.12;
    environment.mRadiation = 0.013;
    environment.mLightIntensity = 0.81;
    environment.mLightSpeed = 0.023;
    environment.mLightRange = 0.43;
    environment.mViscosity = 61.0;
    environment.mDensity = 0.14;
    environment.mGravity = 0.06;
    environment.mDensityGradient = -0.21;
    environment.mGravityFieldType = 2;
    environment.mGravityFieldStrength = 0.08;
    environment.mGravityFieldAngle = 0.44;
    environment.mDensityFieldType = 2;
    environment.mDensityFieldBase = 0.16;
    environment.mDensityFieldStrength = -0.26;
    environment.mDensityFieldAngle = 1.2;
    environment.mDensityFieldOffset = 0.22;
    environment.mDensityFieldSoftness = 0.32;
    environment.mSalinity = 0.27f;
    environment.mFoodLipidLayer = 0.37f;
    environment.mSalinityFieldType = 1;
    environment.mSalinityFieldBase = 0.28;
    environment.mSalinityFieldStrength = 0.04;
    environment.mSalinityFieldAngle = 0.74;
    environment.mSalinityFieldOffset = -0.12;
    environment.mSalinityFieldSoftness = 0.24;
    environment.mFoodLipidLayerFieldType = 2;
    environment.mFoodLipidLayerFieldBase = 0.38;
    environment.mFoodLipidLayerFieldStrength = -0.07;
    environment.mFoodLipidLayerFieldAngle = -0.74;
    environment.mFoodLipidLayerFieldOffset = 0.17;
    environment.mFoodLipidLayerFieldSoftness = 0.27;
    environment.mTestMulticellularCoef = 1.3;
    environment.mTestMutationCoef = 1.4;
    environment.mTestFoodDecay = 1.5;
    environment.mTestIdleMetabolismScale = 0.91;
    environment.mTestFlagellocyteCostScale = 1.11;
    environment.mTestPhagocyteEatSpeedScale = 1.21;
    environment.mFrictionStatic = 0.42;
    environment.mFrictionDynamic = 0.33;
    environment.mFoodLumpiness = 0.23;
    environment.mFoodLumpSize = 0.034;
    environment.mNitrates = 0.76;
    environment.maxDeadSplashCount = 1;
    environment.IsOnlyTweakMutations = true;
    environment.mutationOnlyAfterSplit = true;
    environment.aging = true;
    environment.addRandom = true;
    environment.dieEdge = true;
    environment.mobileFood = true;
    environment.mGenePool.fill(false);
    environment.mGenePool[0] = true;
    environment.mGenePool[7] = true;
    for (int i = 0; i < Environment::MUTATION_WEIGHT_COUNT; i++) {
        environment.mMutationWeights[i] = 0.5 + i * 0.01;
    }
    environment.mRandom.setSeed(0x0102030405060708LL);

    auto decodedWorld = std::make_unique<SimulationWorld>(false, environment);
    SimulationWorld& world = *decodedWorld;
    world.mTime = 45.25;
    CellData& cells = world.mCellData;
    cells.replaceActiveCellCount(2);
    const OrganismId organismId = cells.assignNewOrganism(0);
    cells.mOrganismIds[1] = organismId;
    cells.mParentOrganismIds[1] = kUnknownOrganismId;
    for (int cell = 0; cell < cells.mCellCount; cell++) {
        cells.mPositionX[cell] = 0.1 + cell;
        cells.mPositionY[cell] = -0.2 - cell;
        cells.mPositionZ[cell] = 0.003 + cell * 0.001;
        cells.mRadius[cell] = 0.02 + cell * 0.001;
        cells.refreshRadiusSq(cell);
        cells.mVelocityX[cell] = 0.01 + cell;
        cells.mVelocityY[cell] = -0.02 - cell;
        cells.mForceX[cell] = 0.03 + cell;
        cells.mForceY[cell] = -0.04 - cell;
        cells.mForceZ[cell] = 0.05 + cell;
        cells.mTorque[cell] = -0.06 - cell;
        cells.mAngle[cell] = 0.2 + cell;
        cells.mAngleCytoplasm[cell] = 0.3 + cell;
        cells.mAngularVelocity[cell] = 0.04 + cell;
        cells.mAngularVelocityCytoplasm[cell] = -0.05 - cell;
        cells.mAge[cell] = 1.5 + cell;
        cells.mNitrates[cell] = 0.8 - cell * 0.1;
        cells.mSwimAnimationTime[cell] = 0.1f + cell;
        cells.mSwimAnimationTime2[cell] = 0.2f + cell;
        cells.mHairRotation[cell] = 0.6 + cell;
        cells.hairX[cell] = 0.01 + cell;
        cells.hairY[cell] = 0.02 + cell;
        cells.mMass[cell] = 0.2 + cell * 0.01;
        cells.mNewMass[cell] = cells.mMass[cell];
        cells.mNewNitrates[cell] = cells.mNitrates[cell];
        cells.mIsDead[cell] = cell == 1;
        cells.mIsMirrored[cell] = cell == 1;
        cells.mIsStuck[cell] = cell == 0;
        cells.mOriginTag[cell] = 11 + cell;
        cells.mSelectTag[cell] = 21 + cell;
        cells.mMutationCount[cell] = 3 + cell;
        cells.mPendingMutationCount[cell] = 4 + cell;
        cells.mSeldomCounter[cell] = 5 + cell;
        cells.mOldLinkCount[cell] = 0;
        cells.mLipids[cell] = 0.06f + cell * 0.01f;
        cells.mTelomeresCount[cell] = 17 + cell;
        cells.mFertilizationTarget[cell] = -1;
        cells.mColor[cell] = {0.2f, 0.4f, 0.6f, 1.0f};
        cellularity::FixedGenome genome{};
        for (int gene = 0; gene < CellData::MAX_GENES_COUNT; gene++) {
            genome[gene] = richGene(cell, gene);
        }
        assertTrue(
                cells.replaceGenome(cell, genome, GeneIndex(0)),
                "rich world imports Cell Genome");
        for (size_t i = 0; i < cells.mSignalSubstance[cell].size(); i++) {
            cells.mSignalSubstance[cell][i] = 0.01f * static_cast<float>(i + 1 + cell);
        }
        for (size_t i = 0; i < cells.mNewSignalSubstance[cell].size(); i++) {
            cells.mNewSignalSubstance[cell][i] = 0.02f * static_cast<float>(i + 1 + cell);
            cells.mSignalSubstanceOut[cell][i] = 0.03f * static_cast<float>(i + 1 + cell);
        }
        cells.mToxin[cell] = 0.1f + cell;
        cells.mToxication[cell] = 0.2f + cell;
        cells.mNewToxin[cell] = 0.3f + cell;
        cells.mMyocyteContraction[cell] = 0.4f + cell;
        cells.mMyocyteBending[cell] = 0.5f + cell;
        cells.mMyocyteLifting[cell] = 0.6f + cell;
        cells.refreshActiveCellType(cell);
    }
    int edge = cells.mTopology.addCellLink(
            0,
            1,
            CellLinkCreationReason::SPLIT_SIBLING,
            0.25,
            1.25,
            0.5f,
            0.01f);
    cells.mTopology.setEdgeRenderOffsets(edge, -0.002f, 0.003f, 0.004f, -0.005f);
    int boundary = cells.mTopology.addBoundaryLink(0, 0.75, 0.6, -0.4, 0.25f, 0.02f);
    cells.mTopology.setBoundaryRenderStartOffset(boundary, 0.125f, -0.25f);

    world.mFoodData.replaceActiveFoodCount(2);
    for (int i = 0; i < 2; i++) {
        world.mFoodData.mPositionX[i] = 0.2f + i * 0.8f;
        world.mFoodData.mPositionY[i] = -0.3f - i * 0.8f;
        world.mFoodData.mMass[i] = 0.04f + i * 0.01f;
        world.mFoodData.mVelocityX[i] = 0.01f + i;
        world.mFoodData.mVelocityY[i] = -0.02f - i;
        world.mFoodData.mLipidLayer[i] = 0.3f + i * 0.01f;
    }
    world.mDeadSplashData.replaceActiveDeadSplashCount(1);
    world.mDeadSplashData.mPositionX[0] = 0.7f;
    world.mDeadSplashData.mPositionY[0] = -0.8f;
    world.mDeadSplashData.mRadius[0] = 0.09f;
    world.mDeadSplashData.mAge[0] = 0.1f;
    world.mDeadSplashData.mColor[0] = {0.1f, 0.2f, 0.3f, 0.4f};
    return decodedWorld;
}

cellularity::GenomeData dynamicBoundaryGenome(
        std::size_t geneCount,
        int marker) {
    cellularity::GenomeData genome(geneCount);
    for (std::size_t index = 0; index < geneCount; ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto materialized = cellularity::materializeCanonicalGene(
                cellularity::CanonicalGeneDefaults::value(),
                owner,
                geneCount,
                index + 1 == geneCount);
        assertTrue(materialized.has_value(),
                "dynamic boundary Gene materializes");
        genome[index] = *materialized;
        GeneBuilder builder(genome[index]);
        builder.setColor(
                0,
                static_cast<float>(marker + 1)
                        / static_cast<float>(geneCount + 16));
        builder.setColor(
                1,
                static_cast<float>(index + 1)
                        / static_cast<float>(geneCount + 1));
        builder.setChild1GeneIndex(GeneIndex(static_cast<GeneIndex::Value>(
                (index + 1) % geneCount)));
        builder.setChild2GeneIndex(GeneIndex(static_cast<GeneIndex::Value>(
                (index + geneCount - 1) % geneCount)));
    }
    return genome;
}

void testDynamicGenomeBoundaryRoundTrips() {
    constexpr std::array<std::size_t, 5> lengths{1, 2, 40, 150, 256};
    Environment environment = Environment::constructDefaultEnvironment(
            static_cast<int>(lengths.size()), 4, 1.25);
    SimulationWorld source(false, environment);
    source.mCellData.replaceActiveCellCount(
            static_cast<int>(lengths.size()));

    std::array<cellularity::GenomeData, lengths.size()> expectedGenomes;
    std::array<GeneIndex, lengths.size()> expectedActiveIndices;
    for (std::size_t cell = 0; cell < lengths.size(); ++cell) {
        source.mCellData.mPositionX[cell] =
                -0.4 + 0.2 * static_cast<double>(cell);
        source.mCellData.mPositionY[cell] = 0.0;
        source.mCellData.mRadius[cell] = 0.02;
        source.mCellData.refreshRadiusSq(static_cast<int>(cell));
        source.mCellData.mMass[cell] = 0.1;
        source.mCellData.mNewMass[cell] = 0.1;
        expectedGenomes[cell] = dynamicBoundaryGenome(
                lengths[cell], static_cast<int>(cell + 1));
        expectedActiveIndices[cell] = GeneIndex(
                static_cast<GeneIndex::Value>(lengths[cell] / 2));
        assertTrue(source.mCellData.replaceGenome(
                           static_cast<int>(cell),
                           expectedGenomes[cell],
                           expectedActiveIndices[cell]),
                "dynamic boundary Genome enters CellData");
        assertEqual(source.mCellData.geneCount(static_cast<int>(cell)),
                lengths[cell],
                "CellData preserves dynamic boundary length");

        cellularity::GenomeData standalone = expectedGenomes[cell];
        for (std::size_t gene = 0; gene < standalone.size(); ++gene) {
            GeneBuilder(standalone[gene]).setInitialCompatibilityFlag(gene == expectedActiveIndices[cell].value());
        }
        const std::vector<std::uint8_t> bytes =
                cellularity::save_format::release::encodeReleaseGenome(
                        standalone);
        const cellularity::GenomeData decoded =
                cellularity::save_format::release::decodeReleaseGenome(bytes);
        assertEqual(decoded.size(), lengths[cell],
                "standalone Genome preserves dynamic boundary length");
        for (std::size_t gene = 0; gene < lengths[cell]; ++gene) {
            Gene expected(expectedGenomes[cell][gene]);
            GeneBuilder(expected).setInitialCompatibilityFlag(
                    gene == expectedActiveIndices[cell].value());
            assertGeneEquivalent(
                    decoded[gene],
                    expected,
                    "dynamic standalone round trip["
                            + std::to_string(lengths[cell]) + "]["
                            + std::to_string(gene) + "]");
        }
    }

    const std::vector<std::uint8_t> bytes =
            cellularity::save_format::release::encodeReleaseSubstrate(source)
                    .bytes;
    std::unique_ptr<SimulationWorld> decoded = decodeSubstrate(
            bytes, legacyFallbackEnvironment());
    assertEqual(decoded->mCellData.mCellCount,
            static_cast<int>(lengths.size()),
            "dynamic substrate Cell count");
    for (std::size_t cell = 0; cell < lengths.size(); ++cell) {
        assertEqual(decoded->mCellData.geneCount(static_cast<int>(cell)),
                lengths[cell],
                "substrate preserves dynamic boundary length");
        assertEqual(decoded->mCellData.activeGeneIndex(
                            static_cast<int>(cell)),
                expectedActiveIndices[cell],
                "substrate preserves dynamic active Gene index");
        const cellularity::GenomeData actual =
                decoded->mCellData.materializeGenome(static_cast<int>(cell));
        for (std::size_t gene = 0; gene < lengths[cell]; ++gene) {
            assertGeneEquivalent(
                    actual[gene],
                    expectedGenomes[cell][gene],
                    "dynamic substrate round trip["
                            + std::to_string(lengths[cell]) + "]["
                            + std::to_string(gene) + "]");
        }
    }
}

void testReleaseSaveLoadRoundTripsAndWriterPolicy() {
    std::unique_ptr<SimulationWorld> source = richWorld();
    source->mCellData.clearCellCollisionContacts();
    assertTrue(source->mCellData.addKnownCellCollisionContactFast(
                       0, 1, -0.03, 0.0, 0.0009),
            "writer policy fixture stores a transient source contact");
    assertTrue(source->mCellData.addKnownCellCollisionContactFast(
                       1, 0, 0.03, 0.0, 0.0009),
            "writer policy fixture stores a transient reciprocal contact");
    const std::vector<uint8_t> beforeRuntimeIdentityRefresh =
            cellularity::save_format::release::encodeReleaseSubstrate(
                    *source)
                    .bytes;
    const std::unique_ptr<SimulationWorld> withoutTransientContacts =
            cellularity::save_format::release::decodeReleaseSubstrate(
                    beforeRuntimeIdentityRefresh, false);
    assertTrue(!withoutTransientContacts->cellContactCacheIsFresh(),
            "release files do not persist transient collision contacts");
    const CellRuntimeId previousFirstRuntimeId =
            source->mCellData.runtimeId(0);
    const FoodRuntimeId previousFirstFoodRuntimeId =
            source->mFoodData.runtimeId(0);
    const DeadSplashRuntimeId previousFirstDeadSplashRuntimeId =
            source->mDeadSplashData.runtimeId(0);
    source->mCellData.replaceActiveCellCount(source->mCellData.mCellCount);
    source->mFoodData.replaceActiveFoodCount(source->mFoodData.mFoodCount);
    source->mDeadSplashData.replaceActiveDeadSplashCount(
            source->mDeadSplashData.mDeadSplashCount);
    assertTrue(source->mCellData.runtimeId(0) != previousFirstRuntimeId,
            "runtime Cell identity refresh changes only transient state");
    assertTrue(source->mFoodData.runtimeId(0)
                            != previousFirstFoodRuntimeId
                    && source->mDeadSplashData.runtimeId(0)
                            != previousFirstDeadSplashRuntimeId,
            "runtime auxiliary identity refresh changes only transient state");
    assertEqual(cellularity::save_format::release::encodeReleaseSubstrate(
                        *source)
                        .bytes,
            beforeRuntimeIdentityRefresh,
            "runtime entity identities are absent from substrate bytes");

    source->updateGrids();
    const std::vector<uint8_t> beforeRenderCapture =
            cellularity::save_format::release::encodeReleaseSubstrate(
                    *source)
                    .bytes;
    const cellularity::render::RenderViewBounds captureBounds{
            -2.0f, 2.0f, -2.0f, 2.0f
    };
    cellularity::render::RenderFrameCaptureMetadata captureMetadata;
    captureMetadata.viewport.visibleBounds = captureBounds;
    captureMetadata.viewport.coverageBounds = captureBounds;
    cellularity::render::RenderSourceFrame renderSource;
    cellularity::render::RenderSourceCapture renderCapture(
            source->mEnvironment.maxCellCount, renderSource);
    const cellularity::render::RenderMarkerCaptureRequest markerRequest{
            0, 0.01f, 1.0f
    };
    renderCapture.capture(
            *source,
            captureBounds,
            captureMetadata,
            renderSource,
            &markerRequest,
            1);
    assertTrue(renderSource.valid && renderSource.markers.size() == 1,
            "render source capture produces a committed owned frame");
    assertEqual(cellularity::save_format::release::encodeReleaseSubstrate(
                        *source)
                        .bytes,
            beforeRenderCapture,
            "render source capture does not mutate persistent world state");
    const SimulationWorld& sourceWorld = *source;

    cellularity::GenomeData sourceGenome =
            sourceWorld.mCellData.materializeGenome(0);
    const int activeGeneIndex =
            sourceWorld.mCellData.activeGeneIndex(0).value();
    for (std::size_t i = 0; i < sourceGenome.size(); ++i) {
        GeneBuilder(sourceGenome[i]).setInitialCompatibilityFlag(i == static_cast<std::size_t>(activeGeneIndex));
    }
    const std::vector<uint8_t> genomeBytes =
            cellularity::save_format::release::encodeReleaseGenome(
                    sourceGenome);
    assertEqual(
            cellularity::save_format::release::encodeReleaseGenome(
                    sourceGenome),
            genomeBytes,
            "release Genome writer is deterministic");
    const auto decodedGenome =
            cellularity::save_format::release::decodeReleaseGenome(
                    genomeBytes);
    assertEqual(decodedGenome.size(), sourceGenome.size(),
            "release Genome length");
    for (std::size_t i = 0; i < sourceGenome.size(); ++i) {
        assertGeneEquivalent(decodedGenome[i], sourceGenome[i],
                "genomeRoundTrip[" + std::to_string(i) + "]");
    }

    const std::vector<uint8_t> cellBytes =
            writeCellSnapshotWire(sourceWorld, 0);
    assertEqual(firstInt(cellBytes), CELL_MAGIC, "cellWire.magic");
    assertEqual(firstInt(cellBytes, 4),
            cellularity::internal_wire::kCellSnapshotVersion,
            "cellWire.version");

    const std::vector<uint8_t> substrateBytes =
            cellularity::save_format::release::encodeReleaseSubstrate(
                    sourceWorld)
                    .bytes;
    assertTrue(isReleaseFile(substrateBytes),
            "release substrate magic");
    const NativeSubstrateInspection headerInspection = inspectSubstrateHeader(
            substrateBytes, false, legacyFallbackEnvironment());
    assertEqual(headerInspection.familyLocalVersion, 1,
            "release header inspection schema version");
    assertTrue(
            headerInspection.formatFamily
                            == NativeSubstrateFormatFamily::ReleaseClty
                    && headerInspection.contentSchemaMajor == 1
                    && headerInspection.loadSupported,
            "release header inspection compatibility");
    assertNear(headerInspection.time, sourceWorld.mTime, DOUBLE_TOLERANCE,
            "current header inspection time");
    assertEqual(headerInspection.cellCount, sourceWorld.mCellData.mCellCount,
            "current header inspection cell count");
    assertEqual(headerInspection.environment.maxCellCount,
            sourceWorld.mEnvironment.maxCellCount,
            "release header maximum Cell count");
    assertEqual(headerInspection.environment.maxFoodCount,
            sourceWorld.mEnvironment.maxFoodCount,
            "release header maximum Food count");
    assertNear(headerInspection.environment.mRadius,
            sourceWorld.mEnvironment.mRadius,
            DOUBLE_TOLERANCE,
            "release header dish radius");
    std::unique_ptr<SimulationWorld> decodedSubstrate = decodeSubstrate(
            substrateBytes, legacyFallbackEnvironment());
    assertWorldEquivalent(*decodedSubstrate, sourceWorld, "substrateRoundTrip");
    assertTrue(decodedSubstrate->foodSpatialIndexIsFresh(),
            "Substrate load publishes a fresh Food spatial index");
    assertTrue(isEntityCellRuntimeId(decodedSubstrate->mCellData.runtimeId(0))
                    && isEntityCellRuntimeId(
                            decodedSubstrate->mCellData.runtimeId(1))
                    && decodedSubstrate->mCellData.runtimeId(0)
                            != decodedSubstrate->mCellData.runtimeId(1),
            "substrate load assigns fresh distinct runtime Cell identities");
    assertTrue(isValidFoodRuntimeId(
                       decodedSubstrate->mFoodData.runtimeId(0))
                    && isValidFoodRuntimeId(
                            decodedSubstrate->mFoodData.runtimeId(1))
                    && decodedSubstrate->mFoodData.runtimeId(0)
                            != decodedSubstrate->mFoodData.runtimeId(1)
                    && isValidDeadSplashRuntimeId(
                            decodedSubstrate->mDeadSplashData.runtimeId(0)),
            "substrate load assigns fresh valid auxiliary runtime identities");

    ScopedTemporaryFile substrateFile("-release.substrate");
    writeSubstratePath(substrateFile.path.string(), sourceWorld);
    const std::vector<uint8_t> releaseBytes = substrateFile.read();
    assertEqual(releaseBytes, substrateBytes,
            "streamed and bounded release writers differ");
    validateNativeSubstratePath(
            substrateFile.path.string(), false, legacyFallbackEnvironment());
    std::unique_ptr<SimulationWorld> releaseDecoded =
            cellularity::save_format::release::decodeReleaseSubstrate(
                    releaseBytes, false);
    assertEqual(
            cellularity::save_format::release::substrateSemanticDigest(
                    *releaseDecoded),
            cellularity::save_format::release::substrateSemanticDigest(
                    sourceWorld),
            "production release substrate semantic round trip");
    const NativeSubstrateInspection inspection = inspectSubstratePath(
            substrateFile.path.string(), false, legacyFallbackEnvironment());
    assertEqual(inspection.familyLocalVersion, 1,
            "release inspection content schema version");
    assertTrue(
            inspection.formatFamily
                            == NativeSubstrateFormatFamily::ReleaseClty
                    && inspection.contentSchemaMajor == 1
                    && inspection.contentSchemaMinor == 2
                    && inspection.loadSupported,
            "release inspection compatibility");
    assertEqual(inspection.knownOrganismCount, 1,
            "release inspection known Organism count");
    assertTrue(inspection.organismIdentityComplete,
            "release inspection Organism identity completeness");
    assertNear(inspection.time, sourceWorld.mTime, DOUBLE_TOLERANCE,
            "current inspection time");
    assertEqual(inspection.cellCount, sourceWorld.mCellData.mCellCount,
            "current inspection cell count");
    assertEqual(inspection.environment.maxCellCount,
            sourceWorld.mEnvironment.maxCellCount,
            "release inspection maximum Cell count");
    assertEqual(inspection.environment.maxFoodCount,
            sourceWorld.mEnvironment.maxFoodCount,
            "release inspection maximum Food count");
    assertNear(inspection.environment.mRadius,
            sourceWorld.mEnvironment.mRadius,
            DOUBLE_TOLERANCE,
            "release inspection dish radius");
}

void testJavaObjectStreamCorpus() {
    int tested = 0;
    for (const auto& spec : cellularity::test_fixtures::formatFixtureSpecs()) {
        if (spec.family != "java-serialization" || spec.expected != "accept-native") {
            continue;
        }
        JavaObjectStreamReader reader(fixtureBytes(spec));
        if (spec.id == "jos-blockdata" || spec.id == "jos-blockdatalong") {
            assertEqual(reader.readInt(), 0x01020304, spec.id + ".primitiveInt");
        } else if (spec.id == "jos-reset") {
            assertEqual(reader.readObject().stringValue, std::string("A"), spec.id + ".firstString");
            assertEqual(reader.readObject().stringValue, std::string("B"), spec.id + ".secondString");
        } else if (spec.id == "jos-string-reference") {
            const auto first = reader.readObject();
            const auto second = reader.readObject();
            assertEqual(first.stringValue, std::string("Cellularity"), spec.id + ".string");
            assertEqual(second.kind, JavaObjectStreamReader::ObjectKind::Reference,
                    spec.id + ".referenceKind");
            assertEqual(second.stringValue, first.stringValue, spec.id + ".referenceValue");
        } else if (spec.id == "jos-longstring") {
            assertEqual(reader.readObject().stringValue, std::string("Long"), spec.id + ".value");
        } else if (spec.id == "jos-null") {
            assertEqual(reader.readObject().kind, JavaObjectStreamReader::ObjectKind::Null,
                    spec.id + ".kind");
        } else {
            const auto object = reader.readObject();
            if (spec.id == "jos-class-desc") {
                assertEqual(object.kind, JavaObjectStreamReader::ObjectKind::ClassDesc,
                        spec.id + ".kind");
            } else if (spec.id == "jos-object-graph") {
                assertEqual(object.kind, JavaObjectStreamReader::ObjectKind::Object,
                        spec.id + ".kind");
            } else {
                assertEqual(object.kind, JavaObjectStreamReader::ObjectKind::Array,
                        spec.id + ".kind");
            }
        }
        assertTrue(!reader.hasRemainingBytes(), spec.id + ".completeConsumption");
        tested++;
    }
    assertEqual(tested, 10, "Java serialization semantic fixture count");
}

template <typename Function>
void expectNewerVersionError(Function function,
        NativeSnapshotDecodeErrorCode expectedCode,
        const std::string& context) {
    try {
        function();
    } catch (const NativeSnapshotDecodeError& error) {
        assertEqual(error.code(), expectedCode, context + ".errorCode");
        return;
    }
    fail(context + ": expected NativeSnapshotDecodeError");
}

void testNewerVersionTypedErrors() {
    for (const std::string id : {
                 "legacy-outer-v104-invalid",
                 "legacy-environment-v104-invalid",
                 "legacy-cell-v104-invalid",
                 "legacy-gene-v104-invalid"
         }) {
        expectNewerVersionError([&] {
            (void)cellularity::snapshot_codec::
                    loadLegacySubstrateBytesDirect(
                            fixtureBytes(fixtureById(id)),
                            false,
                            legacyFallbackEnvironment());
        },
                NativeSnapshotDecodeErrorCode::NewerProductVersion, "newer." + id);
    }

    for (const std::array<std::uint8_t, 4> magic : {
                 std::array<std::uint8_t, 4>{'C', 'L', 'S', 'B'},
                 std::array<std::uint8_t, 4>{'C', 'L', 'G', 'N'},
                 std::array<std::uint8_t, 4>{'C', 'L', 'E', 'N'},
                 std::array<std::uint8_t, 4>{'C', 'G', 'D', 'W'}
         }) {
        ScopedTemporaryFile discarded("-discarded.substrate");
        discarded.write(std::vector<std::uint8_t>(
                magic.begin(), magic.end()));
        expectNewerVersionError([&] {
            (void)loadNativeSubstrateFromPath(
                    discarded.path.string(),
                    false,
                    legacyFallbackEnvironment());
        },
                NativeSnapshotDecodeErrorCode::UnsupportedFeature, "discarded pre-release substrate format");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        cellularity::test_fixtures::verifyFormatFixtureCorpus(CORPUS_DIRECTORY);
        if (argc == 2 && std::string(argv[1]) == "--print-legacy-effective-state") {
            printLegacyEffectiveStateDigests();
            return 0;
        }
        testLegacySubstrateCorpusAndMigration();
        testLegacyEffectiveStateOracle();
        testAcceptedLegacyGenesEnterCanonicalBoundary();
        testSemanticOracleRejectsChangedExpectedField();
        testLegacyGenomeCorpus();
        testDynamicGenomeBoundaryRoundTrips();
        testReleaseSaveLoadRoundTripsAndWriterPolicy();
        testJavaObjectStreamCorpus();
        testNewerVersionTypedErrors();
        std::cout << "format semantic tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "format semantic tests failed: " << error.what() << '\n';
        return 1;
    }
}
