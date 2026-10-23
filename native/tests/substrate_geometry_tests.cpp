#include "core/Environment.h"
#include "core/geometry/SubstrateGeometry.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

using cellularity::SubstrateGeometry;
using cellularity::tests::assertTrue;

void assertSame(double expected, double actual, const char* message) {
    if (std::isnan(expected) && std::isnan(actual)) {
        return;
    }
    std::uint64_t expectedBits;
    std::uint64_t actualBits;
    std::memcpy(&expectedBits, &expected, sizeof(expectedBits));
    std::memcpy(&actualBits, &actual, sizeof(actualBits));
    assertTrue(expectedBits == actualBits, message);
}

void testDescriptionHasOneSizeSourceAndNoOwnedAllocations() {
    static_assert(sizeof(SubstrateGeometry) == 2 * sizeof(double));
    static_assert(std::is_trivially_copyable_v<SubstrateGeometry>);
    Environment environment;
    environment.mRadius = 2.0;
    const auto first = environment.geometry();
    const auto bounds = first.bounds();
    assertSame(-2.0, bounds.minX, "Circle min X");
    assertSame(2.0, bounds.maxX, "Circle max X");
    assertSame(-2.0, bounds.minY, "Circle min Y");
    assertSame(2.0, bounds.maxY, "Circle max Y");
    assertSame(Environment::PI * 2.0 * 2.0, first.area(), "Circle area");
    environment.mRadius = 3.0;
    assertSame(3.0, environment.geometry().radius(), "Direct radius updates must not leave stale geometry");
    assertSame(2.0, first.radius(), "An acquired value is an immutable geometry snapshot");
}

void testPointClassificationPreservesEveryEdgeRule() {
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::array<double, 9> coordinates{0.0, -0.0, 0.6, -0.8, 1.0, std::nextafter(1.0, 0.0), std::nextafter(1.0, infinity), infinity, nan};
    for (double radius : {0.1, 1.0, 2.0}) {
        const SubstrateGeometry geometry(radius);
        for (double x : coordinates) {
            for (double y : coordinates) {
                assertTrue(geometry.isOutside(x, y) == (x * x + y * y > radius * radius), "Strict outside test differs from the original expression");
                assertTrue(geometry.isOutsideOrOnBoundary(x, y) == (x * x + y * y >= radius * radius), "Inclusive outside test differs from the original expression");
            }
        }
    }
    const SubstrateGeometry geometry(1.0);
    assertTrue(!geometry.isOutside(1.0, 0.0) && geometry.isOutsideOrOnBoundary(1.0, 0.0), "Sampling includes the rim; insertion/removal excludes it");
}

void testFoodClassificationRetainsFloatArithmetic() {
    const float x = 0.8f;
    const float y = 0.6f;
    const double floatDistanceSq = x * x + y * y;
    const double doubleDistanceSq = static_cast<double>(x) * x + static_cast<double>(y) * y;
    const double radius = std::sqrt((floatDistanceSq + doubleDistanceSq) * 0.5);
    const SubstrateGeometry geometry(radius);
    const bool expected = x * x + y * y >= radius * radius;
    assertTrue(expected != (doubleDistanceSq >= radius * radius), "Fixture must distinguish float and widened-double food positions");
    assertTrue(geometry.isOutsideOrOnBoundary(x, y) == expected, "Food positions must not be widened before their squared-distance sum");
}

void testBoundaryLocationsMatchTheExistingRadialGeometry() {
    const SubstrateGeometry geometry(1.0);
    for (const std::array<double, 2>& point : {std::array<double, 2>{0.75, 0.25}, {-0.3, 0.9}, {0.0, -1.0}, {0.0, 0.0}}) {
        const auto location = geometry.boundaryLocation(point[0], point[1]);
        const double distance = std::sqrt(point[0] * point[0] + point[1] * point[1]);
        assertSame(distance, location.centerDistance, "Boundary distance");
        assertSame(1.0 - distance, location.clearance, "Wall smell clearance");
        if (distance != 0.0) {
            assertSame(point[0] / distance, location.normalX, "Ciliocyte wall normal X");
            assertSame(point[1] / distance, location.normalY, "Ciliocyte wall normal Y");
        }
    }
}

void testContactsAndRenderChordsMatchTheOriginalFormulas() {
    constexpr double cellRadius = 0.03;
    for (double radius : {0.1, 0.3, 1.0, 3.0}) {
        const SubstrateGeometry geometry(radius);
        for (double angle : {0.0, 0.3, -0.7, 2.4}) {
            for (double fraction : {0.0, 0.5, 0.97, 0.99, 1.0}) {
                const double x = std::cos(angle) * radius * fraction;
                const double y = std::sin(angle) * radius * fraction;
                const double distanceSq = x * x + y * y;
                const double contactDistance = radius - cellRadius;
                cellularity::SubstrateBoundaryContact contact;
                const bool hasContact = geometry.cellBoundaryContact(x, y, cellRadius, contact);
                assertTrue(hasContact == !(distanceSq < contactDistance * contactDistance), "Boundary contact acceptance");
                if (!hasContact) {
                    continue;
                }
                const double distance = std::sqrt(distanceSq);
                const double length = std::sqrt((-distance + cellRadius - radius) * (-distance - cellRadius + radius) * (-distance + cellRadius + radius) * (distance + cellRadius + radius)) / distance;
                assertSame(distance, contact.centerDistance, "Contact center distance must remain bit-identical");
                assertSame(length, contact.chordLength, "Contact chord length must remain bit-identical");
                const double midpointDistance = (distance * distance - radius * radius + cellRadius * cellRadius) / (2.0 * distance);
                const double midpointX = midpointDistance * -x / distance;
                const double midpointY = midpointDistance * -y / distance;
                const auto chord = geometry.cellBoundaryChord(x, y, cellRadius, contact);
                assertSame(midpointX + length * y / (2.0 * distance), chord.x1, "Render boundary chord X1");
                assertSame(midpointY - length * x / (2.0 * distance), chord.y1, "Render boundary chord Y1");
                assertSame(midpointX - length * y / (2.0 * distance), chord.x2, "Render boundary chord X2");
                assertSame(midpointY + length * x / (2.0 * distance), chord.y2, "Render boundary chord Y2");
            }
        }
    }
}

void testAnchorAndFoodProjectionKeepTheirOperationOrder() {
    const SubstrateGeometry geometry(1.25);
    const double x = 0.75;
    const double y = -0.8;
    const double distance = std::sqrt(x * x + y * y);
    const double offset = 0.003;
    const auto anchor = geometry.boundaryAnchor(x, y, distance, offset);
    assertSame(x / distance * (1.25 + offset), anchor[0], "Glueocyte anchor X");
    assertSame(y / distance * (1.25 + offset), anchor[1], "Glueocyte anchor Y");
    const float foodX = 1.4f;
    const float foodY = -0.6f;
    const float foodDistance = std::sqrt(foodX * foodX + foodY * foodY);
    const auto projection = geometry.projectToBoundary(foodX, foodY, foodDistance);
    assertSame(static_cast<float>(1.25) * foodX / foodDistance, projection[0], "Food projection X must stay float");
    assertSame(static_cast<float>(1.25) * foodY / foodDistance, projection[1], "Food projection Y must stay float");
}

void testSamplingPreservesCoordinatesAndTheRandomStream() {
    for (double radius : {0.1, 1.0, 3.0}) {
        const SubstrateGeometry geometry(radius);
        for (std::int64_t seed : {0LL, 1337LL, 1989LL, 987654321LL}) {
            SimulationRandom actualRandom;
            SimulationRandom referenceRandom;
            actualRandom.setSeed(seed);
            referenceRandom.setSeed(seed);
            for (int index = 0; index < 1024; ++index) {
                double x;
                double y;
                do {
                    x = referenceRandom.nextUniformSymmetric() * radius;
                    y = referenceRandom.nextUniformSymmetric() * radius;
                } while (x * x + y * y > radius * radius);
                const auto point = geometry.samplePoint(actualRandom);
                assertSame(x, point[0], "Sampled X must not change");
                assertSame(y, point[1], "Sampled Y must not change");
                assertTrue(actualRandom.getSeed() == referenceRandom.getSeed(), "Sampling must consume exactly the original random draws");
            }
        }
    }
}

void testRingClassificationAndLimits() {
    const SubstrateGeometry geometry(1.0, 0.5);
    assertTrue(geometry.isValid(CellData::MAX_CELL_RADIUS), "Ordinary ring is valid");
    assertTrue(!SubstrateGeometry(1.0, 0.95).isValid(CellData::MAX_CELL_RADIUS), "A cell must not touch both walls");
    assertTrue(!SubstrateGeometry(1.0, 0.01).isValid(CellData::MAX_CELL_RADIUS), "A cell must not enclose the hole");
    assertTrue(!SubstrateGeometry(1.0, -0.5).isValid(CellData::MAX_CELL_RADIUS), "Negative hole is invalid");
    assertTrue(!SubstrateGeometry(1.0, std::numeric_limits<double>::quiet_NaN()).isValid(CellData::MAX_CELL_RADIUS), "Nonfinite hole is invalid");
    assertTrue(geometry.isOutside(0.0, 0.0) && geometry.isOutside(0.49, 0.0), "Hole is outside the substrate");
    assertTrue(!geometry.isOutside(0.5, 0.0) && geometry.isOutsideOrOnBoundary(0.5, 0.0), "Inner edge retains strict/inclusive policy");
    assertTrue(!geometry.isOutsideOrOnBoundary(0.75, 0.0), "Annular interior is accessible");
    assertSame(Environment::PI * 0.75, geometry.area(), "Annular area excludes the hole");
    Environment environment = Environment::constructDefaultEnvironment(4, 4, 1.0);
    environment.mInnerRadius = 0.5;
    Environment copy;
    copy.set(environment);
    assertSame(0.5, copy.geometry().innerRadius(), "Environment copies preserve the hole");
}

void testBothRingWallsAndRenderChordOrientation() {
    const SubstrateGeometry geometry(1.0, 0.5);
    constexpr double cellRadius = 0.03;
    for (double angle : {0.0, 0.6, -1.2, 2.9}) {
        for (double centerRadius : {0.51, 0.99}) {
            const double x = centerRadius * std::cos(angle);
            const double y = centerRadius * std::sin(angle);
            const bool inner = centerRadius < 0.75;
            const auto location = geometry.boundaryLocation(x, y);
            assertTrue((x * location.normalX + y * location.normalY < 0.0) == inner, "Normal points into the selected wall");
            cellularity::SubstrateBoundaryContact contact;
            assertTrue(geometry.cellBoundaryContact(x, y, cellRadius, contact), "Both ring walls must generate contacts");
            assertTrue(std::isfinite(contact.chordLength) && contact.chordLength > 0.0, "Ring contact chord is finite");
            assertSame(inner ? 0.5 : 1.0, contact.wallRadius, "Contact identifies its boundary");
            const auto chord = geometry.cellBoundaryChord(x, y, cellRadius, contact);
            for (auto point : {std::array<double, 2>{chord.x1, chord.y1}, {chord.x2, chord.y2}}) {
                cellularity::tests::assertNear(contact.wallRadius, std::hypot(x + point[0], y + point[1]), 1e-12, "Chord endpoint is on the wall");
                cellularity::tests::assertNear(cellRadius, std::hypot(point[0], point[1]), 1e-12, "Chord endpoint is on the cell");
            }
            assertTrue(chord.x1 * chord.y2 - chord.y1 * chord.x2 > 0.0, "Inner and outer chords retain the visible half-plane's winding");
            const auto anchor = geometry.boundaryAnchor(x, y, contact.centerDistance, 0.003);
            cellularity::tests::assertNear(inner ? 0.497 : 1.003, std::hypot(anchor[0], anchor[1]), 1e-12, "Adhesion anchor lies inside its wall");
        }
    }
    cellularity::SubstrateBoundaryContact contact;
    assertTrue(!geometry.cellBoundaryContact(0.75, 0.0, cellRadius, contact), "Ring midpoint has no boundary contact");
    assertTrue(!geometry.cellBoundaryContact(0.0, 0.0, cellRadius, contact), "Frozen cell inside the hole must not produce a nonfinite chord");
}

void testRingSamplingIsUniformInArea() {
    for (double innerRadius : {0.1, 0.5, 0.93}) {
        const SubstrateGeometry geometry(1.0, innerRadius);
        SimulationRandom random;
        random.setSeed(1989);
        double meanDistanceSq = 0.0;
        double meanX = 0.0;
        for (int index = 0; index < 10000; ++index) {
            const auto point = geometry.samplePoint(random);
            assertTrue(!geometry.isOutside(point[0], point[1]), "Ring spawns never enter the hole or leave the dish");
            meanDistanceSq += (point[0] * point[0] + point[1] * point[1]) / 10000.0;
            meanX += point[0] / 10000.0;
        }
        cellularity::tests::assertNear((1.0 + innerRadius * innerRadius) * 0.5, meanDistanceSq, 0.01, "Ring samples are area-uniform, not radius-uniform");
        cellularity::tests::assertNear(0.0, meanX, 0.02, "Ring spawns are angularly unbiased");
    }
}

void testRingForcesAndBoundaryAdhesion() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mInnerRadius = 0.5;
    SimulationWorld world(false, environment);
    for (double centerRadius : {0.505, 0.995}) {
        const int cell = cellularity::tests::addAliveCell(world.mCellData, CellType::GLUEOCYTE);
        world.mCellData.mPositionX[cell] = centerRadius;
        world.mCellData.mPositionY[cell] = 0.0;
        world.mCellData.handleCellToBoundaryCollisionPartial(cell, world.mEnvironment);
        assertTrue(std::isfinite(world.mCellData.mForceX[cell]), "Ring force must remain finite");
        assertTrue((world.mCellData.mForceX[cell] > 0.0) == (centerRadius < 0.75), "Inner wall repels outward and outer wall repels inward");
        assertTrue(world.mCellData.mTopology.hasBoundaryLink(cell), "Glueocyte attaches to both ring walls");
    }
    const auto boundaries = world.mCellData.mTopology.boundaryView();
    assertTrue(boundaries.anchorX[0] < 0.5 && boundaries.anchorX[1] > 1.0, "Boundary anchors identify their own walls");
    assertTrue(world.mCellData.mTopology.validate(), "Both wall links remain topologically valid");
}

void testRingFoodReflectionAndHoleExclusion() {
    Environment environment = Environment::constructDefaultEnvironment(1, 4, 1.0);
    environment.mInnerRadius = 0.5;
    environment.mobileFood = true;
    environment.mViscosity = 0.0;
    SimulationWorld world(false, environment);
    assertTrue(world.mFoodData.addFood(0.0, 0.0, 0.001, 0.0f, world.mEnvironment) < 0, "Food insertion rejects the hole");
    const int inner = world.mFoodData.addFood(0.501, 0.0, 0.001, 0.0f, world.mEnvironment);
    const int outer = world.mFoodData.addFood(0.999, 0.0, 0.001, 0.0f, world.mEnvironment);
    world.mFoodData.mVelocityX[inner] = -0.1f;
    world.mFoodData.mVelocityX[outer] = 0.1f;
    world.eatFoods(0.02);
    assertTrue(world.mFoodData.mVelocityX[inner] > 0.0f && world.mFoodData.mVelocityX[outer] < 0.0f, "Mobile food reflects off each wall toward the annular interior");
    assertTrue(std::isfinite(world.mFoodData.mVelocityX[inner]) && std::isfinite(world.mFoodData.mPositionX[inner]), "Inner reflection is finite");
    world.mFoodData.mPositionX[inner] = 0.75f;
    world.mFoodData.mPositionY[inner] = 0.0f;
    world.mFoodData.mVelocityX[inner] = -15.0f;
    world.eatFoods(0.1);
    assertTrue(world.mFoodData.mPositionX[inner] > 0.49f && world.mFoodData.mVelocityX[inner] > 0.0f, "Fast food cannot tunnel across the entire hole");
}

void testRingWallSmellAndRemovalPolicies() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    environment.mInnerRadius = 0.5;
    SimulationWorld world(false, environment);
    for (CellType type : {CellType::SENSEOCYTE, CellType::STEREOCYTE}) {
        for (double centerRadius : {0.505, 0.995}) {
            const int cell = cellularity::tests::addAliveCell(world.mCellData, type);
            world.mCellData.mPositionX[cell] = centerRadius;
            world.mCellData.mAngle[cell] = Environment::PI * 0.5;
            world.mCellData.editActiveGene(cell, [](cellularity::GeneBuilder& builder) {
                builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 1.0f);
                builder.setIntegerValue(Gene::SMELL_OUT_INTEGER_INDEX, 0);
                builder.setIntegerValue(Gene::SMELL_TYPE_INTEGER_INDEX, SimulationWorld::SMELL_TYPE_WALL);
            });
        }
    }
    world.updateGrids();
    for (int cell = 0; cell < world.mCellData.mCellCount; ++cell) {
        world.updateCellSmellInteraction(cell);
    }
    assertTrue(world.mCellData.mSignalSubstanceOut[0][0] > 0.0f && world.mCellData.mSignalSubstanceOut[1][0] > 0.0f, "Senseocytes recognize both walls");
    assertTrue(world.mCellData.mSignalSubstanceOut[2][0] > 0.0f && world.mCellData.mSignalSubstanceOut[3][0] < 0.0f, "Stereocytes recognize the opposite directions of the two walls");
    world.mCellData.mPositionX[0] = 0.0;
    world.removeDeadCells();
    assertTrue(world.mCellData.mCellCount == 3, "A cell center inside the hole follows the original outside-wall removal policy");
    world.mEnvironment.dieEdge = true;
    const int food = world.mFoodData.addFood(0.501, 0.0, 0.001, 0.0f, world.mEnvironment);
    world.mFoodData.mVelocityX[food] = -0.1f;
    world.mEnvironment.mobileFood = true;
    world.eatFoods(0.02);
    assertTrue(world.mFoodData.mFoodCount == 0, "dieEdge applies to the inner wall as well");
}

void testRingSimulationRemainsFiniteAcrossUpdates() {
    Environment environment = Environment::constructDefaultEnvironment(32, 64, 0.3);
    environment.mInnerRadius = 0.15;
    environment.mobileFood = true;
    environment.aging = false;
    environment.mNitrates = 1.0;
    environment.mRadiation = 0.0;
    environment.mTestIdleMetabolismScale = 0.0;
    environment.mTestFlagellocyteCostScale = 0.0;
    environment.mRandom.setSeed(1989);
    SimulationWorld world(false, environment);
    const std::array<CellType, 4> types{CellType::GLUEOCYTE, CellType::CILIOCYTE, CellType::PHOTOCYTE, CellType::PHAGOCYTE};
    for (int index = 0; index < 8; ++index) {
        const int cell = cellularity::tests::addAliveCell(world.mCellData, types[static_cast<std::size_t>(index % 4)]);
        // Match production seed-cell initialization; the generic helper leaves
        // the telomere counter at zero, which kills both children on division.
        world.mCellData.mTelomeresCount[cell] = -1;
        const double angle = index * Environment::PI * 0.25;
        const double centerRadius = index < 4 ? 0.155 : 0.295;
        world.mCellData.mPositionX[cell] = centerRadius * std::cos(angle);
        world.mCellData.mPositionY[cell] = centerRadius * std::sin(angle);
    }
    for (int update = 0; update < 1000; ++update) {
        world.update(false, 1.0);
        assertTrue(world.mCellData.mTopology.validate(), "Ring topology stays valid across full simulation updates");
        for (int cell = 0; cell < world.mCellData.mCellCount; ++cell) {
            assertTrue(std::isfinite(world.mCellData.mPositionX[cell]) && std::isfinite(world.mCellData.mPositionY[cell])
                            && std::isfinite(world.mCellData.mForceX[cell]) && std::isfinite(world.mCellData.mForceY[cell]),
                    "Ring simulation does not accumulate nonfinite cell state");
        }
        for (int food = 0; food < world.mFoodData.mFoodCount; ++food) {
            assertTrue(std::isfinite(world.mFoodData.mPositionX[food]) && std::isfinite(world.mFoodData.mPositionY[food]), "Ring food stays finite across full updates");
        }
    }
    assertTrue(world.mCellData.mCellCount > 0, "The ring smoke scenario retains living cells");
}

} // namespace

int main() {
    testDescriptionHasOneSizeSourceAndNoOwnedAllocations();
    testPointClassificationPreservesEveryEdgeRule();
    testFoodClassificationRetainsFloatArithmetic();
    testBoundaryLocationsMatchTheExistingRadialGeometry();
    testContactsAndRenderChordsMatchTheOriginalFormulas();
    testAnchorAndFoodProjectionKeepTheirOperationOrder();
    testSamplingPreservesCoordinatesAndTheRandomStream();
    testRingClassificationAndLimits();
    testBothRingWallsAndRenderChordOrientation();
    testRingSamplingIsUniformInArea();
    testRingForcesAndBoundaryAdhesion();
    testRingFoodReflectionAndHoleExclusion();
    testRingWallSmellAndRemovalPolicies();
    testRingSimulationRemainsFiniteAcrossUpdates();
    std::cout << "14 substrate geometry cases passed\n";
    return 0;
}
