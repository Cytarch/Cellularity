#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>

#include "app/EnvironmentFieldPreviewEndpoint.h"
#include "app/EnvironmentFieldPreviewState.h"
#include "app/NativeCellularityController.h"
#include "app/interaction/NativeViewport.h"
#include "core/Environment.h"
#include "tests/support/FakeRenderSurfaceSession.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::app::EnvironmentFieldPreviewEndpoint;
using cellularity::app::EnvironmentFieldPreviewState;
using cellularity::app::EnvironmentFieldPreviewUpdate;
using cellularity::app::EnvironmentFieldPreviewUpdateStatus;
using cellularity::app::EnvironmentFieldPreviewValidationError;
using cellularity::tests::assertTrue;
using cellularity::tests::FakeRenderSurfaceSession;

NativeViewport readyViewport(int width = 640, int height = 360) {
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(width, height);
    viewport.setCamera(0.0f, 0.0f, static_cast<float>(1.0 / 1.1));
    return viewport;
}

EnvironmentFieldPreviewUpdate scalarUpdate(std::int32_t target,
        std::int32_t fieldType, double seed = 1.0) {
    return {target, fieldType, seed + 0.1, seed + 0.2, seed + 0.3,
            seed + 0.4, seed + 0.5};
}

EnvironmentFieldPreviewUpdate gravityUpdate(std::int32_t fieldType,
        double strength = 0.75, double angle = -0.25) {
    return {cellularity::app::kEnvironmentFieldTargetGravity, fieldType,
            0.0, strength, angle, 0.0, 0.0};
}

void testWireValuesAndEveryTargetTypeMapExactly() {
    static_assert(cellularity::app::kEnvironmentFieldTargetDensity == 0);
    static_assert(cellularity::app::kEnvironmentFieldTargetSalinity == 1);
    static_assert(cellularity::app::kEnvironmentFieldTargetFoodLipidLayer == 2);
    static_assert(cellularity::app::kEnvironmentFieldTargetGravity == 3);
    static_assert(cellularity::app::kEnvironmentScalarFieldConstant == 0);
    static_assert(
            cellularity::app::kEnvironmentScalarFieldLinearDirectional == 1);
    static_assert(cellularity::app::kEnvironmentScalarFieldRadial == 2);
    static_assert(
            cellularity::app::kEnvironmentVectorFieldLinearDirectional == 0);
    static_assert(cellularity::app::kEnvironmentVectorFieldRadialInward == 1);
    static_assert(cellularity::app::kEnvironmentVectorFieldRadialOutward == 2);

    NativeViewport viewport = readyViewport();
    constexpr std::array scalarTargets{
            cellularity::app::kEnvironmentFieldTargetDensity,
            cellularity::app::kEnvironmentFieldTargetSalinity,
            cellularity::app::kEnvironmentFieldTargetFoodLipidLayer,
    };
    constexpr std::array scalarTypes{
            cellularity::app::kEnvironmentScalarFieldConstant,
            cellularity::app::kEnvironmentScalarFieldLinearDirectional,
            cellularity::app::kEnvironmentScalarFieldRadial,
    };
    constexpr std::array mappedScalarTypes{
            Environment::DENSITY_FIELD_CONSTANT,
            Environment::DENSITY_FIELD_LINEAR_DIRECTIONAL,
            Environment::DENSITY_FIELD_RADIAL,
    };

    std::uint64_t revision = 1;
    for (const std::int32_t target : scalarTargets) {
        for (std::size_t typeIndex = 0; typeIndex < scalarTypes.size();
                ++typeIndex) {
            const auto update = scalarUpdate(
                    target, scalarTypes[typeIndex], static_cast<double>(revision));
            const auto command =
                    EnvironmentFieldPreviewState{revision++, update}
                            .toRenderCommand(viewport);
            assertTrue(command.has_value() && !command->vectorPreview
                            && command->fieldType
                                    == mappedScalarTypes[typeIndex]
                            && command->fieldBase == update.base
                            && command->fieldStrength == update.strength
                            && command->fieldAngle == update.angle
                            && command->fieldOffset == update.offset
                            && command->fieldSoftness == update.softness
                            && command->dishRadius == 1.0
                            && command->surfaceWidth == 640
                            && command->surfaceHeight == 360,
                    "Every scalar target/type maps every value without ordinal casts");
            const bool density = target
                    == cellularity::app::kEnvironmentFieldTargetDensity;
            assertTrue(command->displayMin == (density ? -3.3 : 0.0)
                            && command->displayMax == (density ? 3.3 : 1.0),
                    "Target-specific preview ranges preserve production output");
        }
    }

    constexpr std::array vectorTypes{
            cellularity::app::kEnvironmentVectorFieldLinearDirectional,
            cellularity::app::kEnvironmentVectorFieldRadialInward,
            cellularity::app::kEnvironmentVectorFieldRadialOutward,
    };
    constexpr std::array mappedVectorTypes{
            Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL,
            Environment::VECTOR_FIELD_RADIAL_INWARD,
            Environment::VECTOR_FIELD_RADIAL_OUTWARD,
    };
    for (std::size_t typeIndex = 0; typeIndex < vectorTypes.size();
            ++typeIndex) {
        const auto update = gravityUpdate(vectorTypes[typeIndex]);
        const auto command =
                EnvironmentFieldPreviewState{revision++, update}
                        .toRenderCommand(viewport);
        assertTrue(command.has_value() && command->vectorPreview
                        && command->gravityFieldType
                                == mappedVectorTypes[typeIndex]
                        && command->gravityFieldAngle == update.angle
                        && command->fieldStrength == update.strength
                        && command->dishRadius == 1.0,
                "Every gravity vector mode maps through its own target domain");
    }
}

void testMalformedInputsAreRejectedBeforePublication() {
    EnvironmentFieldPreviewEndpoint endpoint(7);
    auto assertMalformed = [&](const EnvironmentFieldPreviewUpdate& update,
                                   const char* message) {
        const auto result = endpoint.publish(7, update);
        assertTrue(result.status
                                == EnvironmentFieldPreviewUpdateStatus::Malformed
                        && result.ingressRevision == 0
                        && endpoint.retainedState() == nullptr,
                message);
    };

    assertMalformed(scalarUpdate(99, 0), "Unknown target is malformed");
    assertMalformed(
            scalarUpdate(cellularity::app::kEnvironmentFieldTargetDensity, 99),
            "Unknown scalar type is malformed");
    assertMalformed(gravityUpdate(99), "Unknown vector type is malformed");

    auto nonFinite = scalarUpdate(
            cellularity::app::kEnvironmentFieldTargetSalinity,
            cellularity::app::kEnvironmentScalarFieldRadial);
    nonFinite.softness = std::numeric_limits<double>::quiet_NaN();
    assertMalformed(nonFinite, "Non-finite scalar input is malformed");

    auto reserved = gravityUpdate(
            cellularity::app::kEnvironmentVectorFieldRadialInward);
    reserved.offset = 0.1;
    assertMalformed(reserved, "Gravity rejects scalar-only values");
    reserved.offset = -0.0;
    assertMalformed(
            reserved, "Gravity reserved values require canonical positive zero");
}

void testEndpointPublishesLatestWholeStateAndHandlesDuplicates() {
    EnvironmentFieldPreviewEndpoint endpoint(17);
    const auto first = scalarUpdate(
            cellularity::app::kEnvironmentFieldTargetDensity,
            cellularity::app::kEnvironmentScalarFieldLinearDirectional, 10.0);
    const auto second = scalarUpdate(
            cellularity::app::kEnvironmentFieldTargetFoodLipidLayer,
            cellularity::app::kEnvironmentScalarFieldRadial, 20.0);

    const auto stale = endpoint.publish(16, first);
    const auto applied = endpoint.publish(17, first);
    const auto duplicate = endpoint.publish(17, first);
    const auto replaced = endpoint.publish(17, second);
    const auto retained = endpoint.retainedState();
    assertTrue(stale.status
                            == EnvironmentFieldPreviewUpdateStatus::StaleEndpoint
                    && stale.ingressRevision == 0
                    && applied.status
                            == EnvironmentFieldPreviewUpdateStatus::Applied
                    && applied.ingressRevision == 1
                    && duplicate.status
                            == EnvironmentFieldPreviewUpdateStatus::Duplicate
                    && duplicate.ingressRevision == 1
                    && replaced.status
                            == EnvironmentFieldPreviewUpdateStatus::Applied
                    && replaced.ingressRevision == 2
                    && retained != nullptr
                    && retained->ingressRevision == 2
                    && cellularity::app::environmentFieldPreviewUpdatesEqual(
                            retained->value, second),
            "Endpoint assigns revisions only to validated changed whole states");

    endpoint.close();
    const auto closing = endpoint.publish(17, first);
    assertTrue(closing.status
                            == EnvironmentFieldPreviewUpdateStatus::Closing
                    && closing.ingressRevision == 0
                    && endpoint.retainedState() == nullptr,
            "Closing endpoint cannot publish or retain another state");
}

void testRevisionExhaustionNeverWrapsToZero() {
    constexpr std::uint64_t lastRevision =
            std::numeric_limits<std::uint64_t>::max();
    EnvironmentFieldPreviewEndpoint endpoint(23, lastRevision);
    const auto first = scalarUpdate(
            cellularity::app::kEnvironmentFieldTargetDensity,
            cellularity::app::kEnvironmentScalarFieldConstant, 1.0);
    const auto second = scalarUpdate(
            cellularity::app::kEnvironmentFieldTargetDensity,
            cellularity::app::kEnvironmentScalarFieldConstant, 2.0);

    const auto applied = endpoint.publish(23, first);
    const auto duplicate = endpoint.publish(23, first);
    const auto exhausted = endpoint.publish(23, second);
    const auto retained = endpoint.retainedState();
    assertTrue(applied.status
                            == EnvironmentFieldPreviewUpdateStatus::Applied
                    && applied.ingressRevision == lastRevision
                    && duplicate.status
                            == EnvironmentFieldPreviewUpdateStatus::Duplicate
                    && duplicate.ingressRevision == lastRevision
                    && exhausted.status
                            == EnvironmentFieldPreviewUpdateStatus::RevisionExhausted
                    && exhausted.ingressRevision == 0
                    && retained != nullptr
                    && retained->ingressRevision == lastRevision,
            "Near-exhaustion publishes the last non-zero revision and never wraps");
}

void testResultLayoutIsExactLittleEndianEfa1() {
    constexpr std::uint64_t revision = 0x0102030405060708ULL;
    const auto bytes =
            cellularity::app::encodeEnvironmentFieldPreviewUpdateResult(
                    {EnvironmentFieldPreviewUpdateStatus::Duplicate, revision});
    const std::array<std::uint8_t, 24> expected{
            0x45,
            0x46,
            0x41,
            0x31,
            0x01,
            0x00,
            0x18,
            0x00,
            0x01,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x08,
            0x07,
            0x06,
            0x05,
            0x04,
            0x03,
            0x02,
            0x01,
    };
    assertTrue(bytes == expected,
            "EFA1 acknowledgment is exactly 24 little-endian bytes");
}

void testConcurrentReaderNeverObservesTornState() {
    constexpr std::uint64_t updateCount = 20'000;
    EnvironmentFieldPreviewEndpoint endpoint(31);
    std::atomic<bool> producerDone{false};
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        for (std::uint64_t sequence = 1; sequence <= updateCount; ++sequence) {
            const double value = static_cast<double>(sequence);
            EnvironmentFieldPreviewUpdate update{
                    cellularity::app::kEnvironmentFieldTargetDensity,
                    cellularity::app::kEnvironmentScalarFieldLinearDirectional,
                    value + 0.1,
                    value + 0.2,
                    value + 0.3,
                    value + 0.4,
                    value + 0.5,
            };
            const auto result = endpoint.publish(31, update);
            if (result.status
                            != EnvironmentFieldPreviewUpdateStatus::Applied
                    || result.ingressRevision != sequence) {
                failed.store(true, std::memory_order_release);
                break;
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t previousRevision = 0;
        while (!producerDone.load(std::memory_order_acquire)
                || previousRevision < updateCount) {
            if (failed.load(std::memory_order_acquire)) {
                break;
            }
            const auto state = endpoint.retainedState();
            if (state == nullptr) {
                std::this_thread::yield();
                continue;
            }
            const double value = static_cast<double>(state->ingressRevision);
            if (state->ingressRevision < previousRevision
                    || state->value.base != value + 0.1
                    || state->value.strength != value + 0.2
                    || state->value.angle != value + 0.3
                    || state->value.offset != value + 0.4
                    || state->value.softness != value + 0.5) {
                failed.store(true, std::memory_order_release);
                break;
            }
            previousRevision = state->ingressRevision;
        }
    });

    producer.join();
    consumer.join();
    assertTrue(!failed.load(std::memory_order_acquire)
                    && endpoint.retainedState()->ingressRevision == updateCount,
            "Latest-only pointer publication never exposes mixed update fields");
}

void testControllerReusesRetainedStateAcrossSurfaceResize() {
    const Environment environment =
            Environment::setupPreviewSimulationEnvironment(1);
    NativeCellularityController controller(true, environment);
    auto backend = std::make_unique<FakeRenderSurfaceSession>();
    FakeRenderSurfaceSession* fake = backend.get();
    controller.installRenderSurfaceSession(std::move(backend));
    controller.setViewportSurfaceSize(320, 180);
    controller.setViewportCamera(
            0.0f, 0.0f, static_cast<float>(1.0 / 1.1));

    const auto endpoint = controller.environmentFieldPreviewEndpoint();
    const auto update = scalarUpdate(
            cellularity::app::kEnvironmentFieldTargetSalinity,
            cellularity::app::kEnvironmentScalarFieldRadial, 4.0);
    const auto published = endpoint->publish(endpoint->generation(), update);
    const auto first = controller.drawEnvironmentFieldPreview();
    controller.setViewportSurfaceSize(800, 600);
    const auto recreated = controller.drawEnvironmentFieldPreview();

    assertTrue(published.status
                            == EnvironmentFieldPreviewUpdateStatus::Applied
                    && cellularity::render::isPrimaryContentPresented(first)
                    && cellularity::render::isPrimaryContentPresented(recreated)
                    && fake->environmentPreviewCount == 2
                    && fake->lastEnvironmentPreview.surfaceWidth == 800
                    && fake->lastEnvironmentPreview.surfaceHeight == 600
                    && fake->lastEnvironmentPreview.fieldBase == update.base
                    && fake->lastEnvironmentPreview.fieldType
                            == Environment::DENSITY_FIELD_RADIAL,
            "Surface resize rebuilds the command from retained state and current viewport");
}

} // namespace

int main() {
    testWireValuesAndEveryTargetTypeMapExactly();
    testMalformedInputsAreRejectedBeforePublication();
    testEndpointPublishesLatestWholeStateAndHandlesDuplicates();
    testRevisionExhaustionNeverWrapsToZero();
    testResultLayoutIsExactLittleEndianEfa1();
    testConcurrentReaderNeverObservesTornState();
    testControllerReusesRetainedStateAcrossSurfaceResize();
    std::cout << "Environment Field Preview state tests passed\n";
    return 0;
}
