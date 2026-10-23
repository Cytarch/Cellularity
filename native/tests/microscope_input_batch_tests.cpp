#include "app/NativeInteractionController.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeInputBatchLayout.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "tests/support/TestAssertions.h"

namespace {

using cellularity::microscope::MicroscopeCommandApplyResult;
using cellularity::microscope::MicroscopeInputBatch;
using cellularity::microscope::MicroscopeInputBatchResult;
using cellularity::microscope::MicroscopeInputCommandKind;
using cellularity::microscope::MicroscopeInputCommandWireStatus;
using cellularity::microscope::MicroscopeInputIngressStatus;
using cellularity::tests::assertNear;
using cellularity::tests::assertTrue;

void writeU16(std::uint8_t* bytes, std::uint16_t value) {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void writeU64(std::uint8_t* bytes, std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint16_t readU16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0])
            | static_cast<std::uint16_t>(bytes[1]) << 8U;
}

std::uint32_t readU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0])
            | static_cast<std::uint32_t>(bytes[1]) << 8U
            | static_cast<std::uint32_t>(bytes[2]) << 16U
            | static_cast<std::uint32_t>(bytes[3]) << 24U;
}

std::uint64_t readU64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t floatBits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::array<std::uint8_t,
        cellularity::microscope::kMicroscopeInputBufferCapacity>
encodedBatch(std::size_t commandCount) {
    using namespace cellularity::microscope;
    std::array<std::uint8_t, kMicroscopeInputBufferCapacity> bytes{};
    writeU32(bytes.data(), kMicroscopeInputBatchMagic);
    writeU16(bytes.data() + 4, kMicroscopeInputBatchVersion);
    writeU16(bytes.data() + 6,
            static_cast<std::uint16_t>(kMicroscopeInputHeaderSize));
    writeU16(bytes.data() + 8,
            static_cast<std::uint16_t>(kMicroscopeInputCommandRecordSize));
    writeU16(bytes.data() + 10, static_cast<std::uint16_t>(commandCount));
    writeU64(bytes.data() + 16, 0x8877665544332211ULL);
    writeU64(bytes.data() + 24, 123'000'000ULL);
    writeU64(bytes.data() + 32, 0xFEDCBA9876543210ULL);
    writeU64(bytes.data() + 40, 987'654'321ULL);
    for (std::size_t index = 0; index < commandCount; ++index) {
        std::uint8_t* record = bytes.data() + kMicroscopeInputHeaderSize
                + index * kMicroscopeInputCommandRecordSize;
        writeU16(record,
                static_cast<std::uint16_t>(MicroscopeInputCommandKind::Pan));
        writeU32(record + 8, floatBits(static_cast<float>(index + 1)));
        writeU32(record + 12, floatBits(-static_cast<float>(index + 1)));
    }
    return bytes;
}

void setCommand(std::array<std::uint8_t,
                        cellularity::microscope::kMicroscopeInputBufferCapacity>& bytes,
        std::size_t index, MicroscopeInputCommandKind kind, int pointerId,
        float value0 = 0.0F, float value1 = 0.0F, float value2 = 0.0F) {
    using namespace cellularity::microscope;
    std::uint8_t* record = bytes.data() + kMicroscopeInputHeaderSize
            + index * kMicroscopeInputCommandRecordSize;
    writeU16(record, static_cast<std::uint16_t>(kind));
    writeU32(record + 4, static_cast<std::uint32_t>(pointerId));
    writeU32(record + 8, floatBits(value0));
    writeU32(record + 12, floatBits(value1));
    writeU32(record + 16, floatBits(value2));
}

void testExactCib1DecodeAndCapacityBoundary() {
    using namespace cellularity::microscope;
    static_assert(kMicroscopeInputHeaderSize == 48);
    static_assert(kMicroscopeInputCommandRecordSize == 32);
    static_assert(kMicroscopeInputCommandCapacity == 64);
    static_assert(kMicroscopeInputBufferCapacity == 2096);

    auto bytes = encodedBatch(kMicroscopeInputCommandCapacity);
    MicroscopeInputBatch decoded;
    assertTrue(decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded)
                    && decoded.commandCount == kMicroscopeInputCommandCapacity
                    && decoded.sequence == 0x8877665544332211ULL
                    && decoded.eventTimeNanos == 123'000'000
                    && decoded.expectedInputEpoch == 0xFEDCBA9876543210ULL
                    && decoded.profilingIngressTimestampNanos == 987'654'321
                    && decoded.commands.front().kind
                            == MicroscopeInputCommandKind::Pan
                    && decoded.commands.back().values[0] == 64.0F,
            "CIB1 decodes all 64 fixed records with exact little-endian fields");
}

void testMalformedBatchIsRejectedBeforeDispatch() {
    using namespace cellularity::microscope;
    MicroscopeInputBatch decoded;

    auto bytes = encodedBatch(1);
    assertTrue(!decodeMicroscopeInputBatch(nullptr, bytes.size(), decoded),
            "A null CIB1 address is malformed");
    assertTrue(!decodeMicroscopeInputBatch(
                       bytes.data(), kMicroscopeInputHeaderSize - 1, decoded),
            "A short CIB1 header is malformed");

    bytes = encodedBatch(1);
    writeU16(bytes.data() + 10,
            static_cast<std::uint16_t>(kMicroscopeInputCommandCapacity + 1));
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "A CIB1 count above the 64-command ingress limit is malformed");

    bytes = encodedBatch(2);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(),
                       kMicroscopeInputHeaderSize
                               + kMicroscopeInputCommandRecordSize,
                       decoded),
            "A CIB1 body shorter than its declared record count is malformed");

    bytes = encodedBatch(1);
    writeU32(bytes.data() + 12, 1);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "Unknown CIB1 flags are malformed");
    assertTrue(readMicroscopeInputBatchSequence(bytes.data(), bytes.size())
                    == 0x8877665544332211ULL,
            "Malformed CIB1 still exposes its correlation sequence");

    bytes = encodedBatch(1);
    writeU16(bytes.data() + 10, 0);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "An empty CIB1 batch is malformed");

    bytes = encodedBatch(1);
    writeU16(bytes.data() + kMicroscopeInputHeaderSize, 99);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "An unknown CIB1 command kind is malformed");

    bytes = encodedBatch(1);
    writeU32(bytes.data() + kMicroscopeInputHeaderSize + 16, 0x80000000U);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "Unused CIB1 float slots require canonical positive zero");

    bytes = encodedBatch(1);
    setCommand(bytes, 0, MicroscopeInputCommandKind::Down, 32, 1.0F, 2.0F);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "Pointer identifiers above Android's fixed range are malformed");

    bytes = encodedBatch(1);
    setCommand(bytes, 0, MicroscopeInputCommandKind::Zoom, 0, 0.0F, 1.0F, 2.0F);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "A non-positive zoom factor is malformed");

    bytes = encodedBatch(2);
    setCommand(bytes, 0, MicroscopeInputCommandKind::Tap, 0, 1.0F, 2.0F);
    setCommand(bytes, 1, MicroscopeInputCommandKind::Tap, 0, 3.0F, 4.0F);
    assertTrue(!decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded),
            "CIB1 permits at most one inline-payload tap");
}

void testProfilingTimestampIsDiagnosticMetadataOnly() {
    using namespace cellularity::microscope;
    for (const std::int64_t timestamp : {
                 std::int64_t{0}, std::int64_t{-1},
                 std::numeric_limits<std::int64_t>::max()
         }) {
        auto bytes = encodedBatch(1);
        writeU64(bytes.data() + 40, static_cast<std::uint64_t>(timestamp));
        MicroscopeInputBatch decoded;
        assertTrue(decodeMicroscopeInputBatch(bytes.data(), bytes.size(), decoded)
                        && decoded.profilingIngressTimestampNanos == timestamp,
                "V-24 profiling timestamps never affect input admission");
    }
}

void testCir1AndCms1WritersAreCanonical() {
    using namespace cellularity::microscope;
    static_assert(kMicroscopeInputResultBufferCapacity == 320);
    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 9, 11, MicroscopeInputIngressStatus::Applied);
    result.commandCount = 2;
    result.aggregateFlags = kMicroscopeInputAggregateCameraChanged
            | kMicroscopeInputAggregateZoomDisplaySizeChanged;
    result.viewportRevision = 13;
    result.presentationMotionRevision = 17;
    result.zoomDisplaySizeBits = floatBits(0.75F);
    result.commandStatuses[0] =
            MicroscopeInputCommandWireStatus::WorldAccepted;
    result.commandStatuses[1] =
            MicroscopeInputCommandWireStatus::PresentationApplied;

    std::array<std::uint8_t, kMicroscopeInputResultBufferCapacity> bytes;
    bytes.fill(0xA5U);
    writeMicroscopeInputBatchResult(bytes.data(), result);
    assertTrue(readU32(bytes.data()) == kMicroscopeInputResultMagic
                    && readU16(bytes.data() + 4)
                            == kMicroscopeInputBatchVersion
                    && readU16(bytes.data() + 6)
                            == kMicroscopeInputResultHeaderSize
                    && readU16(bytes.data() + 10) == 2
                    && readU64(bytes.data() + 16) == 9
                    && readU64(bytes.data() + 24) == 11
                    && readU64(bytes.data() + 32) == 13
                    && readU64(bytes.data() + 40) == 17
                    && readU32(bytes.data() + 48) == floatBits(0.75F)
                    && readU32(bytes.data() + 64) == 1
                    && readU32(bytes.data() + 68) == 9
                    && readU32(bytes.data() + 72) == 0
                    && readU64(bytes.data() + 312) == 0,
            "CIR1 overwrites the full fixed result including every unused slot");

    PresentationMotionSnapshot motion{23, 29, true, true};
    std::array<std::uint8_t, kPresentationMotionSnapshotSize> motionBytes;
    motionBytes.fill(0xA5U);
    writePresentationMotionSnapshot(motionBytes.data(), motion);
    assertTrue(readU32(motionBytes.data()) == kPresentationMotionSnapshotMagic
                    && readU16(motionBytes.data() + 6)
                            == kPresentationMotionSnapshotSize
                    && readU64(motionBytes.data() + 8) == 23
                    && readU64(motionBytes.data() + 16) == 29
                    && readU32(motionBytes.data() + 24) == 3
                    && readU32(motionBytes.data() + 28) == 0,
            "CMS1 is a canonical fixed 32-byte motion snapshot");
}

MicroscopeInputBatch interactionBatch() {
    MicroscopeInputBatch batch;
    batch.sequence = 1;
    batch.eventTimeNanos = 10;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 2;
    batch.commands[0].kind = MicroscopeInputCommandKind::Pan;
    batch.commands[0].values = {10.0F, -5.0F, 0.0F, 0.0F};
    batch.commands[1].kind = MicroscopeInputCommandKind::Zoom;
    batch.commands[1].values = {1.1F, 320.0F, 180.0F, 0.0F};
    return batch;
}

void testPresentationBatchPublishesOnlyTheFinalViewport() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);
    const auto before = controller.presentationStateSnapshot();

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 1, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(interactionBatch(), nullptr, result);
    const auto after = controller.presentationStateSnapshot();

    assertTrue(result.ingressStatus == MicroscopeInputIngressStatus::Applied
                    && result.commandCount == 2
                    && result.commandStatuses[0]
                            == MicroscopeInputCommandWireStatus::PresentationApplied
                    && result.commandStatuses[1]
                            == MicroscopeInputCommandWireStatus::PresentationApplied
                    && (result.aggregateFlags
                               & kMicroscopeInputAggregateCameraChanged)
                            != 0
                    && result.zoomDisplaySizeBits
                            == floatBits(
                                    controller.viewport().viewWidth() * 0.0005F)
                    && after.glViewportExchange.published
                            == before.glViewportExchange.published + 1,
            "Pan and Zoom publish one final viewport and preserve the zoom-display formula");
}

void testTwoPointerMoveIsOnePresentationTransaction() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);

    MicroscopeInputBatch downBatch;
    downBatch.sequence = 9;
    downBatch.eventTimeNanos = 90;
    downBatch.expectedInputEpoch = 7;
    downBatch.commandCount = 2;
    downBatch.commands[0].kind = MicroscopeInputCommandKind::Down;
    downBatch.commands[0].pointerId = 0;
    downBatch.commands[0].values = {100.0F, 100.0F, 0.0F, 0.0F};
    downBatch.commands[1].kind = MicroscopeInputCommandKind::Down;
    downBatch.commands[1].pointerId = 1;
    downBatch.commands[1].values = {200.0F, 100.0F, 0.0F, 0.0F};
    MicroscopeInputBatchResult downResult;
    initializeMicroscopeInputBatchResult(
            downResult, 9, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(downBatch, nullptr, downResult);

    MicroscopeInputBatch moveBatch;
    moveBatch.sequence = 10;
    moveBatch.eventTimeNanos = 100;
    moveBatch.expectedInputEpoch = 7;
    moveBatch.commandCount = 4;
    moveBatch.commands[0].kind = MicroscopeInputCommandKind::Move;
    moveBatch.commands[0].pointerId = 0;
    moveBatch.commands[0].values = {102.0F, 104.0F, 0.0F, 0.0F};
    moveBatch.commands[1].kind = MicroscopeInputCommandKind::Move;
    moveBatch.commands[1].pointerId = 1;
    moveBatch.commands[1].values = {213.0F, 104.0F, 0.0F, 0.0F};
    moveBatch.commands[2].kind = MicroscopeInputCommandKind::Pan;
    moveBatch.commands[2].values = {7.5F, 4.0F, 0.0F, 0.0F};
    moveBatch.commands[3].kind = MicroscopeInputCommandKind::Zoom;
    moveBatch.commands[3].values = {1.1F, 157.5F, 104.0F, 0.0F};

    const auto before = controller.presentationStateSnapshot();
    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 10, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(moveBatch, nullptr, result);
    const auto after = controller.presentationStateSnapshot();

    assertTrue(result.commandStatuses[0]
                            == MicroscopeInputCommandWireStatus::WorldAccepted
                    && result.commandStatuses[1]
                            == MicroscopeInputCommandWireStatus::WorldAccepted
                    && result.commandStatuses[2]
                            == MicroscopeInputCommandWireStatus::PresentationApplied
                    && result.commandStatuses[3]
                            == MicroscopeInputCommandWireStatus::PresentationApplied
                    && after.glViewportExchange.published
                            == before.glViewportExchange.published + 1,
            "Two-pointer Move, Move, Pan and Zoom remain one coherent publication");
}

void testSemanticRejectionPreservesPrefixAndLaterCommands() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);

    MicroscopeInputBatch batch;
    batch.sequence = 3;
    batch.eventTimeNanos = 30;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 3;
    batch.commands[0].kind = MicroscopeInputCommandKind::Down;
    batch.commands[0].pointerId = 5;
    batch.commands[0].values = {100.0F, 90.0F, 0.0F, 0.0F};
    batch.commands[1] = batch.commands[0];
    batch.commands[2].kind = MicroscopeInputCommandKind::Pan;
    batch.commands[2].values = {12.0F, -4.0F, 0.0F, 0.0F};

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 3, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(batch, nullptr, result);

    assertTrue(result.ingressStatus == MicroscopeInputIngressStatus::Applied
                    && result.commandStatuses[0]
                            == MicroscopeInputCommandWireStatus::WorldAccepted
                    && result.commandStatuses[1]
                            == MicroscopeInputCommandWireStatus::WorldRejectedPointerState
                    && result.commandStatuses[2]
                            == MicroscopeInputCommandWireStatus::PresentationApplied
                    && controller.activePointerGenerationCount() == 1
                    && (result.aggregateFlags
                               & kMicroscopeInputAggregateCameraChanged)
                            != 0,
            "A semantic rejection neither rolls back its prefix nor skips later commands");
}

void testStalePlacementGenomeRejectsOnlyTapRecord() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);

    MicroscopeInputBatch batch;
    batch.sequence = 31;
    batch.eventTimeNanos = 310;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 3;
    batch.commands[0].kind = MicroscopeInputCommandKind::Down;
    batch.commands[0].pointerId = 6;
    batch.commands[0].values = {100.0F, 90.0F, 0.0F, 0.0F};
    batch.commands[1].kind = MicroscopeInputCommandKind::Tap;
    batch.commands[1].values = {120.0F, 95.0F, 0.0F, 0.0F};
    batch.commands[2].kind = MicroscopeInputCommandKind::Pan;
    batch.commands[2].values = {12.0F, -4.0F, 0.0F, 0.0F};

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 31, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(batch, nullptr, result, true);

    assertTrue(result.ingressStatus == MicroscopeInputIngressStatus::Applied
                    && result.commandStatuses[0]
                            == MicroscopeInputCommandWireStatus::WorldAccepted
                    && result.commandStatuses[1]
                            == MicroscopeInputCommandWireStatus::
                                    WorldRejectedStalePlacementGenome
                    && result.commandStatuses[2]
                            == MicroscopeInputCommandWireStatus::
                                    PresentationApplied
                    && controller.activePointerGenerationCount() == 1,
            "A stale placement revision rejects only Tap and preserves surrounding commands");
}

void testWorldCommandOrderMatchesBatchRecords() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);

    MicroscopeInputBatch batch;
    batch.sequence = 8;
    batch.eventTimeNanos = 80;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 4;
    batch.commands[0].kind = MicroscopeInputCommandKind::Down;
    batch.commands[0].pointerId = 2;
    batch.commands[0].values = {10.0F, 20.0F, 0.0F, 0.0F};
    batch.commands[1].kind = MicroscopeInputCommandKind::Move;
    batch.commands[1].pointerId = 2;
    batch.commands[1].values = {11.0F, 21.0F, 0.0F, 0.0F};
    batch.commands[2].kind = MicroscopeInputCommandKind::Up;
    batch.commands[2].pointerId = 2;
    batch.commands[3].kind = MicroscopeInputCommandKind::Tap;
    batch.commands[3].values = {30.0F, 40.0F, 0.0F, 0.0F};

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 8, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(batch, nullptr, result);

    std::array<MicroscopeWorldCommandKind, 4> observed{};
    std::size_t observedCount = 0;
    const auto now = microscopeCommandNow();
    controller.commandMailbox().serviceUntil(
            now + MicroscopeCommandDuration(1'000'000'000), 16,
            [&](const MicroscopeWorldCommand& command) {
                if (observedCount < observed.size()) {
                    observed[observedCount++] = command.kind;
                }
                return MicroscopeCommandApplyResult::applied();
            },
            [now] {
                return now;
            });

    const std::array expected{
            MicroscopeWorldCommandKind::TouchDown,
            MicroscopeWorldCommandKind::TouchMovePosition,
            MicroscopeWorldCommandKind::TouchUp,
            MicroscopeWorldCommandKind::Tap,
    };
    assertTrue(observedCount == expected.size() && observed == expected,
            "World commands preserve their exact CIB1 record order");
}

void testInternalFailureStatusImageIsExactAtEveryDispatchPosition() {
    using namespace cellularity::microscope;
    MicroscopeInputBatch batch;
    batch.sequence = 4;
    batch.eventTimeNanos = 40;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 4;

    for (std::size_t failureIndex = 0; failureIndex < batch.commandCount;
            ++failureIndex) {
        MicroscopeInputBatchResult result;
        initializeMicroscopeInputBatchResult(
                result, 4, 7, MicroscopeInputIngressStatus::Malformed);
        try {
            dispatchValidatedMicroscopeInputBatch(
                    batch, result,
                    [failureIndex](std::size_t index,
                            const MicroscopeInputCommandRecord&) {
                        if (index == failureIndex) {
                            throw std::runtime_error("injected dispatch failure");
                        }
                        return MicroscopeInputCommandWireStatus::WorldAccepted;
                    });
            assertTrue(false, "The injected dispatch failure must escape to JNI");
        } catch (const std::runtime_error&) {
            markMicroscopeInputBatchInternalFailure(result, true);
        }

        assertTrue(result.ingressStatus
                                == MicroscopeInputIngressStatus::InternalFailure
                        && result.commandCount == batch.commandCount
                        && result.aggregateFlags == 0
                        && result.viewportRevision == 0
                        && result.presentationMotionRevision == 0
                        && result.zoomDisplaySizeBits == 0,
                "A dispatch failure has a canonical CIR1 header");
        for (std::size_t index = 0; index < batch.commandCount; ++index) {
            const auto expected = index < failureIndex
                    ? MicroscopeInputCommandWireStatus::WorldAccepted
                    : index == failureIndex
                    ? MicroscopeInputCommandWireStatus::IndeterminateInternalFailure
                    : MicroscopeInputCommandWireStatus::NotExecutedInternalFailure;
            assertTrue(result.commandStatuses[index] == expected,
                    "Internal failure preserves exact prefix/current/suffix status");
        }
    }

    MicroscopeInputBatchResult beforeDispatch;
    initializeMicroscopeInputBatchResult(
            beforeDispatch, 5, 7, MicroscopeInputIngressStatus::Malformed);
    beforeDispatch.commandCount = 4;
    beforeDispatch.commandStatuses.fill(
            MicroscopeInputCommandWireStatus::WorldAccepted);
    beforeDispatch.aggregateFlags = kMicroscopeInputAggregateCameraChanged;
    beforeDispatch.viewportRevision = 9;
    beforeDispatch.presentationMotionRevision = 10;
    beforeDispatch.zoomDisplaySizeBits = floatBits(0.5F);
    markMicroscopeInputBatchInternalFailure(beforeDispatch, false);
    assertTrue(beforeDispatch.commandCount == 0
                    && beforeDispatch.ingressStatus
                            == MicroscopeInputIngressStatus::InternalFailure
                    && beforeDispatch.commandStatuses[0]
                            == MicroscopeInputCommandWireStatus::Unused
                    && beforeDispatch.aggregateFlags == 0
                    && beforeDispatch.viewportRevision == 0
                    && beforeDispatch.presentationMotionRevision == 0
                    && beforeDispatch.zoomDisplaySizeBits == 0,
            "A failure before dispatch exposes no executed command or stale aggregate");
}

void testIngressCapacityDoesNotRaiseTheTwentyPointerMailboxLimit() {
    using namespace cellularity::microscope;
    static_assert(MicroscopeCommandMailbox::kPointerCapacity == 20);
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);

    MicroscopeInputBatch batch;
    batch.sequence = 6;
    batch.eventTimeNanos = 60;
    batch.expectedInputEpoch = 7;
    batch.commandCount = MicroscopeCommandMailbox::kPointerCapacity + 1;
    for (std::size_t index = 0; index < batch.commandCount; ++index) {
        batch.commands[index].kind = MicroscopeInputCommandKind::Down;
        batch.commands[index].pointerId = static_cast<int>(index);
        batch.commands[index].values = {
                static_cast<float>(index), static_cast<float>(index), 0.0F, 0.0F
        };
    }

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 6, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(batch, nullptr, result);
    for (std::size_t index = 0;
            index < MicroscopeCommandMailbox::kPointerCapacity; ++index) {
        assertTrue(result.commandStatuses[index]
                        == MicroscopeInputCommandWireStatus::WorldAccepted,
                "Each pointer up to the mailbox limit is accepted");
    }
    assertTrue(result.commandStatuses[MicroscopeCommandMailbox::kPointerCapacity]
                            == MicroscopeInputCommandWireStatus::WorldRejectedPointerCapacity
                    && controller.activePointerGenerationCount()
                            == MicroscopeCommandMailbox::kPointerCapacity,
            "The 64-record ingress contract does not enlarge pointer ownership");

    assertTrue(controller.requestInputResync()
                    && controller.requestInputResync(),
            "Repeated recovery remains idempotent at full pointer capacity");
    std::uint32_t cancelCount = 0;
    const auto now = microscopeCommandNow();
    controller.commandMailbox().serviceUntil(
            now + MicroscopeCommandDuration(1'000'000'000), 64,
            [&](const MicroscopeWorldCommand& command) {
                if (command.kind == MicroscopeWorldCommandKind::TouchCancel) {
                    ++cancelCount;
                }
                return MicroscopeCommandApplyResult::applied();
            },
            [now] {
                return now;
            });
    assertTrue(cancelCount == MicroscopeCommandMailbox::kPointerCapacity
                    && controller.activePointerGenerationCount() == 0,
            "Cancel-all emits exactly one reserved terminal command per pointer");
}

void testRejectedCameraCommandStillReturnsCurrentRevisions() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);

    MicroscopeInputBatch batch;
    batch.sequence = 7;
    batch.eventTimeNanos = 70;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 1;
    batch.commands[0].kind = MicroscopeInputCommandKind::Pan;
    batch.commands[0].values = {1.0F, 2.0F, 0.0F, 0.0F};

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 7, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(batch, nullptr, result);
    const auto motion = controller.presentationMotionSnapshot();
    assertTrue(result.commandStatuses[0]
                            == MicroscopeInputCommandWireStatus::PresentationRejectedInteractionGate
                    && result.aggregateFlags == 0
                    && result.viewportRevision == motion.cameraRevision
                    && result.presentationMotionRevision == motion.revision,
            "A rejected camera command still returns coherent current revisions");
}

void testWorldCoordinatesPrecedeLaterCameraMutationAndResyncDrainsPointers() {
    using namespace cellularity::microscope;
    NativeInteractionController controller(1.0F, 1);
    controller.setViewportSurfaceSize(640, 360);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);
    const double expectedWorldX = controller.viewportScreenToWorldX(100.0F);

    MicroscopeInputBatch batch;
    batch.sequence = 2;
    batch.eventTimeNanos = 20;
    batch.expectedInputEpoch = 7;
    batch.commandCount = 2;
    batch.commands[0].kind = MicroscopeInputCommandKind::Down;
    batch.commands[0].pointerId = 4;
    batch.commands[0].values = {100.0F, 90.0F, 0.0F, 0.0F};
    batch.commands[1].kind = MicroscopeInputCommandKind::Pan;
    batch.commands[1].values = {80.0F, 0.0F, 0.0F, 0.0F};

    MicroscopeInputBatchResult result;
    initializeMicroscopeInputBatchResult(
            result, 2, 7, MicroscopeInputIngressStatus::Malformed);
    controller.submitInputBatch(batch, nullptr, result);
    assertTrue(controller.activePointerGenerationCount() == 1
                    && controller.requestInputResync()
                    && controller.activePointerGenerationCount() == 1
                    && controller.requestInputResync(),
            "Resync adds one terminal command and is idempotent while it drains");

    double observedWorldX = 0.0;
    std::uint32_t downCount = 0;
    std::uint32_t cancelCount = 0;
    const auto now = microscopeCommandNow();
    controller.commandMailbox().serviceUntil(
            now + MicroscopeCommandDuration(1'000'000'000), 16,
            [&](const MicroscopeWorldCommand& command) {
                if (command.kind == MicroscopeWorldCommandKind::TouchDown) {
                    observedWorldX = command.worldX;
                    ++downCount;
                } else if (command.kind
                        == MicroscopeWorldCommandKind::TouchCancel) {
                    ++cancelCount;
                }
                return MicroscopeCommandApplyResult::applied();
            },
            [now] {
                return now;
            });

    assertTrue(downCount == 1 && cancelCount == 1
                    && controller.activePointerGenerationCount() == 0,
            "Owner drain releases the active pointer with one reserved cancel");
    assertNear(observedWorldX, expectedWorldX, 1e-6,
            "Touch coordinates use the camera state before later Pan in the batch");
}

} // namespace

int main() {
    testExactCib1DecodeAndCapacityBoundary();
    testMalformedBatchIsRejectedBeforeDispatch();
    testProfilingTimestampIsDiagnosticMetadataOnly();
    testCir1AndCms1WritersAreCanonical();
    testPresentationBatchPublishesOnlyTheFinalViewport();
    testTwoPointerMoveIsOnePresentationTransaction();
    testSemanticRejectionPreservesPrefixAndLaterCommands();
    testStalePlacementGenomeRejectsOnlyTapRecord();
    testWorldCommandOrderMatchesBatchRecords();
    testInternalFailureStatusImageIsExactAtEveryDispatchPosition();
    testIngressCapacityDoesNotRaiseTheTwentyPointerMailboxLimit();
    testRejectedCameraCommandStillReturnsCurrentRevisions();
    testWorldCoordinatesPrecedeLaterCameraMutationAndResyncDrainsPointers();
    std::cout << "Microscope input batch tests passed\n";
    return 0;
}
