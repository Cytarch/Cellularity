#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include "app/NativeCellularityController.h"
#include "app/genome_preview/GenomePreviewCaptureRequestStore.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "app/genome_preview/GenomePreviewEndpointLayouts.h"
#include "app/genome_preview/GenomePreviewInteractionQueue.h"
#include "app/genome_preview/GenomePreviewRetainedState.h"
#include "app/genome_preview/RetainedPayloadBudget.h"
#include "app/microscope/SubstratePreviewCaptureEndpoint.h"
#include "app/microscope/SubstratePreviewCaptureRequestStore.h"
#include "app/render/preview/NativePreviewCaptureLayouts.h"
#include "app/render/preview/NativePreviewCaptureRequestState.h"
#include "core/Environment.h"
#include "formats/GenomeDraftCodec.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::app::genome_preview::GenomePreviewCaptureAdmissionStatus;
using cellularity::app::genome_preview::GenomePreviewCaptureGhost;
using cellularity::app::genome_preview::GenomePreviewCaptureRequestState;
using cellularity::app::genome_preview::GenomePreviewCaptureRequestStore;
using cellularity::app::genome_preview::GenomePreviewClearGhost;
using cellularity::app::genome_preview::GenomePreviewEndpoint;
using cellularity::app::genome_preview::GenomePreviewFrameRequestFlag;
using cellularity::app::genome_preview::GenomePreviewInteractionEnqueueStatus;
using cellularity::app::genome_preview::GenomePreviewInteractionInput;
using cellularity::app::genome_preview::GenomePreviewInteractionQueue;
using cellularity::app::genome_preview::GenomePreviewMoveViewport;
using cellularity::app::genome_preview::GenomePreviewPendingState;
using cellularity::app::genome_preview::GenomePreviewRestoreViewport;
using cellularity::app::genome_preview::GenomePreviewRetainedState;
using cellularity::app::genome_preview::GenomePreviewSelectGene;
using cellularity::app::genome_preview::GenomePreviewStatePublishStatus;
using cellularity::app::genome_preview::GenomePreviewZoomViewport;
using cellularity::app::genome_preview::PreviewCaptureRequestPhase;
using cellularity::app::genome_preview::RetainedPayloadBudget;
using cellularity::app::microscope::SubstratePreviewCaptureAdmissionStatus;
using cellularity::app::microscope::SubstratePreviewCaptureEndpoint;
using cellularity::app::microscope::SubstratePreviewCaptureRequestPhase;
using cellularity::app::microscope::SubstratePreviewCaptureRequestStore;
using cellularity::tests::assertTrue;

std::vector<std::uint8_t> encodedGenome(std::size_t geneCount = 1) {
    return writeGenomeDraft(
            cellularity::tests::canonicalDefaultGenomeData(geneCount));
}

std::uint16_t readLittleEndian16(
        const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0])
            | static_cast<std::uint16_t>(bytes[1]) << 8U;
}

std::uint32_t readLittleEndian32(
        const std::uint8_t* bytes) noexcept {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t readLittleEndian64(
        const std::uint8_t* bytes) noexcept {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

GenomePreviewInteractionInput interaction(
        std::uint64_t token,
        std::uint64_t controllerGeneration,
        cellularity::app::genome_preview::GenomePreviewInteractionValue value,
        std::uint64_t attachmentGeneration = 0) {
    return {
            token,
            controllerGeneration,
            attachmentGeneration,
            std::move(value),
    };
}

void testCapacityPolicyUsesMeasuredBoundsWithoutClamping() {
    using cellularity::app::genome_preview::boundedCapacityFromObservedHighWater;
    static_assert(cellularity::kGenomeDraftMaximumWireBytes == 78'616U);
    static_assert(
            cellularity::app::genome_preview::kGenomePreviewInteractionCapacity
            == 512U);
    static_assert(
            cellularity::app::genome_preview::kGenomePreviewCaptureSlotCapacity
            == 2U);
    static_assert(
            cellularity::app::microscope::kSubstratePreviewCaptureSlotCapacity
            == 2U);

    assertTrue(boundedCapacityFromObservedHighWater(0, 128, 1024) == 128
                    && boundedCapacityFromObservedHighWater(196, 128, 1024)
                            == 512
                    && boundedCapacityFromObservedHighWater(512, 128, 1024)
                            == 1024
                    && boundedCapacityFromObservedHighWater(513, 128, 1024)
                            == 0
                    && boundedCapacityFromObservedHighWater(1, 2, 32) == 2
                    && boundedCapacityFromObservedHighWater(16, 2, 32) == 32
                    && boundedCapacityFromObservedHighWater(17, 2, 32) == 0
                    && boundedCapacityFromObservedHighWater(
                               std::numeric_limits<std::size_t>::max(),
                               128,
                               1024)
                            == 0,
            "Capacity policy rounds up exactly and rejects overflow/over-cap input");
}

void testPayloadBudgetCountsUniqueAllocationsRatherThanFences() {
    RetainedPayloadBudget budget(32);
    auto reservation = budget.tryReserve(12);
    assertTrue(reservation.has_value(),
            "A payload fitting the ledger is admitted");

    struct Allocation final {
        explicit Allocation(RetainedPayloadBudget::Reservation value)
                : reservation(std::move(value)) {
        }

        RetainedPayloadBudget::Reservation reservation;
    };

    auto allocation = std::make_shared<Allocation>(std::move(*reservation));
    std::vector<std::shared_ptr<Allocation>> fences(20, allocation);
    const auto shared = budget.snapshot();
    assertTrue(shared.retainedBytes == 12
                    && shared.activeReservations == 1,
            "Many shared fences charge one immutable allocation once");

    auto second = budget.tryReserve(20);
    auto rejected = budget.tryReserve(1);
    assertTrue(second.has_value() && !rejected.has_value()
                    && budget.snapshot().peakRetainedBytes == 32,
            "Exact byte limit is admitted and the next byte is rejected");
    second.reset();
    fences.clear();
    allocation.reset();
    assertTrue(budget.snapshot().retainedBytes == 0
                    && budget.snapshot().activeReservations == 0,
            "Last owners release every reservation");
}

void testEditorWorstCaseBudgetFormulaHasFourExtraPositions() {
    RetainedPayloadBudget budget(
            cellularity::app::genome_preview::kGenomePreviewRetainedByteBudget);
    std::vector<RetainedPayloadBudget::Reservation> reservations;
    reservations.reserve(
            cellularity::app::genome_preview::kGenomePreviewRetainedOwnershipPositions);
    for (std::size_t index = 0;
            index
            < cellularity::app::genome_preview::kGenomePreviewRetainedOwnershipPositions;
            ++index) {
        auto reservation = budget.tryReserve(
                cellularity::kGenomeDraftMaximumWireBytes);
        assertTrue(reservation.has_value(),
                "Every queue fence plus four transient owners fits the editor budget");
        reservations.push_back(std::move(*reservation));
    }
    assertTrue(!budget.tryReserve(cellularity::kGenomeDraftMaximumWireBytes)
                       .has_value(),
            "The unique snapshot after capacity plus four is rejected");
    reservations.clear();
    assertTrue(budget.snapshot().retainedBytes == 0,
            "Worst-case accounting returns to zero after owners release");
}

void testLatestStateCoalescesAndDependentCommandKeepsPriorFence() {
    GenomePreviewRetainedState state;
    GenomePreviewInteractionQueue queue(4);
    const std::vector<std::uint8_t> contentA = encodedGenome(1);
    const std::vector<std::uint8_t> contentB = encodedGenome(2);

    const auto contentAResult = state.publishContent(
            contentA.data(), contentA.size(), 1.0, 1);
    const auto viewAResult = state.publishView({2, true, false, 320, 240}, 1);
    const auto commandResult = state.enqueueInteraction(
            queue,
            interaction(1, 7, GenomePreviewCaptureGhost{}));
    const auto contentBResult = state.publishContent(
            contentB.data(), contentB.size(), 2.0, 2);
    const auto viewBResult = state.publishView({4, false, true, 640, 360}, 2);

    GenomePreviewPendingState pending;
    assertTrue(contentAResult.revision == 1
                    && viewAResult.revision == 1
                    && commandResult.accepted()
                    && contentBResult.revision == 2
                    && viewBResult.revision == 2
                    && state.consumePendingOnRenderOwner(pending)
                    && pending.content != nullptr
                    && pending.content->genomeDraftBytes == contentB
                    && pending.view != nullptr
                    && pending.view->update.selectedGeneIndex == 4
                    && pending.view->update.viewportWidth == 640
                    && pending.view->update.viewportHeight == 360,
            "Latest content/view coalesce independently to complete B snapshots");

    auto command = queue.consumePendingOnRenderOwner(7, 1);
    assertTrue(command.has_value()
                    && command->fence.content != nullptr
                    && command->fence.content->genomeDraftBytes == contentA
                    && command->fence.view != nullptr
                    && command->fence.view->update.selectedGeneIndex == 2,
            "State A -> dependent command -> state B keeps A on the command");

    const auto duplicate = state.publishContent(
            contentB.data(), contentB.size(), 2.0, 2);
    const auto conflict = state.publishContent(
            contentA.data(), contentA.size(), 2.0, 2);
    const auto stale = state.publishContent(
            contentA.data(), contentA.size(), 1.0, 1);
    assertTrue(duplicate.status == GenomePreviewStatePublishStatus::Duplicate
                    && duplicate.revision == 2
                    && conflict.status
                            == GenomePreviewStatePublishStatus::ConflictingProducerSequence
                    && stale.status
                            == GenomePreviewStatePublishStatus::StaleProducerSequence
                    && state.snapshot().contentRevision == 2,
            "Duplicate/stale/conflicting producer sequences consume no revision");

    command.reset();
    pending = {};
    state.close();
    assertTrue(state.publishView({1, false, false, 320, 240}, 3).status
                            == GenomePreviewStatePublishStatus::Closing
                    && state.enqueueInteraction(
                                    queue,
                                    interaction(
                                            2,
                                            7,
                                            GenomePreviewClearGhost{}))
                                    .status
                            == GenomePreviewInteractionEnqueueStatus::Closing,
            "Closed retained state rejects late state and interaction ingress");
}

void testFrameRequestFlagCoalescesAndConsumesNewestPublishedState() {
    GenomePreviewRetainedState state;
    GenomePreviewFrameRequestFlag frameRequest;
    const std::vector<std::uint8_t> first = encodedGenome(1);
    const std::vector<std::uint8_t> second = encodedGenome(2);
    (void)state.publishContent(first.data(), first.size(), 1.0, 1);
    assertTrue(frameRequest.request(),
            "First state publication schedules one frame wakeup");
    (void)state.publishContent(second.data(), second.size(), 2.0, 2);
    assertTrue(!frameRequest.request() && frameRequest.pending(),
            "Adjacent requests coalesce without hiding a newer state");

    GenomePreviewPendingState pending;
    assertTrue(frameRequest.consumePendingOnRenderOwner()
                    && !frameRequest.consumePendingOnRenderOwner()
                    && state.consumePendingOnRenderOwner(pending)
                    && pending.content != nullptr
                    && pending.content->genomeDraftBytes == second,
            "One render-owner wake consumes the complete newest snapshot once");
    frameRequest.close();
    assertTrue(!frameRequest.request() && !frameRequest.pending(),
            "Closed frame latch rejects late wakeups");
}

void testRetainedStateBudgetFailurePreservesPreviouslyPublishedSnapshot() {
    const std::vector<std::uint8_t> acceptedBytes = encodedGenome(1);
    const std::vector<std::uint8_t> rejectedBytes = encodedGenome(2);
    GenomePreviewRetainedState state(acceptedBytes.size());
    const auto accepted = state.publishContent(
            acceptedBytes.data(), acceptedBytes.size(), 1.0, 1);
    const auto rejected = state.publishContent(
            rejectedBytes.data(), rejectedBytes.size(), 2.0, 2);
    const auto viewOnly = state.publishView({3, true, true, 320, 240}, 1);
    const auto invalidViewport =
            state.publishView({3, true, true, 0, 240}, 2);
    GenomePreviewPendingState pending;
    assertTrue(accepted.status == GenomePreviewStatePublishStatus::Applied
                    && rejected.status
                            == GenomePreviewStatePublishStatus::BudgetExceeded
                    && viewOnly.status
                            == GenomePreviewStatePublishStatus::Applied
                    && invalidViewport.status
                            == GenomePreviewStatePublishStatus::InvalidArgument
                    && state.snapshot().contentRevision == 1
                    && state.snapshot().viewRevision == 1
                    && state.consumePendingOnRenderOwner(pending)
                    && pending.content != nullptr
                    && pending.content->genomeDraftBytes == acceptedBytes,
            "Budget rejection preserves old content while independent view state advances");
}

void testRetainedStateRevisionsNeverWrapToZero() {
    constexpr std::uint64_t lastRevision =
            std::numeric_limits<std::uint64_t>::max();
    const std::vector<std::uint8_t> first = encodedGenome(1);
    const std::vector<std::uint8_t> second = encodedGenome(2);
    GenomePreviewRetainedState state(
            first.size() + second.size(), lastRevision, lastRevision);
    const auto contentApplied = state.publishContent(
            first.data(), first.size(), 1.0, 1);
    const auto contentDuplicate = state.publishContent(
            first.data(), first.size(), 1.0, 1);
    const auto contentExhausted = state.publishContent(
            second.data(), second.size(), 2.0, 2);
    const auto viewApplied = state.publishView(
            {1, true, false, 320, 240}, 1);
    const auto viewExhausted = state.publishView(
            {2, false, true, 640, 360}, 2);
    assertTrue(contentApplied.revision == lastRevision
                    && contentDuplicate.status
                            == GenomePreviewStatePublishStatus::Duplicate
                    && contentDuplicate.revision == lastRevision
                    && contentExhausted.status
                            == GenomePreviewStatePublishStatus::RevisionExhausted
                    && viewApplied.revision == lastRevision
                    && viewExhausted.status
                            == GenomePreviewStatePublishStatus::RevisionExhausted
                    && state.snapshot().lastContentProducerSequence == 1
                    && state.snapshot().lastViewProducerSequence == 1,
            "Content and view revisions stop at UINT64_MAX without consuming state");
}

void testInteractionQueuePreservesFifoOverflowAndGenerationFences() {
    GenomePreviewInteractionQueue queue(3);
    assertTrue(queue.enqueue(
                            interaction(
                                    1,
                                    10,
                                    GenomePreviewRestoreViewport{1.0F, 2.0F, 3.0F},
                                    4),
                            {})
                            .accepted()
                    && queue.enqueue(
                                    interaction(
                                            2,
                                            10,
                                            GenomePreviewMoveViewport{4.0F, 5.0F},
                                            4),
                                    {})
                            .accepted()
                    && queue.enqueue(
                                    interaction(
                                            3,
                                            10,
                                            GenomePreviewSelectGene{6.0F, 7.0F},
                                            4),
                                    {})
                            .accepted(),
            "Typed interaction variants fill the bounded FIFO");
    assertTrue(queue.enqueue(
                            interaction(
                                    4,
                                    10,
                                    GenomePreviewZoomViewport{2.0F, 0.0F, 0.0F},
                                    4),
                            {})
                            .status
                    == GenomePreviewInteractionEnqueueStatus::QueueFull,
            "Overflow is explicit and never blocks the producer");

    const auto first = queue.consumePendingOnRenderOwner(10, 4);
    const auto second = queue.consumePendingOnRenderOwner(10, 4);
    const auto third = queue.consumePendingOnRenderOwner(10, 4);
    assertTrue(first.has_value() && second.has_value() && third.has_value()
                    && first->input.completionToken == 1
                    && second->input.completionToken == 2
                    && third->input.completionToken == 3
                    && std::holds_alternative<GenomePreviewRestoreViewport>(
                            first->input.value)
                    && std::holds_alternative<GenomePreviewMoveViewport>(
                            second->input.value)
                    && std::holds_alternative<GenomePreviewSelectGene>(
                            third->input.value),
            "Restore, move and select retain exact FIFO order without coalescing");

    assertTrue(queue.enqueue(
                            interaction(
                                    5,
                                    9,
                                    GenomePreviewCaptureGhost{},
                                    3),
                            {})
                            .accepted()
                    && queue.enqueue(
                                    interaction(
                                            6,
                                            10,
                                            GenomePreviewClearGhost{},
                                            5),
                                    {})
                            .accepted(),
            "Old and new attachment commands can coexist before consumption");
    const auto newest = queue.consumePendingOnRenderOwner(10, 5);
    assertTrue(newest.has_value() && newest->input.completionToken == 6
                    && queue.snapshot().staleGenerationDiscarded == 1,
            "Stale generation is discarded without consuming the new attachment command");
}

void testInteractionCloseReleasesSnapshotFence() {
    GenomePreviewRetainedState state;
    GenomePreviewInteractionQueue queue(2);
    const std::vector<std::uint8_t> bytes = encodedGenome();
    (void)state.publishContent(bytes.data(), bytes.size(), 1.0, 1);
    GenomePreviewPendingState pending;
    (void)state.consumePendingOnRenderOwner(pending);
    std::weak_ptr<const cellularity::app::genome_preview::GenomePreviewContentSnapshot>
            weak = pending.content;
    (void)state.enqueueInteraction(
            queue,
            interaction(1, 1, GenomePreviewCaptureGhost{}));
    state.close();
    pending = {};
    assertTrue(!weak.expired(),
            "Queued dependent command retains its immutable content fence");
    queue.close();
    assertTrue(weak.expired()
                    && queue.snapshot().cancelledOnClose == 1,
            "Close cancels queued work and releases the final fence");
}

void testConcurrentLatestPublicationNeverProducesTornContent() {
    constexpr std::uint64_t kPublicationCount = 10'000;
    const std::vector<std::uint8_t> encoded = encodedGenome();
    GenomePreviewRetainedState state(
            encoded.size()
            * cellularity::app::genome_preview::kGenomePreviewRetainedOwnershipPositions);
    std::atomic<bool> producerDone{false};
    std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> lastConsumed{0};

    std::thread producer([&] {
        for (std::uint64_t sequence = 1; sequence <= kPublicationCount;
                ++sequence) {
            for (;;) {
                const auto result = state.publishContent(
                        encoded.data(),
                        encoded.size(),
                        static_cast<double>(sequence),
                        sequence);
                if (result.status == GenomePreviewStatePublishStatus::Applied) {
                    break;
                }
                if (result.status
                        != GenomePreviewStatePublishStatus::ExchangeUnavailable) {
                    failed.store(true, std::memory_order_release);
                    producerDone.store(true, std::memory_order_release);
                    return;
                }
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        std::uint64_t previous = 0;
        while (!producerDone.load(std::memory_order_acquire)
                || previous < kPublicationCount) {
            GenomePreviewPendingState pending;
            if (!state.consumePendingOnRenderOwner(pending)
                    || pending.content == nullptr) {
                std::this_thread::yield();
                continue;
            }
            const auto& snapshot = *pending.content;
            if (snapshot.revision <= previous
                    || snapshot.genomeDraftBytes != encoded
                    || snapshot.simulationTimeHours
                            != static_cast<double>(
                                    snapshot.producerSequence)) {
                failed.store(true, std::memory_order_release);
                break;
            }
            previous = snapshot.revision;
        }
        lastConsumed.store(previous, std::memory_order_release);
    });
    producer.join();
    consumer.join();
    assertTrue(!failed.load(std::memory_order_acquire)
                    && lastConsumed.load(std::memory_order_acquire)
                            == kPublicationCount,
            "Concurrent producer/consumer observes only whole newest snapshots");
}

void testGenomeCaptureStoreBoundsCopiesAndTerminalTransitions() {
    GenomePreviewCaptureRequestStore store;
    std::vector<std::uint8_t> firstBytes = encodedGenome(1);
    const std::vector<std::uint8_t> secondBytes = encodedGenome(2);
    auto first = store.enqueue(
            1, 9, 5, firstBytes.data(), firstBytes.size(), 48.0);
    auto second = store.enqueue(
            2, 9, 5, secondBytes.data(), secondBytes.size(), 24.0);
    firstBytes[0] = 99;
    const auto overflow = store.enqueue(
            3, 9, 5, secondBytes.data(), secondBytes.size(), 12.0);
    const auto duplicate = store.enqueue(
            1, 9, 5, secondBytes.data(), secondBytes.size(), 12.0);
    assertTrue(first.accepted() && second.accepted()
                    && first.request->genomeDraftBytes[0] == 0x43U
                    && overflow.status
                            == GenomePreviewCaptureAdmissionStatus::Busy
                    && duplicate.status
                            == GenomePreviewCaptureAdmissionStatus::DuplicateRequestId
                    && store.snapshot().highWater == 2,
            "Capture store copies immutable input once and enforces slots/IDs");

    auto consumedFirst = store.consumePendingOnRenderOwner(9, 5);
    auto consumedSecond = store.consumePendingOnRenderOwner(9, 5);
    assertTrue(consumedFirst == first.request
                    && consumedSecond == second.request
                    && consumedFirst->phase()
                            == PreviewCaptureRequestPhase::Running
                    && consumedFirst->complete()
                    && !consumedFirst->fail()
                    && consumedSecond->cancel()
                    && !consumedSecond->complete(),
            "FIFO requests have one irreversible terminal transition");
    assertTrue(store.release(1) && store.release(2)
                    && store.snapshot().occupied == 0,
            "Terminal requests release their bounded slots");
    consumedFirst.reset();
    consumedSecond.reset();
    first.request.reset();
    second.request.reset();
    assertTrue(store.snapshot().inputBudget.retainedBytes == 0,
            "Genome input budget returns after the final request owners release");
}

void testGenomeCaptureBudgetAndCloseAreIndependentFromEditorBudget() {
    const std::vector<std::uint8_t> bytes = encodedGenome();
    GenomePreviewCaptureRequestStore store(2, bytes.size());
    auto accepted = store.enqueue(1, 2, 3, bytes.data(), bytes.size(), 1.0);
    const auto rejected = store.enqueue(
            2, 2, 3, bytes.data(), bytes.size(), 1.0);
    assertTrue(accepted.accepted()
                    && rejected.status
                            == GenomePreviewCaptureAdmissionStatus::BudgetExceeded,
            "Capture input quota rejects before another payload copy");

    std::atomic<PreviewCaptureRequestPhase> waited{
            PreviewCaptureRequestPhase::Pending
    };
    std::thread waiter([&] {
        waited.store(
                accepted.request->waitForTerminal(
                        std::chrono::milliseconds(1000)),
                std::memory_order_release);
    });
    store.close();
    waiter.join();
    assertTrue(waited.load(std::memory_order_acquire)
                            == PreviewCaptureRequestPhase::Cancelled
                    && store.snapshot().occupied == 0,
            "Store close cancels and wakes a pending one-shot exactly once");
    accepted.request.reset();
    assertTrue(store.snapshot().inputBudget.retainedBytes == 0,
            "Closing the capture store releases its independent input budget");
}

void testCaptureTerminalRaceHasExactlyOneWinner() {
    GenomePreviewCaptureRequestStore store;
    const std::vector<std::uint8_t> bytes = encodedGenome();
    auto admission = store.enqueue(
            1, 2, 3, bytes.data(), bytes.size(), 1.0);
    auto running = store.consumePendingOnRenderOwner(2, 3);
    std::atomic<int> winners{0};
    std::vector<std::thread> contenders;
    for (int index = 0; index < 12; ++index) {
        contenders.emplace_back([&, index] {
            const bool won = index % 3 == 0
                    ? running->complete()
                    : index % 3 == 1
                    ? running->fail()
                    : running->cancel();
            if (won) {
                winners.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& contender : contenders) {
        contender.join();
    }
    assertTrue(winners.load(std::memory_order_relaxed) == 1
                    && isTerminal(running->phase())
                    && store.release(1),
            "Concurrent complete/fail/cancel has one irreversible winner");
}

void testGenomeCaptureSkipsStaleControllerWithoutLosingNewRequest() {
    GenomePreviewCaptureRequestStore store;
    const std::vector<std::uint8_t> bytes = encodedGenome();
    auto stale = store.enqueue(1, 3, 5, bytes.data(), bytes.size(), 1.0);
    auto current = store.enqueue(2, 4, 6, bytes.data(), bytes.size(), 1.0);
    auto consumed = store.consumePendingOnRenderOwner(4, 6);
    assertTrue(stale.request->phase() == PreviewCaptureRequestPhase::Cancelled
                    && consumed == current.request
                    && consumed->phase() == PreviewCaptureRequestPhase::Running,
            "Stale controller capture is cancelled before current work is consumed");
}

void testGenomeSurfaceLossCancelsOnlyMatchingAttachment() {
    GenomePreviewCaptureRequestStore store;
    const std::vector<std::uint8_t> bytes = encodedGenome();
    auto oldSurface = store.enqueue(
            1, 7, 10, bytes.data(), bytes.size(), 1.0);
    auto newSurface = store.enqueue(
            2, 7, 11, bytes.data(), bytes.size(), 1.0);
    assertTrue(store.cancelAttachment(10) == 1
                    && oldSurface.request->phase()
                            == PreviewCaptureRequestPhase::Cancelled
                    && newSurface.request->phase()
                            == PreviewCaptureRequestPhase::Pending
                    && store.consumePendingOnRenderOwner(7, 11)
                            == newSurface.request,
            "Surface loss cancels only requests admitted for that attachment");
}

void testSubstrateCaptureStoreIsBoundedOrderedAndAttachmentAware() {
    SubstratePreviewCaptureRequestStore store;
    auto stale = store.enqueue(1, 101, 7, 4);
    auto current = store.enqueue(2, 102, 7, 5);
    const auto overflow = store.enqueue(3, 103, 7, 5);
    const auto duplicate = store.enqueue(1, 104, 7, 5);
    assertTrue(stale.accepted() && current.accepted()
                    && overflow.status
                            == SubstratePreviewCaptureAdmissionStatus::Busy
                    && duplicate.status
                            == SubstratePreviewCaptureAdmissionStatus::DuplicateRequestId,
            "Substrate token store applies its own two-slot bound");

    auto consumed = store.consumePendingOnRenderOwner(7, 5);
    assertTrue(stale.request->phase()
                            == SubstratePreviewCaptureRequestPhase::Cancelled
                    && consumed == current.request
                    && consumed->substratePreviewToken == 102
                    && consumed->complete()
                    && !consumed->cancel()
                    && store.release(2),
            "Surface generation cancellation does not consume current token work");
}

void testSubstrateCloseCancelsAndWakesExactlyOnce() {
    SubstratePreviewCaptureRequestStore store;
    auto admission = store.enqueue(1, 11, 2, 3);
    std::atomic<SubstratePreviewCaptureRequestPhase> waited{
            SubstratePreviewCaptureRequestPhase::Pending
    };
    std::thread waiter([&] {
        waited.store(
                admission.request->waitForTerminal(
                        std::chrono::milliseconds(1000)),
                std::memory_order_release);
    });
    store.close();
    waiter.join();
    assertTrue(waited.load(std::memory_order_acquire)
                            == SubstratePreviewCaptureRequestPhase::Cancelled
                    && !admission.request->fail()
                    && store.snapshot().cancelled == 1,
            "Substrate close wakes pending capture with one terminal result");
}

void testSubstrateSurfaceLossCancelsOnlyMatchingAttachment() {
    SubstratePreviewCaptureRequestStore store;
    auto oldSurface = store.enqueue(1, 21, 7, 10);
    auto newSurface = store.enqueue(2, 22, 7, 11);
    assertTrue(store.cancelAttachment(10) == 1
                    && oldSurface.request->phase()
                            == SubstratePreviewCaptureRequestPhase::Cancelled
                    && newSurface.request->phase()
                            == SubstratePreviewCaptureRequestPhase::Pending
                    && store.consumePendingOnRenderOwner(7, 11)
                            == newSurface.request,
            "Substrate surface loss leaves the new attachment request intact");
}

void testEndpointReplacesManagedGateWithoutResimulatingViewOrSurfaceState() {
    Environment environment = Environment::setupPreviewSimulationEnvironment(32);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(200, 100);
    const std::shared_ptr<GenomePreviewEndpoint> endpoint =
            controller.genomePreviewEndpoint();
    const std::vector<std::uint8_t> contentA = encodedGenome(1);
    const std::vector<std::uint8_t> contentB = encodedGenome(2);

    const auto firstContent = endpoint->publishContent(
            contentA.data(), contentA.size(), 0.0, 1, endpoint->generation());
    const auto firstView = endpoint->publishViewState(
            {0, true, false, 320, 240}, 1, endpoint->generation());
    const auto restore = endpoint->enqueueInteraction(
            interaction(
                    1,
                    endpoint->generation(),
                    GenomePreviewRestoreViewport{0.25F, -0.5F, 6.0F}),
            endpoint->generation());
    const auto newestContent = endpoint->publishContent(
            contentB.data(), contentB.size(), 0.0, 2, endpoint->generation());
    const auto newestView = endpoint->publishViewState(
            {1, false, true, 640, 360}, 2, endpoint->generation());
    const std::uint64_t firstAttachment = endpoint->surfaceCreated();

    endpoint->applyPendingOnRenderOwner(controller);
    const auto acknowledgment = endpoint->readAcknowledgment();
    const std::uint64_t updateCountAfterContent =
            controller.readWorld().completedUpdateCount();
    assertTrue(firstContent.accepted()
                    && firstView.accepted()
                    && restore.accepted()
                    && newestContent.revision == 2
                    && newestView.revision == 2
                    && firstAttachment != 0
                    && acknowledgment.appliedContentRevision == 2
                    && acknowledgment.appliedViewRevision == 2
                    && acknowledgment.cellCountContentRevision == 2
                    && acknowledgment.previewCellCount == 1
                    && acknowledgment.viewport[0] == 0.25F
                    && acknowledgment.viewport[1] == -0.5F
                    && acknowledgment.viewport[2] == 6.0F,
            "Cold-start publications coalesce and apply once after attachment");

    const auto viewOnly = endpoint->publishViewState(
            {0, true, false, 320, 240}, 3, endpoint->generation());
    endpoint->applyPendingOnRenderOwner(controller);
    const auto afterView = endpoint->readAcknowledgment();
    const std::uint64_t updateCountAfterView =
            controller.readWorld().completedUpdateCount();
    assertTrue(viewOnly.accepted()
                    && afterView.appliedContentRevision == 2
                    && afterView.appliedViewRevision == 3
                    && updateCountAfterView == updateCountAfterContent,
            "Selected gene and options advance view state without resimulation");

    endpoint->surfaceDestroyed(firstAttachment);
    const std::uint64_t recreatedAttachment = endpoint->surfaceCreated();
    endpoint->applyPendingOnRenderOwner(controller);
    const auto afterRecreation = endpoint->readAcknowledgment();
    assertTrue(recreatedAttachment != 0
                    && recreatedAttachment != firstAttachment
                    && afterRecreation.attachmentGeneration
                            == recreatedAttachment
                    && afterRecreation.appliedContentRevision == 2
                    && afterRecreation.appliedViewRevision == 3
                    && controller.readWorld().completedUpdateCount()
                            == updateCountAfterContent,
            "Surface recreation retains native content and view without replay");

    endpoint->close();
    assertTrue(endpoint->publishContent(
                               contentA.data(),
                               contentA.size(),
                               0.0,
                               3,
                               endpoint->generation())
                                    .status
                            == GenomePreviewStatePublishStatus::Closing
                    && endpoint->publishViewState(
                                       {0, false, false, 320, 240},
                                       4,
                                       endpoint->generation())
                                    .status
                            == GenomePreviewStatePublishStatus::Closing,
            "Closed native endpoint rejects late gate-equivalent updates");
}

void testEndpointInteractionsApplyInFifoAgainstTheirStateFence() {
    Environment environment = Environment::setupPreviewSimulationEnvironment(32);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(200, 100);
    const std::shared_ptr<GenomePreviewEndpoint> endpoint =
            controller.genomePreviewEndpoint();
    const std::vector<std::uint8_t> content = encodedGenome();
    const auto publishedContent = endpoint->publishContent(
            content.data(), content.size(), 0.0, 1, endpoint->generation());
    const auto publishedView = endpoint->publishViewState(
            {0, true, false, 320, 240}, 1, endpoint->generation());
    const std::uint64_t attachment = endpoint->surfaceCreated();

    const auto restore = endpoint->enqueueInteraction(
            interaction(1, endpoint->generation(),
                    GenomePreviewRestoreViewport{0.0F, 0.0F, 10.0F},
                    attachment),
            endpoint->generation());
    const auto move = endpoint->enqueueInteraction(
            interaction(2, endpoint->generation(),
                    GenomePreviewMoveViewport{10.0F, -5.0F}, attachment),
            endpoint->generation());
    const auto zoom = endpoint->enqueueInteraction(
            interaction(3, endpoint->generation(),
                    GenomePreviewZoomViewport{2.0F, 160.0F, 120.0F},
                    attachment),
            endpoint->generation());
    const auto select = endpoint->enqueueInteraction(
            interaction(4, endpoint->generation(),
                    GenomePreviewSelectGene{180.0F, 110.0F}, attachment),
            endpoint->generation());

    endpoint->applyPendingOnRenderOwner(controller);
    const auto acknowledgment = endpoint->readAcknowledgment();
    assertTrue(publishedContent.accepted()
                    && publishedView.accepted()
                    && restore.accepted()
                    && move.accepted()
                    && zoom.accepted()
                    && select.accepted()
                    && acknowledgment.selectionToken == 4
                    && acknowledgment.selectedGeneIndex == 0
                    && std::abs(acknowledgment.viewport[0] + 1.0F / 120.0F)
                            < 0.00001F
                    && std::abs(acknowledgment.viewport[1] + 1.0F / 240.0F)
                            < 0.00001F
                    && std::abs(acknowledgment.viewport[2] - 20.0F)
                            < 0.00001F,
            "Restore, move, zoom and selection use one ordered native viewport");
}

void testReplacingSurfaceCancelsOnlyOldAttachmentCaptures() {
    Environment environment = Environment::setupPreviewSimulationEnvironment(32);
    NativeCellularityController controller(true, environment);
    const std::shared_ptr<GenomePreviewEndpoint> genome =
            controller.genomePreviewEndpoint();
    const std::vector<std::uint8_t> bytes = encodedGenome();
    const std::uint64_t firstGenomeAttachment = genome->surfaceCreated();
    auto oldGenome = genome->enqueueFileCapture(1, bytes.data(), bytes.size(),
            1.0, genome->generation(), firstGenomeAttachment);
    const std::uint64_t secondGenomeAttachment = genome->surfaceCreated();
    auto currentGenome = genome->enqueueFileCapture(2, bytes.data(),
            bytes.size(), 1.0, genome->generation(), secondGenomeAttachment);

    SubstratePreviewCaptureEndpoint substrate(17);
    const std::uint64_t firstSubstrateAttachment = substrate.surfaceCreated();
    auto oldSubstrate =
            substrate.enqueue(1, 101, firstSubstrateAttachment);
    const std::uint64_t secondSubstrateAttachment =
            substrate.surfaceCreated();
    auto currentSubstrate =
            substrate.enqueue(2, 102, secondSubstrateAttachment);

    assertTrue(oldGenome.accepted()
                    && currentGenome.accepted()
                    && firstGenomeAttachment != secondGenomeAttachment
                    && oldGenome.request->phase()
                            == PreviewCaptureRequestPhase::Cancelled
                    && currentGenome.request->phase()
                            == PreviewCaptureRequestPhase::Pending
                    && oldSubstrate.accepted()
                    && currentSubstrate.accepted()
                    && firstSubstrateAttachment
                            != secondSubstrateAttachment
                    && oldSubstrate.request->phase()
                            == SubstratePreviewCaptureRequestPhase::Cancelled
                    && currentSubstrate.request->phase()
                            == SubstratePreviewCaptureRequestPhase::Pending,
            "A replacement surface cancels only work owned by the old attachment");
}

void testEndpointCloseWakesBlockedCaptureAwait() {
    Environment environment = Environment::setupPreviewSimulationEnvironment(32);
    NativeCellularityController controller(true, environment);
    const std::shared_ptr<GenomePreviewEndpoint> endpoint =
            controller.genomePreviewEndpoint();
    const std::vector<std::uint8_t> bytes = encodedGenome();
    const std::uint64_t attachment = endpoint->surfaceCreated();
    auto admission = endpoint->enqueueFileCapture(1, bytes.data(), bytes.size(),
            1.0, endpoint->generation(), attachment);
    assertTrue(admission.accepted() && admission.request != nullptr,
            "Capture admission succeeds before testing endpoint-close wakeup");
    std::atomic<cellularity::app::preview::PreviewCaptureStatus> status{
            cellularity::app::preview::PreviewCaptureStatus::InternalFailure
    };
    std::thread waiter([&] {
        auto& request = static_cast<
                cellularity::app::preview::PreviewCaptureRequestState&>(
                *admission.request);
        status.store(request.await(std::chrono::milliseconds(1000)).header.status,
                std::memory_order_release);
    });

    endpoint->close();
    waiter.join();

    assertTrue(status.load(std::memory_order_acquire)
                    == cellularity::app::preview::
                            PreviewCaptureStatus::Cancelled,
            "Endpoint close wakes a blocked capture await with Cancelled");
}

void testCompletedPayloadReleasesExactOutputReservationAfterDelivery() {
    using namespace cellularity::app::preview;
    SubstratePreviewCaptureRequestStore store(2, 16);
    auto admission = store.enqueue(1, 11, 2, 3);
    auto request = store.consumePendingOnRenderOwner(2, 3);
    assertTrue(admission.accepted() && request != nullptr,
            "Capture admission succeeds before exact output completion");
    auto reservation = store.tryReserveOutput(16);
    assertTrue(reservation.has_value(),
            "Capture reserves exact output storage before completion");
    PreviewCaptureResultHeader header;
    header.requestId = 1;
    header.status = PreviewCaptureStatus::Completed;
    header.width = 2;
    header.height = 2;
    header.rowStrideBytes = 8;
    header.pixelFormat = PreviewCapturePixelFormat::Rgba8;
    header.origin = PreviewCaptureOrigin::TopLeft;
    header.alphaMode = PreviewCaptureAlphaMode::Opaque;
    header.payloadBytes = 16;
    auto& common = static_cast<PreviewCaptureRequestState&>(*request);

    assertTrue(common.complete(
                       header, std::vector<std::uint8_t>(16),
                       std::move(*reservation))
                    && store.release(1)
                    && store.snapshot().outputBudget.retainedBytes == 16,
            "Completed payload owns its exact output reservation after store release");
    auto ticket = common.await(std::chrono::milliseconds(10));
    assertTrue(ticket.header.status == PreviewCaptureStatus::Completed
                    && ticket.payload != nullptr
                    && common.consumeDelivery(ticket.deliveryToken),
            "Completed payload is delivered exactly once");
    ticket.payload.reset();
    assertTrue(store.snapshot().outputBudget.retainedBytes == 0,
            "Moving the payload to its consumer releases the native output budget");
}

void testInvalidCaptureMetadataFailsWithoutRetainingOutput() {
    using namespace cellularity::app::preview;
    SubstratePreviewCaptureRequestStore store(2, 16);
    auto admission = store.enqueue(1, 11, 2, 3);
    auto request = store.consumePendingOnRenderOwner(2, 3);
    assertTrue(admission.accepted() && request != nullptr,
            "Capture admission succeeds before malformed-metadata completion");
    auto reservation = store.tryReserveOutput(16);
    assertTrue(reservation.has_value(),
            "Malformed capture reserves bounded output storage before validation");
    PreviewCaptureResultHeader invalidHeader;
    invalidHeader.requestId = 1;
    invalidHeader.status = PreviewCaptureStatus::Completed;
    invalidHeader.width = 2;
    invalidHeader.height = 2;
    invalidHeader.rowStrideBytes = 4;
    invalidHeader.pixelFormat = PreviewCapturePixelFormat::Rgba8;
    invalidHeader.origin = PreviewCaptureOrigin::TopLeft;
    invalidHeader.alphaMode = PreviewCaptureAlphaMode::Opaque;
    invalidHeader.payloadBytes = 16;
    auto& common = static_cast<PreviewCaptureRequestState&>(*request);

    const bool completed = common.complete(invalidHeader,
            std::vector<std::uint8_t>(16), std::move(*reservation));
    assertTrue(completed,
            "Malformed capture completion wins the terminal transition");
    assertTrue(request->phase()
                    == SubstratePreviewCaptureRequestPhase::Failed,
            "Invalid capture stride fails closed");
    assertTrue(store.snapshot().outputBudget.retainedBytes == 0,
            "Invalid capture stride releases its exact output reservation");
    const auto failed = common.await(std::chrono::milliseconds(10));
    assertTrue(failed.header.status == PreviewCaptureStatus::Failed
                    && failed.payload == nullptr
                    && common.consumeDelivery(failed.deliveryToken)
                    && store.release(1),
            "Invalid capture metadata delivers one payload-free typed failure");
}

void testCaptureLayoutsAndExactlyOnceAwaitContract() {
    using namespace cellularity::app::preview;
    const auto admission = encodePreviewCaptureAdmission(
            {PreviewCaptureAdmissionStatus::Accepted, 0x0102030405060708ULL});
    assertTrue(readLittleEndian32(admission.data())
                            == kPreviewCaptureAdmissionMagic
                    && readLittleEndian16(admission.data() + 4)
                            == kPreviewCaptureAdmissionVersion
                    && readLittleEndian16(admission.data() + 6)
                            == kPreviewCaptureAdmissionSize
                    && readLittleEndian32(admission.data() + 8) == 0
                    && readLittleEndian32(admission.data() + 12) == 0
                    && readLittleEndian64(admission.data() + 16)
                            == 0x0102030405060708ULL,
            "PCA1 golden layout keeps every fixed offset and reserved byte");

    PreviewCaptureResultHeader completedHeader;
    completedHeader.requestId = 9;
    completedHeader.status = PreviewCaptureStatus::Completed;
    completedHeader.width = 2;
    completedHeader.height = 3;
    completedHeader.rowStrideBytes = 8;
    completedHeader.pixelFormat = PreviewCapturePixelFormat::Rgba8;
    completedHeader.origin = PreviewCaptureOrigin::TopLeft;
    completedHeader.alphaMode = PreviewCaptureAlphaMode::Straight;
    completedHeader.payloadBytes = 24;
    const auto result = encodePreviewCaptureResultHeader(completedHeader);
    assertTrue(readLittleEndian32(result.data()) == kPreviewCaptureResultMagic
                    && readLittleEndian16(result.data() + 6)
                            == kPreviewCaptureResultSize
                    && readLittleEndian64(result.data() + 8) == 9
                    && readLittleEndian32(result.data() + 16) == 0
                    && readLittleEndian32(result.data() + 20) == 2
                    && readLittleEndian32(result.data() + 24) == 3
                    && readLittleEndian32(result.data() + 28) == 8
                    && readLittleEndian32(result.data() + 44) == 0
                    && readLittleEndian64(result.data() + 48) == 24,
            "PCR1 golden layout keeps dimensions, format and payload size fixed");

    SubstratePreviewCaptureRequestStore store;
    auto admissionState = store.enqueue(9, 11, 2, 3);
    auto request = admissionState.request;
    auto& common = static_cast<PreviewCaptureRequestState&>(*request);
    const auto timeout = common.await(std::chrono::milliseconds(1));
    assertTrue(timeout.header.requestId == 9
                    && timeout.header.status == PreviewCaptureStatus::TimedOut
                    && timeout.deliveryToken == 0
                    && request->phase()
                            == SubstratePreviewCaptureRequestPhase::Pending,
            "A timed wait is non-terminal and does not consume the request");
    assertTrue(request->cancel(),
            "Coordinator-style explicit cancellation wins after timeout");
    const auto cancelled = common.await(std::chrono::milliseconds(10));
    assertTrue(cancelled.header.status == PreviewCaptureStatus::Cancelled
                    && cancelled.deliveryToken != 0
                    && common.consumeDelivery(cancelled.deliveryToken),
            "Cancelled terminal result is delivered and consumed exactly once");
    const auto consumed = common.await(std::chrono::milliseconds(10));
    assertTrue(consumed.header.status
                            == PreviewCaptureStatus::AlreadyConsumed
                    && consumed.deliveryToken == 0,
            "A second await reports AlreadyConsumed without another payload");
}

void testEndpointAndCaptureGoldenLayouts() {
    using namespace cellularity::app::genome_preview;
    const auto publish = encodeGenomePreviewPublishResult(
            {GenomePreviewStatePublishStatus::Applied, 7});
    GenomePreviewAcknowledgment acknowledgment;
    acknowledgment.endpointGeneration = 1;
    acknowledgment.attachmentGeneration = 2;
    acknowledgment.appliedContentRevision = 3;
    acknowledgment.appliedViewRevision = 4;
    acknowledgment.presentedContentRevision = 5;
    acknowledgment.presentedViewRevision = 6;
    acknowledgment.presentedInteractionToken = 7;
    acknowledgment.cellCountContentRevision = 8;
    acknowledgment.viewportRevision = 9;
    acknowledgment.selectionToken = 10;
    acknowledgment.previewCellCount = 11;
    acknowledgment.selectedGeneIndex = -1;
    acknowledgment.viewport = {1.0F, 2.0F, 3.0F};
    const auto frame = encodeGenomePreviewAcknowledgment(acknowledgment);
    assertTrue(readLittleEndian32(publish.data())
                            == kGenomePreviewPublishResultMagic
                    && readLittleEndian16(publish.data() + 6)
                            == kGenomePreviewPublishResultSize
                    && readLittleEndian64(publish.data() + 16) == 7
                    && readLittleEndian32(frame.data())
                            == kGenomePreviewAcknowledgmentMagic
                    && readLittleEndian16(frame.data() + 6)
                            == kGenomePreviewAcknowledgmentSize
                    && readLittleEndian64(frame.data() + 8) == 1
                    && readLittleEndian64(frame.data() + 56) == 7
                    && readLittleEndian64(frame.data() + 80) == 10
                    && readLittleEndian32(frame.data() + 88) == 11
                    && readLittleEndian32(frame.data() + 92)
                            == std::numeric_limits<std::uint32_t>::max()
                    && readLittleEndian32(frame.data() + 108) == 0,
            "GPS1/GFA2 golden layouts keep native-managed ABI offsets exact");
}

} // namespace

int main() {
    try {
        testCapacityPolicyUsesMeasuredBoundsWithoutClamping();
        testPayloadBudgetCountsUniqueAllocationsRatherThanFences();
        testEditorWorstCaseBudgetFormulaHasFourExtraPositions();
        testLatestStateCoalescesAndDependentCommandKeepsPriorFence();
        testFrameRequestFlagCoalescesAndConsumesNewestPublishedState();
        testRetainedStateBudgetFailurePreservesPreviouslyPublishedSnapshot();
        testRetainedStateRevisionsNeverWrapToZero();
        testInteractionQueuePreservesFifoOverflowAndGenerationFences();
        testInteractionCloseReleasesSnapshotFence();
        testConcurrentLatestPublicationNeverProducesTornContent();
        testGenomeCaptureStoreBoundsCopiesAndTerminalTransitions();
        testGenomeCaptureBudgetAndCloseAreIndependentFromEditorBudget();
        testCaptureTerminalRaceHasExactlyOneWinner();
        testGenomeCaptureSkipsStaleControllerWithoutLosingNewRequest();
        testGenomeSurfaceLossCancelsOnlyMatchingAttachment();
        testSubstrateCaptureStoreIsBoundedOrderedAndAttachmentAware();
        testSubstrateCloseCancelsAndWakesExactlyOnce();
        testSubstrateSurfaceLossCancelsOnlyMatchingAttachment();
        testEndpointReplacesManagedGateWithoutResimulatingViewOrSurfaceState();
        testEndpointInteractionsApplyInFifoAgainstTheirStateFence();
        testReplacingSurfaceCancelsOnlyOldAttachmentCaptures();
        testEndpointCloseWakesBlockedCaptureAwait();
        testCompletedPayloadReleasesExactOutputReservationAfterDelivery();
        testInvalidCaptureMetadataFailsWithoutRetainingOutput();
        testCaptureLayoutsAndExactlyOnceAwaitContract();
        testEndpointAndCaptureGoldenLayouts();
        std::cout << "Genome Preview command infrastructure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Genome Preview command infrastructure tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
