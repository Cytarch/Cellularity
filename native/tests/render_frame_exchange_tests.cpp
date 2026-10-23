#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

#include "render/frame/RenderFrameExchange.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::render::PreparedRenderFrameExchange;
using cellularity::render::RenderCaptureStage;
using cellularity::render::RenderFrameExchangeCounters;
using cellularity::render::RenderFrameSlotState;
using cellularity::render::RenderSourceCell;
using cellularity::render::RenderSourceFrameExchange;
using cellularity::tests::assertTrue;

static_assert(RenderSourceFrameExchange::kSlotCount == 3);
static_assert(PreparedRenderFrameExchange::kSlotCount == 3);
static_assert(!std::is_copy_constructible_v<RenderSourceFrameExchange>);
static_assert(!std::is_move_constructible_v<RenderSourceFrameExchange>);
static_assert(
        !std::is_copy_constructible_v<RenderSourceFrameExchange::Producer>);
static_assert(
        !std::is_copy_constructible_v<RenderSourceFrameExchange::Consumer>);
static_assert(
        !std::is_copy_constructible_v<RenderSourceFrameExchange::WriteLease>);
static_assert(std::is_nothrow_move_constructible_v<
        RenderSourceFrameExchange::WriteLease>);
static_assert(
        !std::is_copy_constructible_v<RenderSourceFrameExchange::ReadLease>);
static_assert(
        std::is_nothrow_move_constructible_v<RenderSourceFrameExchange::ReadLease>);

void populateSource(RenderSourceFrameExchange::WriteLease& write,
        std::uint64_t sequence, std::uint64_t worldEpoch,
        float payloadMarker) {
    auto& frame = write.frame();
    frame.captureSequence = sequence;
    frame.capture.worldEpoch = worldEpoch;
    frame.capture.completedUpdateId = sequence + 100;
    frame.capture.stage = RenderCaptureStage::AfterCompletedUpdate;
    frame.capture.resolvedInput.lastResolvedInputSequence = sequence + 200;
    frame.capture.resolvedInput.appliedWorldRevision = sequence + 300;
    frame.capture.timeline.captureRequestedNanos =
            static_cast<std::int64_t>(sequence + 400);
    frame.capture.timeline.sourceCapturedNanos =
            static_cast<std::int64_t>(sequence + 500);
    frame.capture.viewport.revision = sequence + 600;
    frame.capture.viewport.coverageRevision = sequence + 700;
    frame.capture.viewport.visibleBounds = {-1.0f, 1.0f, -2.0f, 2.0f};
    frame.capture.viewport.coverageBounds = {-3.0f, 3.0f, -4.0f, 4.0f};
    frame.cells.clear();
    RenderSourceCell cell;
    cell.positionX = payloadMarker;
    frame.cells.push_back(cell);
    frame.valid = true;
}

void publishSource(RenderSourceFrameExchange& exchange, std::uint64_t sequence,
        std::uint64_t worldEpoch, float payloadMarker) {
    auto write = exchange.producer().tryBeginWrite();
    assertTrue(static_cast<bool>(write),
            "Source producer obtains a writable slot");
    populateSource(write, sequence, worldEpoch, payloadMarker);
    assertTrue(write.publish(), "Complete source frame publishes");
    assertTrue(!static_cast<bool>(write),
            "Published source write lease relinquishes ownership");
}

void populatePrepared(PreparedRenderFrameExchange::WriteLease& write,
        std::uint64_t sequence, std::uint64_t worldEpoch,
        float payloadMarker) {
    auto& frame = write.frame();
    frame.identity.captureSequence = sequence;
    frame.identity.worldEpoch = worldEpoch;
    frame.identity.completedUpdateId = sequence + 100;
    frame.identity.captureStage = RenderCaptureStage::AfterCompletedUpdate;
    frame.resolvedInput.lastResolvedInputSequence = sequence + 200;
    frame.resolvedInput.appliedWorldRevision = sequence + 300;
    frame.timeline.captureRequestedNanos =
            static_cast<std::int64_t>(sequence + 400);
    frame.timeline.sourceCapturedNanos =
            static_cast<std::int64_t>(sequence + 500);
    frame.timeline.framePreparedNanos = static_cast<std::int64_t>(sequence + 600);
    frame.viewport.revision = sequence + 700;
    frame.viewport.coverageRevision = sequence + 800;
    frame.viewport.visibleBounds = {-1.0f, 1.0f, -2.0f, 2.0f};
    frame.viewport.coverageBounds = {-3.0f, 3.0f, -4.0f, 4.0f};
    frame.cellTriangleCoords.clear();
    frame.cellTriangleCoords.push_back(payloadMarker);
    frame.cellDrawVertexCount = 1;
    frame.geometryReady = true;
    frame.valid = true;
}

void publishPrepared(PreparedRenderFrameExchange& exchange,
        std::uint64_t sequence, std::uint64_t worldEpoch,
        float payloadMarker) {
    auto write = exchange.producer().tryBeginWrite();
    assertTrue(static_cast<bool>(write),
            "Prepared producer obtains a writable slot");
    populatePrepared(write, sequence, worldEpoch, payloadMarker);
    assertTrue(write.publish(), "Complete prepared frame publishes");
}

void testFixedCapacityAndExplicitEndpoints() {
    RenderSourceFrameExchange exchange;
    assertTrue(
            exchange.slotCountInState(RenderFrameSlotState::Free) == 3 && exchange.slotCountInState(RenderFrameSlotState::Writing) == 0 && exchange.slotCountInState(RenderFrameSlotState::Published) == 0 && exchange.slotCountInState(RenderFrameSlotState::Reading) == 0,
            "Exchange starts as exactly three fixed FREE slots");
    assertTrue(&exchange.producer() == &exchange.producer() && &exchange.consumer() == &exchange.consumer(),
            "Exchange exposes one stable producer and one stable consumer");

    auto write = exchange.producer().tryBeginWrite();
    assertTrue(static_cast<bool>(write) && exchange.slotCountInState(RenderFrameSlotState::Writing) == 1 && exchange.slotCountInState(RenderFrameSlotState::Free) == 2,
            "FREE transitions to WRITING without changing capacity");
    write.abandon();
    assertTrue(exchange.slotCountInState(RenderFrameSlotState::Free) == 3,
            "Abandoned WRITING transitions back to FREE");
}

void testLeaseMovesKeepExactlyOneOwner() {
    RenderSourceFrameExchange exchange;
    auto originalWrite = exchange.producer().tryBeginWrite();
    const std::size_t writeSlot = originalWrite.slotIndex();
    auto movedWrite = std::move(originalWrite);
    assertTrue(!originalWrite && movedWrite.slotIndex() == writeSlot,
            "Moving WriteLease transfers the only mutable slot owner");
    populateSource(movedWrite, 11, 2, 0.11f);
    assertTrue(movedWrite.publish(), "Moved WriteLease may publish once");

    auto originalRead = exchange.consumer().tryAcquireNewest(2);
    const std::size_t readSlot = originalRead.slotIndex();
    auto movedRead = std::move(originalRead);
    assertTrue(!originalRead && movedRead.slotIndex() == readSlot && movedRead.frame().cells[0].positionX == 0.11f,
            "Moving ReadLease transfers the only immutable slot owner");
    movedRead.release();
    assertTrue(exchange.slotCountInState(RenderFrameSlotState::Free) == 3,
            "Moved lease releases its slot exactly once");
}

void testReleaseAcquirePublicationAndFrameMetadata() {
    RenderSourceFrameExchange sourceExchange;
    publishSource(sourceExchange, 17, 5, 0.25f);
    assertTrue(sourceExchange.slotCountInState(RenderFrameSlotState::Published) == 1,
            "Source publication transitions WRITING to PUBLISHED");

    auto sourceRead = sourceExchange.consumer().tryAcquireNewest(5);
    assertTrue(static_cast<bool>(sourceRead),
            "Compatible source publication transitions to READING");
    const auto& source = sourceRead.frame();
    assertTrue(
            sourceRead.metadata().publicationTicket == 1 && sourceRead.metadata().frameSequence == 17 && sourceRead.metadata().worldEpoch == 5 && source.valid && source.captureSequence == 17 && source.capture.worldEpoch == 5 && source.capture.completedUpdateId == 117 && source.capture.resolvedInput.lastResolvedInputSequence == 217 && source.capture.resolvedInput.appliedWorldRevision == 317 && source.capture.timeline.captureRequestedNanos == 417 && source.capture.timeline.sourceCapturedNanos == 517 && source.capture.viewport.revision == 617 && source.capture.viewport.coverageRevision == 717 && source.capture.viewport.coverageBounds.maxX == 3.0f && source.cells.size() == 1 && source.cells[0].positionX == 0.25f,
            "Source slot publishes identity, input, timestamp, coverage, and "
            "payload");
    const RenderFrameExchangeCounters sourceCounters = sourceExchange.counters();
    assertTrue(sourceCounters.publishedFrameCount == 1 && sourceCounters.acquiredFrameCount == 1 && sourceCounters.droppedFrameCount() == 0,
            "Source exchange reports publication without false drops");
    sourceRead.release();
    assertTrue(sourceExchange.slotCountInState(RenderFrameSlotState::Free) == 3,
            "Source READING transitions back to FREE");

    PreparedRenderFrameExchange preparedExchange;
    publishPrepared(preparedExchange, 23, 9, 0.75f);
    auto preparedRead = preparedExchange.consumer().tryAcquireNewest(9);
    assertTrue(static_cast<bool>(preparedRead),
            "Compatible prepared publication is acquired");
    const auto& prepared = preparedRead.frame();
    assertTrue(preparedRead.metadata().frameSequence == 23 && preparedRead.metadata().worldEpoch == 9 && prepared.valid && prepared.geometryReady && prepared.identity.captureSequence == 23 && prepared.identity.worldEpoch == 9 && prepared.resolvedInput.lastResolvedInputSequence == 223 && prepared.resolvedInput.appliedWorldRevision == 323 && prepared.timeline.captureRequestedNanos == 423 && prepared.timeline.sourceCapturedNanos == 523 && prepared.timeline.framePreparedNanos == 623 && prepared.viewport.revision == 723 && prepared.viewport.coverageRevision == 823 && prepared.viewport.coverageBounds.minX == -3.0f && prepared.cellTriangleCoords.size() == 1 && prepared.cellTriangleCoords[0] == 0.75f,
            "Prepared slot publishes identity, input, timestamp, coverage, "
            "and payload");
}

void testPartialFrameIsNeverPublished() {
    PreparedRenderFrameExchange exchange;
    {
        auto write = exchange.producer().tryBeginWrite();
        assertTrue(static_cast<bool>(write), "Partial-frame test obtains WRITING");
        auto& frame = write.frame();
        frame.identity.captureSequence = 31;
        frame.identity.worldEpoch = 4;
        frame.valid = true;
        frame.geometryReady = false;
        assertTrue(!write.publish(),
                "Prepared publication rejects missing geometry completion");
        assertTrue(exchange.slotCountInState(RenderFrameSlotState::Writing) == 1 && !exchange.consumer().tryAcquireNewest(4),
                "Rejected partial frame remains invisible in WRITING");
    }

    const RenderFrameExchangeCounters counters = exchange.counters();
    assertTrue(exchange.slotCountInState(RenderFrameSlotState::Free) == 3 && counters.publishedFrameCount == 0 && counters.rejectedPublicationCount == 1 && counters.abandonedWriteCount == 1,
            "Partial WRITING returns to FREE without becoming a drop");
}

void testConsumerSelectsNewestSequenceWithoutFifoReplay() {
    RenderSourceFrameExchange exchange;
    publishSource(exchange, 50, 8, 0.50f);
    publishSource(exchange, 70, 8, 0.70f);
    publishSource(exchange, 60, 8, 0.60f);

    auto read = exchange.consumer().tryAcquireNewest(8);
    assertTrue(
            static_cast<bool>(read) && read.metadata().frameSequence == 70 && read.metadata().publicationTicket == 2 && read.frame().cells[0].positionX == 0.70f,
            "Consumer chooses newest frame sequence, not FIFO or last slot scan");
    assertTrue(
            exchange.slotCountInState(RenderFrameSlotState::Reading) == 1 && exchange.slotCountInState(RenderFrameSlotState::Free) == 2 && exchange.slotCountInState(RenderFrameSlotState::Published) == 0,
            "Selecting newest immediately frees older unread compatible slots");
    const RenderFrameExchangeCounters counters = exchange.counters();
    assertTrue(counters.consumerDiscardedPublishedFrameCount == 2 && counters.droppedFrameCount() == 2,
            "Skipped unread compatible frames are counted as drops");
}

void testEqualSequenceUsesNewestPublicationTicket() {
    RenderSourceFrameExchange exchange;
    publishSource(exchange, 80, 10, 0.10f);
    publishSource(exchange, 80, 10, 0.20f);
    publishSource(exchange, 80, 10, 0.30f);

    auto read = exchange.consumer().tryAcquireNewest(10);
    assertTrue(static_cast<bool>(read) && read.metadata().publicationTicket == 3 && read.frame().cells[0].positionX == 0.30f,
            "Equal frame sequences resolve to newest publication ticket");
    assertTrue(exchange.counters().consumerDiscardedPublishedFrameCount == 2,
            "Equal-sequence older publications are released as stale");
}

void testProducerReplacesOldestUnreadPublication() {
    RenderSourceFrameExchange exchange;
    publishSource(exchange, 10, 6, 0.10f);
    publishSource(exchange, 50, 7, 0.50f);
    publishSource(exchange, 30, 7, 0.30f);

    auto write = exchange.producer().tryBeginWrite();
    assertTrue(static_cast<bool>(write),
            "Full producer obtains an unread PUBLISHED replacement slot");
    populateSource(write, 70, 7, 0.70f);
    assertTrue(write.publish(), "Replacement source frame publishes");
    assertTrue(!exchange.consumer().tryAcquireNewest(6),
            "Producer reclaimed the oldest publication ticket across epochs");

    auto read = exchange.consumer().tryAcquireNewest(7);
    assertTrue(static_cast<bool>(read) && read.metadata().frameSequence == 70 && read.frame().cells[0].positionX == 0.70f,
            "Consumer observes replacement as newest state");
    const RenderFrameExchangeCounters counters = exchange.counters();
    assertTrue(counters.producerReplacedPublishedFrameCount == 1 && counters.consumerDiscardedPublishedFrameCount == 2 && counters.incomingDroppedFrameCount == 0 && counters.droppedFrameCount() == 3,
            "Producer replacement and consumer skips have distinct counters");
}

void testReadingSlotIsNeverOverwritten() {
    RenderSourceFrameExchange exchange;
    publishSource(exchange, 1, 12, 0.10f);
    auto protectedRead = exchange.consumer().tryAcquireNewest(12);
    assertTrue(static_cast<bool>(protectedRead),
            "Protected-frame test acquires first publication");

    publishSource(exchange, 2, 12, 0.20f);
    publishSource(exchange, 3, 12, 0.30f);
    publishSource(exchange, 4, 12, 0.40f);

    assertTrue(protectedRead.metadata().frameSequence == 1 && protectedRead.frame().captureSequence == 1 && protectedRead.frame().cells[0].positionX == 0.10f,
            "Producer replacement never mutates a READING payload");
    auto newest = exchange.consumer().tryAcquireNewest(12);
    assertTrue(static_cast<bool>(newest) && newest.metadata().frameSequence == 4 && newest.frame().cells[0].positionX == 0.40f,
            "Newest unread frame remains available beside protected READING");
    assertTrue(exchange.counters().producerReplacedPublishedFrameCount == 1,
            "Only an unread PUBLISHED slot was replaced");
}

void testProducerDropsImmediatelyWhenEverySlotIsReading() {
    RenderSourceFrameExchange exchange;
    publishSource(exchange, 1, 15, 0.10f);
    auto first = exchange.consumer().tryAcquireNewest(15);
    publishSource(exchange, 2, 15, 0.20f);
    auto second = exchange.consumer().tryAcquireNewest(15);
    publishSource(exchange, 3, 15, 0.30f);
    auto third = exchange.consumer().tryAcquireNewest(15);
    assertTrue(first && second && third && exchange.slotCountInState(RenderFrameSlotState::Reading) == 3,
            "One consumer may retain all three immutable READING leases");

    auto unavailable = exchange.producer().tryBeginWrite();
    assertTrue(!unavailable && exchange.counters().incomingDroppedFrameCount == 1,
            "Producer drops incoming work instead of waiting for GL consumer");
    assertTrue(first.frame().cells[0].positionX == 0.10f && second.frame().cells[0].positionX == 0.20f && third.frame().cells[0].positionX == 0.30f,
            "A failed producer claim leaves every READING payload unchanged");
}

void testWorldEpochCompatibilityAndStaleRejection() {
    RenderSourceFrameExchange exchange;
    publishSource(exchange, 1, 21, 0.10f);
    publishSource(exchange, 100, 22, 1.00f);
    publishSource(exchange, 2, 21, 0.20f);

    assertTrue(!exchange.consumer().tryAcquireNewest(23),
            "Consumer rejects every publication from another world epoch");
    auto epoch21 = exchange.consumer().tryAcquireNewest(21);
    assertTrue(static_cast<bool>(epoch21) && epoch21.metadata().frameSequence == 2 && epoch21.frame().cells[0].positionX == 0.20f,
            "Consumer chooses newest publication within requested epoch");
    assertTrue(exchange.slotCountInState(RenderFrameSlotState::Published) == 1,
            "Discarding old compatible frames preserves incompatible epoch");

    auto epoch22 = exchange.consumer().tryAcquireNewest(22);
    assertTrue(static_cast<bool>(epoch22) && epoch22.metadata().frameSequence == 100 && epoch22.frame().cells[0].positionX == 1.00f,
            "Preserved publication is available only to its matching epoch");
    assertTrue(exchange.counters().consumerDiscardedPublishedFrameCount == 1,
            "Only the older compatible epoch publication is discarded");
}

void testSeparateSourceAndPreparedDropProfile() {
    RenderSourceFrameExchange sourceExchange;
    publishSource(sourceExchange, 1, 30, 0.10f);
    publishSource(sourceExchange, 2, 30, 0.20f);
    publishSource(sourceExchange, 3, 30, 0.30f);
    auto sourceRead = sourceExchange.consumer().tryAcquireNewest(30);
    assertTrue(static_cast<bool>(sourceRead),
            "Source profile fixture acquires newest");

    PreparedRenderFrameExchange preparedExchange;
    publishPrepared(preparedExchange, 1, 30, 0.10f);
    publishPrepared(preparedExchange, 2, 30, 0.20f);
    publishPrepared(preparedExchange, 3, 30, 0.30f);
    publishPrepared(preparedExchange, 4, 30, 0.40f);

    const auto profile =
            cellularity::render::captureRenderFrameExchangeDropProfile(
                    sourceExchange, preparedExchange);
    assertTrue(
            profile.droppedSourceFrameCount == 2 && profile.droppedPreparedFrameCount == 1,
            "Profiler snapshot keeps dropped source and prepared counts separate");
}

} // namespace

int main() {
    testFixedCapacityAndExplicitEndpoints();
    testLeaseMovesKeepExactlyOneOwner();
    testReleaseAcquirePublicationAndFrameMetadata();
    testPartialFrameIsNeverPublished();
    testConsumerSelectsNewestSequenceWithoutFifoReplay();
    testEqualSequenceUsesNewestPublicationTicket();
    testProducerReplacesOldestUnreadPublication();
    testReadingSlotIsNeverOverwritten();
    testProducerDropsImmediatelyWhenEverySlotIsReading();
    testWorldEpochCompatibilityAndStaleRejection();
    testSeparateSourceAndPreparedDropProfile();
    std::cout << "render frame exchange state-machine tests passed\n";
    return 0;
}
