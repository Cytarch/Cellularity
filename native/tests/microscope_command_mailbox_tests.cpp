#include "app/NativeCellularityController.h"
#include "app/NativeSimulationOwner.h"
#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeCommandMailbox.h"
#include "app/microscope/MicroscopePresentationState.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/Environment.h"
#include "tests/support/TestAssertions.h"

using namespace std::chrono_literals;
using cellularity::microscope::MicroscopeCommandApplyResult;
using cellularity::microscope::MicroscopeCommandCompletion;
using cellularity::microscope::MicroscopeCommandCompletionSnapshot;
using cellularity::microscope::MicroscopeCommandEnqueueStatus;
using cellularity::microscope::MicroscopeCommandMailbox;
using cellularity::microscope::microscopeCommandNow;
using cellularity::microscope::MicroscopeCommandResolution;
using cellularity::microscope::MicroscopeCommandTimePoint;
using cellularity::microscope::MicroscopeCommandWaitResult;
using cellularity::microscope::MicroscopePresentationState;
using cellularity::microscope::MicroscopeWorldCommand;
using cellularity::microscope::MicroscopeWorldCommandKind;
using cellularity::microscope::PresentationViewportSnapshot;
using cellularity::simulation::SimulationRatePolicyKind;
using cellularity::tests::assertNear;
using cellularity::tests::assertTrue;

static_assert(!std::is_copy_constructible_v<MicroscopeCommandMailbox>);
static_assert(!std::is_move_constructible_v<MicroscopeCommandMailbox>);
static_assert(!std::is_copy_constructible_v<MicroscopePresentationState>);

namespace {

Environment testEnvironment() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    return environment;
}

MicroscopeCommandTimePoint fixedTime(std::int64_t nanos) {
    return MicroscopeCommandTimePoint(
            cellularity::microscope::MicroscopeCommandDuration(nanos));
}

template <typename Apply>
cellularity::microscope::MicroscopeCommandServiceResult
serviceAll(MicroscopeCommandMailbox& mailbox, Apply&& apply) {
    const auto now = fixedTime(20'000'000'000LL);
    return mailbox.serviceUntil(now + 1s, 10'000, std::forward<Apply>(apply),
            [now] {
                return now;
            });
}

PresentationViewportSnapshot viewportSnapshot(std::uint64_t revision) {
    PresentationViewportSnapshot snapshot;
    snapshot.viewport.revision = revision;
    snapshot.viewport.coverageRevision = revision;
    snapshot.viewport.visibleBounds = {
            static_cast<float>(revision), static_cast<float>(revision + 1),
            static_cast<float>(revision + 2), static_cast<float>(revision + 3)
    };
    snapshot.viewport.coverageBounds = {
            static_cast<float>(revision + 4), static_cast<float>(revision + 5),
            static_cast<float>(revision + 6), static_cast<float>(revision + 7)
    };
    snapshot.viewport.mvpMatrix.fill(static_cast<float>(revision));
    snapshot.viewport.cameraX = static_cast<float>(revision * 2U);
    snapshot.viewport.cameraY = static_cast<float>(revision * 3U);
    snapshot.viewport.zoom = static_cast<float>(revision * 4U);
    snapshot.viewport.viewWidth = static_cast<float>(revision * 5U);
    snapshot.viewport.viewHeight = static_cast<float>(revision * 6U);
    snapshot.viewport.surfaceWidth = static_cast<int>(revision);
    snapshot.viewport.surfaceHeight = static_cast<int>(revision + 10U);
    snapshot.inputSequence = revision * 11U;
    snapshot.publishedNanos = static_cast<std::int64_t>(revision * 13U);
    return snapshot;
}

bool isWholeViewport(const PresentationViewportSnapshot& snapshot) {
    const std::uint64_t revision = snapshot.viewport.revision;
    return revision != 0 && snapshot.viewport.coverageRevision == revision && snapshot.viewport.visibleBounds.minX == static_cast<float>(revision) && snapshot.viewport.visibleBounds.maxY == static_cast<float>(revision + 3U) && snapshot.viewport.coverageBounds.minX == static_cast<float>(revision + 4U) && snapshot.viewport.coverageBounds.maxY == static_cast<float>(revision + 7U) && snapshot.viewport.mvpMatrix.front() == static_cast<float>(revision) && snapshot.viewport.mvpMatrix.back() == static_cast<float>(revision) && snapshot.viewport.cameraX == static_cast<float>(revision * 2U) && snapshot.viewport.cameraY == static_cast<float>(revision * 3U) && snapshot.viewport.zoom == static_cast<float>(revision * 4U) && snapshot.viewport.viewWidth == static_cast<float>(revision * 5U) && snapshot.viewport.viewHeight == static_cast<float>(revision * 6U) && snapshot.viewport.surfaceWidth == static_cast<int>(revision) && snapshot.viewport.surfaceHeight == static_cast<int>(revision + 10U) && snapshot.inputSequence == revision * 11U && snapshot.publishedNanos == static_cast<std::int64_t>(revision * 13U);
}

void testPresentationPublishesNewestWholeViewport() {
    MicroscopePresentationState state;
    assertTrue(state.publishViewport(viewportSnapshot(1)) && state.publishViewport(viewportSnapshot(2)) && state.publishViewport(viewportSnapshot(3)),
            "Complete presentation viewports publish without waiting");

    PresentationViewportSnapshot consumed;
    assertTrue(state.tryConsumeNewestViewport(consumed) && consumed.viewport.revision == 3 && isWholeViewport(consumed),
            "GL receives one immutable newest viewport rather than deltas");
    assertTrue(!state.tryConsumeNewestViewport(consumed),
            "Consumed viewport revisions are not replayed");

    state.publishInteractionGate(-7, true, false);
    const auto gate = state.interactionGate();
    assertTrue(gate.toolOrdinal == -7 && gate.hasDraggingPointers && !gate.hasSelectedCell && gate.revision == 2,
            "Presentation-only gesture gate is one atomic value snapshot");

    const auto snapshot = state.snapshot();
    assertTrue(snapshot.latestPublishedViewportRevision == 3 && snapshot.glViewportExchange.published == 3 && snapshot.glViewportExchange.consumed == 1 && snapshot.ownerViewportExchange.published == 3,
            "Viewport exchange exposes bounded publication telemetry");
}

void testPresentationExchangeHasNoTornConcurrentSnapshots() {
    constexpr std::uint64_t kPublicationCount = 50'000;
    MicroscopePresentationState state;
    std::atomic<bool> producerDone{false};
    std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> lastConsumed{0};

    std::thread producer([&] {
        for (std::uint64_t revision = 1; revision <= kPublicationCount;
                ++revision) {
            if (!state.publishViewport(viewportSnapshot(revision))) {
                failed.store(true, std::memory_order_release);
                break;
            }
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        std::uint64_t previous = 0;
        while (!producerDone.load(std::memory_order_acquire) || previous < kPublicationCount) {
            PresentationViewportSnapshot consumed;
            if (!state.tryConsumeNewestViewport(consumed)) {
                std::this_thread::yield();
                continue;
            }
            if (!isWholeViewport(consumed) || consumed.viewport.revision <= previous) {
                failed.store(true, std::memory_order_release);
                break;
            }
            previous = consumed.viewport.revision;
        }
        lastConsumed.store(previous, std::memory_order_release);
    });

    producer.join();
    consumer.join();
    assertTrue(!failed.load(std::memory_order_acquire) && lastConsumed.load(std::memory_order_acquire) == kPublicationCount,
            "Concurrent latest-value publication is monotonic and never torn");
    const auto counters = state.snapshot().glViewportExchange;
    assertTrue(counters.published == kPublicationCount && counters.publishRetriesExhausted == 0,
            "Presentation stress remains finite and never blocks on capacity");
}

void testPositionMoveCoalescingAndTerminalReservation() {
    MicroscopeCommandMailbox mailbox(7);
    const auto down = mailbox.enqueueTouchDown(4, 1.0, 2.0, 9, fixedTime(1));
    assertTrue(down.accepted(), "TouchDown enters the ordered command area");

    cellularity::microscope::MicroscopeCommandEnqueueResult latestMove;
    for (std::uint64_t sample = 1; sample <= 2'400; ++sample) {
        latestMove = mailbox.enqueueTouchMovePosition(
                4, static_cast<double>(sample), -static_cast<double>(sample),
                9 + sample,
                fixedTime(1 + static_cast<std::int64_t>(sample) * 4'166'667));
        assertTrue(latestMove.accepted(),
                "Position-state samples always replace within fixed storage");
    }
    const auto up = mailbox.enqueueTouchUp(4, fixedTime(11'000'000'000LL));
    const auto lateMove = mailbox.enqueueTouchMovePosition(
            4, 9.0, 9.0, 99, fixedTime(12'000'000'000LL));
    assertTrue(up.accepted() && lateMove.status == MicroscopeCommandEnqueueStatus::RejectedPointerState,
            "Reserved terminal closes the generation before a late Move");

    std::vector<MicroscopeWorldCommand> applied;
    const auto result =
            serviceAll(mailbox, [&](const MicroscopeWorldCommand& command) {
                applied.push_back(command);
                return MicroscopeCommandApplyResult::applied(
                        command.kind == MicroscopeWorldCommandKind::TouchMovePosition,
                        false);
            });
    assertTrue(
            result.processed == 3 && applied.size() == 3 && applied[0].kind == MicroscopeWorldCommandKind::TouchDown && applied[1].kind == MicroscopeWorldCommandKind::TouchMovePosition && applied[1].sequence == latestMove.sequence && applied[1].worldX == 2'400.0 && applied[1].worldY == -2'400.0 && applied[1].eventViewportRevision == 2'409 && applied[2].kind == MicroscopeWorldCommandKind::TouchUp,
            "Ten seconds at 240 samples/s resolves Down, newest Move, then Up");
    const auto snapshot = mailbox.snapshot();
    assertTrue(snapshot.counters.coalesced == 2'399 && snapshot.counters.orderedHighWater <= MicroscopeCommandMailbox::kOrderedCapacity && snapshot.counters.terminalAccepted == 1 && snapshot.counters.activePointerGenerations == 0 && snapshot.resolved.lastResolvedInputSequence == lateMove.sequence && snapshot.resolved.appliedWorldRevision == 1 && !snapshot.ownerWorkPending,
            "Coalescing is bounded and publishes one contiguous resolution "
            "watermark");
}

void testCancelClosesPointerGeneration() {
    MicroscopeCommandMailbox mailbox(9);
    const auto down = mailbox.enqueueTouchDown(5, 0.0, 0.0, 1);
    const auto move = mailbox.enqueueTouchMovePosition(5, 1.0, 2.0, 1);
    const auto cancel = mailbox.enqueueTouchCancel(5);
    const auto lateMove = mailbox.enqueueTouchMovePosition(5, 3.0, 4.0, 1);
    assertTrue(down.accepted() && move.accepted() && cancel.accepted() && lateMove.status == MicroscopeCommandEnqueueStatus::RejectedPointerState,
            "Cancel reserves and closes its pointer generation like Up");

    std::vector<MicroscopeWorldCommandKind> kinds;
    serviceAll(mailbox, [&](const MicroscopeWorldCommand& command) {
        kinds.push_back(command.kind);
        return MicroscopeCommandApplyResult::applied();
    });
    assertTrue(kinds == std::vector<MicroscopeWorldCommandKind>{MicroscopeWorldCommandKind::TouchDown, MicroscopeWorldCommandKind::TouchMovePosition, MicroscopeWorldCommandKind::TouchCancel} && mailbox.snapshot().counters.activePointerGenerations == 0,
            "No Move can be applied after the accepted Cancel terminal");
}

void testPathSegmentsRemainOrderedAndBounded() {
    MicroscopeCommandMailbox mailbox(11);
    assertTrue(mailbox.enqueueTouchDown(1, 0.0, 0.0, 1).accepted(),
            "Path fixture reserves its pointer terminal");
    for (std::uint32_t index = 0;
            index < MicroscopeCommandMailbox::kPathSegmentCapacity; ++index) {
        const auto result = mailbox.enqueueTouchMovePathSegment(
                1, static_cast<double>(index), static_cast<double>(index + 1U), 1);
        assertTrue(result.accepted(), "Every reserved path segment is accepted");
    }
    const auto overflow = mailbox.enqueueTouchMovePathSegment(1, 99.0, 99.0, 1);
    assertTrue(overflow.status == MicroscopeCommandEnqueueStatus::RejectedPathCapacity,
            "Path overflow returns a typed result instead of losing geometry");

    std::uint32_t acceptedTaps = 0;
    for (;;) {
        const auto tap = mailbox.enqueueTap(1.0, 2.0, 1);
        if (!tap.accepted()) {
            assertTrue(tap.status == MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity,
                    "Discrete saturation reports typed busy");
            break;
        }
        ++acceptedTaps;
    }
    const auto up = mailbox.enqueueTouchUp(1);
    assertTrue(up.accepted(),
            "TouchUp remains admissible after the ordinary area saturates");

    std::uint32_t nextPath = 0;
    bool sawUp = false;
    const auto service =
            serviceAll(mailbox, [&](const MicroscopeWorldCommand& command) {
                if (command.kind == MicroscopeWorldCommandKind::TouchMovePathSegment) {
                    assertTrue(command.worldX == static_cast<double>(nextPath),
                            "Path-dependent segments preserve exact arrival order");
                    ++nextPath;
                }
                sawUp = sawUp || command.kind == MicroscopeWorldCommandKind::TouchUp;
                return MicroscopeCommandApplyResult::applied();
            });
    const auto snapshot = mailbox.snapshot();
    assertTrue(
            acceptedTaps > 0 && nextPath == MicroscopeCommandMailbox::kPathSegmentCapacity && sawUp && service.processed == acceptedTaps + nextPath + 2U && snapshot.counters.pathSegmentHighWater == MicroscopeCommandMailbox::kPathSegmentCapacity && snapshot.counters.pathSegmentDepth == 0,
            "Bounded path storage drains completely without endpoint collapse");
}

void testLifecycleReserveSurvivesDiscreteSaturation() {
    MicroscopeCommandMailbox mailbox(13);
    std::uint32_t acceptedTaps = 0;
    for (;;) {
        const auto result = mailbox.enqueueTap(0.0, 0.0, 1);
        if (!result.accepted()) {
            break;
        }
        ++acceptedTaps;
    }
    assertTrue(acceptedTaps == MicroscopeCommandMailbox::kOrderedCapacity - MicroscopeCommandMailbox::kLifecycleReserve,
            "Ordinary discrete commands leave the full lifecycle reserve");

    for (const MicroscopeWorldCommandKind kind :
            {MicroscopeWorldCommandKind::Reset, MicroscopeWorldCommandKind::Reload,
                    MicroscopeWorldCommandKind::LifecyclePause,
                    MicroscopeWorldCommandKind::LifecycleResume}) {
        assertTrue(mailbox.enqueueBarrier(kind).accepted(),
                "Reserved lifecycle command survives saturated ordinary input");
    }
    const auto extra = mailbox.enqueueBarrier(MicroscopeWorldCommandKind::Reset);
    assertTrue(extra.status == MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity,
            "Exhausted lifecycle reserve reports typed busy");
    const auto saturated = mailbox.snapshot();
    assertTrue(saturated.counters.orderedDepth == MicroscopeCommandMailbox::kOrderedCapacity && saturated.counters.lifecycleAccepted == MicroscopeCommandMailbox::kLifecycleReserve,
            "The fixed ordered ring has a finite measurable high-water mark");

    const auto drained = serviceAll(mailbox, [](const MicroscopeWorldCommand&) {
        return MicroscopeCommandApplyResult::applied();
    });
    assertTrue(drained.processed == MicroscopeCommandMailbox::kOrderedCapacity && !mailbox.hasPendingOwnerWork(),
            "Saturated discrete and lifecycle work drains without replay");
}

void testLatestSettingsAndRateValidation() {
    MicroscopeCommandMailbox mailbox(17);
    const auto temperature1 = mailbox.enqueueTemperatureMode(1);
    const auto temperature2 = mailbox.enqueueTemperatureMode(2);
    const auto temperature3 = mailbox.enqueueTemperatureMode(3);
    const auto tool1 = mailbox.enqueueToolOrdinal(4);
    const auto tool2 = mailbox.enqueueToolOrdinal(5);
    const auto slow1 = mailbox.enqueueSlowMotionSpeed(0.1);
    const auto slow2 = mailbox.enqueueSlowMotionSpeed(0.25);
    const auto invalidSlow = mailbox.enqueueSlowMotionSpeed(-0.1);
    const auto maxRate = mailbox.enqueueRatePolicy(
            SimulationRatePolicyKind::MaxThroughput, std::nullopt);
    const auto invalidMax =
            mailbox.enqueueRatePolicy(SimulationRatePolicyKind::MaxThroughput, 60.0);
    const auto invalidTarget = mailbox.enqueueRatePolicy(
            SimulationRatePolicyKind::TargetRate, std::nullopt);
    const auto invalidPolicy = mailbox.enqueueRatePolicy(
            static_cast<SimulationRatePolicyKind>(255), std::nullopt);
    const auto targetRate =
            mailbox.enqueueRatePolicy(SimulationRatePolicyKind::TargetRate, 120.0);
    const auto target1 = mailbox.enqueueOptionalTargetUps(90.0);
    const auto target2 = mailbox.enqueueOptionalTargetUps(std::nullopt);

    assertTrue(temperature1.accepted() && temperature2.accepted() && temperature3.accepted() && tool1.accepted() && tool2.accepted() && slow1.accepted() && slow2.accepted() && maxRate.accepted() && targetRate.accepted() && target1.accepted() && target2.accepted() && invalidSlow.status == MicroscopeCommandEnqueueStatus::RejectedInvalidArgument && invalidMax.status == MicroscopeCommandEnqueueStatus::RejectedInvalidArgument && invalidTarget.status == MicroscopeCommandEnqueueStatus::RejectedInvalidArgument && invalidPolicy.status == MicroscopeCommandEnqueueStatus::RejectedInvalidArgument,
            "Slow-speed, rate, and optional-target contracts reject invalid "
            "values");

    std::vector<MicroscopeWorldCommand> applied;
    serviceAll(mailbox, [&](const MicroscopeWorldCommand& command) {
        applied.push_back(command);
        return MicroscopeCommandApplyResult::applied();
    });
    assertTrue(
            applied.size() == 5 && applied[0].sequence == temperature3.sequence && applied[0].integerValue == 3 && applied[1].sequence == tool2.sequence && applied[1].integerValue == 5 && applied[2].sequence == slow2.sequence && applied[2].kind == MicroscopeWorldCommandKind::SetSlowMotionSpeed && applied[2].hasNumericValue && applied[2].numericValue == 0.25 && applied[3].sequence == targetRate.sequence && applied[3].ratePolicy == SimulationRatePolicyKind::TargetRate && applied[3].hasNumericValue && applied[3].numericValue == 120.0 && applied[4].sequence == target2.sequence && !applied[4].hasNumericValue,
            "Owner applies only the newest complete value from each setting lane");
    const auto snapshot = mailbox.snapshot();
    assertTrue(
            snapshot.counters.coalesced == 6 && snapshot.resolved.lastResolvedInputSequence == target2.sequence,
            "Coalesced and rejected setting sequences join one contiguous watermark");
}

void testEpochBarrierCompletionAndPresentationStates() {
    MicroscopeCommandMailbox mailbox(23);
    auto staleCompletion = std::make_shared<MicroscopeCommandCompletion>();
    const auto save = mailbox.enqueueRequest(MicroscopeWorldCommandKind::Save, 1,
            staleCompletion, fixedTime(100));
    const auto oldTap = mailbox.enqueueTap(3.0, 4.0, 7, nullptr, fixedTime(200));
    assertTrue(save.accepted() && oldTap.accepted(),
            "Old-epoch request and input enter before replacement");

    mailbox.advanceWorldEpoch(24, fixedTime(300));
    auto appliedCompletion = std::make_shared<MicroscopeCommandCompletion>();
    const auto inspect =
            mailbox.enqueueRequest(MicroscopeWorldCommandKind::Inspect, 2,
                    appliedCompletion, fixedTime(400));
    auto applyCompletion = std::make_shared<MicroscopeCommandCompletion>();
    const auto apply = mailbox.enqueueRequest(MicroscopeWorldCommandKind::Apply,
            3, applyCompletion, fixedTime(450));
    const auto pendingNewEpoch = mailbox.resolvedState();
    assertTrue(
            inspect.accepted() && apply.accepted() && pendingNewEpoch.worldEpoch == 24 && pendingNewEpoch.lastResolvedInputSequence < inspect.sequence && appliedCompletion->snapshot().resolution == MicroscopeCommandResolution::Pending,
            "New-epoch requests remain beyond the resolved watermark until owner "
            "application");

    auto waiter = std::async(std::launch::async, [&] {
        MicroscopeCommandCompletionSnapshot snapshot;
        const auto waitResult =
                appliedCompletion->waitUntil(microscopeCommandNow() + 2s, snapshot);
        return std::make_pair(waitResult, snapshot);
    });

    std::vector<MicroscopeWorldCommand> applied;
    const auto service =
            serviceAll(mailbox, [&](const MicroscopeWorldCommand& command) {
                applied.push_back(command);
                return MicroscopeCommandApplyResult::applied(true, true);
            });
    const auto waited = waiter.get();
    const auto stale = staleCompletion->snapshot();
    const auto appliedApply = applyCompletion->snapshot();
    const auto beforePresentation = mailbox.resolvedState();
    assertTrue(
            service.staleEpoch == 2 && service.applied == 2 && applied.size() == 2 && applied.front().sequence == inspect.sequence && applied.back().sequence == apply.sequence && stale.resolution == MicroscopeCommandResolution::StaleEpoch && waited.first == MicroscopeCommandWaitResult::Notified && waited.second.resolution == MicroscopeCommandResolution::Applied && appliedApply.resolution == MicroscopeCommandResolution::Applied && beforePresentation.worldEpoch == 24 && beforePresentation.appliedWorldRevision == 2 && beforePresentation.presentedResolvedInputSequence == 0 && beforePresentation.presentedWorldRevision == 0,
            "Epoch replacement stales old work and keeps accepted, applied, and "
            "presented distinct");

    mailbox.markPresented(23, apply.sequence, 2, fixedTime(500));
    assertTrue(mailbox.resolvedState().presentedWorldRevision == 0,
            "Presentation from a retired epoch is ignored");
    mailbox.markPresented(24, apply.sequence, 2, fixedTime(600));
    const auto presented = mailbox.resolvedState();
    assertTrue(
            presented.presentedResolvedInputSequence == apply.sequence && presented.presentedWorldRevision == 2 && presented.lastPresentedNanos == 600,
            "GL explicitly marks the first compatible applied revision presented");
}

void testViewportCaptureHintIsNewestOnly() {
    MicroscopeCommandMailbox mailbox(29);
    mailbox.publishViewportCaptureHint(4);
    mailbox.publishViewportCaptureHint(7);
    mailbox.publishViewportCaptureHint(12);
    const auto newest = mailbox.consumeViewportCaptureHint();
    const auto empty = mailbox.consumeViewportCaptureHint();
    const auto snapshot = mailbox.snapshot();
    assertTrue(newest == std::optional<std::uint64_t>(12) && !empty.has_value() && snapshot.counters.viewportCaptureHints == 3 && snapshot.counters.viewportCaptureHintsCoalesced == 2 && snapshot.counters.viewportCaptureHintsConsumed == 1,
            "Camera capture hints collapse to one newest viewport revision");
}

void testNotificationDeadlineHardLimitAndEventCoordinates() {
    MicroscopeCommandMailbox mailbox(31);
    const auto observed = mailbox.notificationGeneration();
    assertTrue(!mailbox.hasNotificationSince(observed),
            "Unchanged notification generation is an O(1) idle probe");
    const auto invalid = mailbox.enqueueTap(0.0, 0.0, 0);
    assertTrue(
            !invalid.accepted() && mailbox.hasNotificationSince(observed) && mailbox.waitForNotification(observed, microscopeCommandNow() + 100ms) == MicroscopeCommandWaitResult::Notified,
            "Every completed ingress publication changes the O(1) interruption "
            "generation, including watermark-only rejection service");

    const auto deadline = fixedTime(1'000);
    const auto deferred = mailbox.serviceUntil(
            deadline, 10,
            [](const MicroscopeWorldCommand&) {
                return MicroscopeCommandApplyResult::applied();
            },
            [deadline] {
                return deadline;
            });
    assertTrue(deferred.processed == 0 && deferred.deadlineReached,
            "Expired owner budget performs no unbounded work");
    serviceAll(mailbox, [](const MicroscopeWorldCommand&) {
        return MicroscopeCommandApplyResult::applied();
    });

    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(1'000, 500);
    viewport.setCamera(0.1f, -0.2f, 7.0f);
    const double eventWorldX = viewport.screenToWorldX(123.0f);
    const double eventWorldY = viewport.screenToWorldY(321.0f);
    const std::uint64_t eventRevision = viewport.revision();
    const auto tap = mailbox.enqueueTap(eventWorldX, eventWorldY, eventRevision);
    assertTrue(tap.accepted(), "Event-time world coordinate command is accepted");
    viewport.setCamera(-0.7f, 0.8f, 19.0f);
    const auto laterTap = mailbox.enqueueTap(7.0, 8.0, viewport.revision());
    assertTrue(laterTap.accepted(),
            "Later-camera command remains independently ordered");

    std::vector<MicroscopeWorldCommand> applied;
    const auto limited = mailbox.serviceUntil(
            fixedTime(30'000'000'000LL), 1,
            [&](const MicroscopeWorldCommand& command) {
                applied.push_back(command);
                return MicroscopeCommandApplyResult::applied();
            },
            [] {
                return fixedTime(20'000'000'000LL);
            });
    assertTrue(limited.processed == 1 && mailbox.hasPendingOwnerWork() && applied.front().sequence == tap.sequence && applied.front().eventViewportRevision == eventRevision && applied.front().worldX == eventWorldX && applied.front().worldY == eventWorldY,
            "Hard limit preserves event-time coordinates independent of later "
            "camera state");
    serviceAll(mailbox, [](const MicroscopeWorldCommand&) {
        return MicroscopeCommandApplyResult::applied();
    });
}

void testConcurrentHighRateProducerAndOwnerRemainBounded() {
    constexpr std::uint64_t kMoveCount = 2'400;
    MicroscopeCommandMailbox mailbox(37);
    std::atomic<bool> producerDone{false};
    std::atomic<bool> failed{false};
    std::uint64_t lastAppliedSequence = 0;
    std::uint64_t lastMoveSequence = 0;
    bool terminalApplied = false;

    std::thread producer([&] {
        if (!mailbox.enqueueTouchDown(8, 0.0, 0.0, 1).accepted()) {
            failed.store(true, std::memory_order_release);
        }
        for (std::uint64_t sample = 1; sample <= kMoveCount; ++sample) {
            const auto result = mailbox.enqueueTouchMovePosition(
                    8, static_cast<double>(sample), static_cast<double>(sample), sample,
                    fixedTime(static_cast<std::int64_t>(sample * 4'166'667U)));
            if (!result.accepted()) {
                failed.store(true, std::memory_order_release);
                break;
            }
        }
        if (!mailbox.enqueueTouchUp(8).accepted()) {
            failed.store(true, std::memory_order_release);
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread owner([&] {
        while (!producerDone.load(std::memory_order_acquire) || mailbox.hasPendingOwnerWork()) {
            const auto now = microscopeCommandNow();
            mailbox.serviceUntil(
                    now + 2ms, 16,
                    [&](const MicroscopeWorldCommand& command) {
                        if (command.sequence <= lastAppliedSequence) {
                            failed.store(true, std::memory_order_release);
                        }
                        lastAppliedSequence = command.sequence;
                        if (command.kind == MicroscopeWorldCommandKind::TouchMovePosition) {
                            lastMoveSequence = command.sequence;
                        }
                        terminalApplied =
                                terminalApplied || command.kind == MicroscopeWorldCommandKind::TouchUp;
                        return MicroscopeCommandApplyResult::applied();
                    },
                    [] {
                        return microscopeCommandNow();
                    });
            std::this_thread::yield();
        }
    });

    producer.join();
    owner.join();
    const auto snapshot = mailbox.snapshot();
    assertTrue(
            !failed.load(std::memory_order_acquire) && terminalApplied && lastMoveSequence != 0 && !snapshot.ownerWorkPending && snapshot.counters.orderedHighWater <= MicroscopeCommandMailbox::kOrderedCapacity && snapshot.resolved.lastResolvedInputSequence == snapshot.resolved.lastIssuedInputSequence,
            "Concurrent 240 Hz ingress has no stuck latest payload or terminal loss");
}

void testCameraCommandsDoNotAcquireWorldOwnerLease() {
    NativeCellularityController controller(false, testEnvironment());
    controller.setViewportSurfaceSize(640, 360);
    const auto initial = controller.viewportState();
    std::future<bool> cameraCommands;
    bool completedWhileOwnerHeld = false;
    {
        auto ownerAccess = controller.writeWorld();
        cameraCommands = std::async(std::launch::async, [&] {
            return controller.queueMicroscopePan(40.0f, -10.0f) && controller.queueMicroscopeZoom(1.1f, 320.0f, 180.0f) && controller.queueMicroscopeFling(2.0f, 3.0f);
        });
        completedWhileOwnerHeld =
                cameraCommands.wait_for(500ms) == std::future_status::ready;
        (void)ownerAccess.worldEpoch();
    }
    const bool accepted = cameraCommands.get();
    const auto changed = controller.viewportState();
    assertTrue(completedWhileOwnerHeld && accepted && initial[6] == 0.0f && changed[6] == 1.0f && changed != initial,
            "Pan, Zoom, and Fling bypass the world-owner lock while the "
            "coherent camera snapshot reports active inertia");
}

void testSmallFlingPreservesExistingDecaySemantics() {
    NativeCellularityController controller(false, testEnvironment());
    controller.setViewportSurfaceSize(640, 360);
    controller.setMicroscopeViewportFlingVelocity(10.0f, -10.0f);
    const auto before = controller.viewportState();

    const bool moved = controller.stepMicroscopeViewportInertia();
    const auto after = controller.viewportState();

    assertTrue(moved && before[6] == 1.0f && after[6] == 1.0f && (before[0] != after[0] || before[1] != after[1]),
            "Renderer cadence changes do not truncate the existing fling "
            "decay or alter camera movement semantics");
}

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
void testMailboxApplicationIsBoundToOwnerWriteAccess() {
    NativeSimulationOwner owner(false, testEnvironment());
    std::uint64_t epoch = 0;
    {
        auto access = owner.read();
        epoch = access.worldEpoch();
    }
    MicroscopeCommandMailbox mailbox(epoch);
    assertTrue(mailbox.enqueueToolOrdinal(6).accepted() && mailbox.enqueueTap(0.25, -0.5, 1).accepted(),
            "Owner integration fixture queues value-only commands");

    std::uint32_t mutations = 0;
    {
        auto access = owner.write();
        const auto now = microscopeCommandNow();
        const auto service = access.serviceMicroscopeCommands(
                mailbox, now + 1s, 16,
                [&](NativeSimulationOwner::WriteAccess& ownerAccess,
                        const MicroscopeWorldCommand& command) {
                    if (command.kind == MicroscopeWorldCommandKind::SetToolOrdinal) {
                        ownerAccess.microscopeInteraction().setToolOrdinal(
                                command.integerValue);
                    } else if (command.kind == MicroscopeWorldCommandKind::Tap) {
                        ownerAccess.world().mEnvironment.mLightAngle = command.worldX;
                    }
                    ++mutations;
                    return MicroscopeCommandApplyResult::applied(true, true);
                },
                [now] {
                    return now;
                });
        assertTrue(service.processed == 2 && service.applied == 2 && service.resolved.appliedWorldRevision == 2,
                "Mailbox commands apply at one explicit owner safe point");
    }
    {
        auto access = owner.read();
        assertTrue(mutations == 2 && access.microscopeInteraction().toolOrdinal() == 6 && access.world().mEnvironment.mLightAngle == 0.25,
                "Only owner-held application mutates world-facing state");
    }
}
#endif

} // namespace

int main() {
    testPresentationPublishesNewestWholeViewport();
    testPresentationExchangeHasNoTornConcurrentSnapshots();
    testPositionMoveCoalescingAndTerminalReservation();
    testCancelClosesPointerGeneration();
    testPathSegmentsRemainOrderedAndBounded();
    testLifecycleReserveSurvivesDiscreteSaturation();
    testLatestSettingsAndRateValidation();
    testEpochBarrierCompletionAndPresentationStates();
    testViewportCaptureHintIsNewestOnly();
    testNotificationDeadlineHardLimitAndEventCoordinates();
    testConcurrentHighRateProducerAndOwnerRemainBounded();
    testCameraCommandsDoNotAcquireWorldOwnerLease();
    testSmallFlingPreservesExistingDecaySemantics();
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    testMailboxApplicationIsBoundToOwnerWriteAccess();
#endif
    return 0;
}
