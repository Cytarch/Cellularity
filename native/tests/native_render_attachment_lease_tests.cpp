#include "app/render/NativeRenderAttachmentLease.h"
#include "jni/NativeRenderEndpointHandle.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

#include "app/NativeCellularityController.h"
#include "app/render/RenderSurfaceSessionBinding.h"
#include "core/Environment.h"
#include "tests/support/FakeRenderSurfaceSession.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity::app;
using namespace cellularity::tests;
using namespace std::chrono_literals;

#ifndef __ANDROID__
class AndroidRenderAssetsLease final {
};
#endif

static_assert(!std::is_copy_constructible_v<FrameGuard>);
static_assert(std::is_nothrow_move_constructible_v<FrameGuard>);
static_assert(!std::is_copy_constructible_v<ControllerRebindTicket>);
static_assert(std::is_nothrow_move_constructible_v<ControllerRebindTicket>);
static_assert(!std::is_move_assignable_v<ControllerRebindTicket>);
static_assert(!std::is_copy_constructible_v<RebindWakeRegistration>);
static_assert(std::is_nothrow_move_constructible_v<RebindWakeRegistration>);

namespace {

std::shared_ptr<NativeCellularityController> createController() {
    Environment environment =
            Environment::constructDefaultEnvironment(8, 8, 1.0);
    environment.addRandom = false;
    return std::make_shared<NativeCellularityController>(false, environment);
}

class FakeVulkanBinding final : public cellularity::render::RenderSurfaceSessionBinding {
public:
    cellularity::render::RenderBackendKind kind() const noexcept override {
        return cellularity::render::RenderBackendKind::Vulkan;
    }

    const void* identity() const noexcept override {
        return this;
    }

    std::unique_ptr<cellularity::render::RenderSurfaceSession> createControllerAdapter() const noexcept override {
        auto adapter = std::make_unique<FakeRenderSurfaceSession>();
        adapter->backendKind = kind();
        adapter->identity = identity();
        return adapter;
    }
};

void testGlesResourcesRequireCurrentSurfaceAndLiveAdapter() {
    auto controller = createController();
    auto adapter = std::make_unique<FakeRenderSurfaceSession>();
    auto* gles = adapter.get();
    gles->resources = true;
    controller->installRenderSurfaceSession(std::move(adapter));
    NativeRenderAttachmentLease lease(controller);
    const auto worker = lease.authorizeWorker(11, 11);
    const auto surface = lease.activateSurface(*worker.value, 1, 640, 480);
    auto frame = lease.tryAcquireFrame(*surface.value);
    assertTrue(frame.guard.has_value(), "The current GLES surface admits a frame");
    assertTrue(!lease.hasGlesResourcesForFrame(*frame.guard), "An unmarked frame must load GLES resources");
    assertTrue(lease.markGlesResourcesLoaded(frame.guard->key().controllerGeneration) == LeaseMutationStatus::Applied,
            "The current frame records its resource generation");
    assertTrue(lease.hasGlesResourcesForFrame(*frame.guard), "Steady frames reuse the current live GLES resources");

    NativeRenderAttachmentLease other(createController());
    assertTrue(!other.hasGlesResourcesForFrame(*frame.guard), "A frame cannot authorize resources in another lease");
    assertTrue(lease.updateSurfaceDimensions(*surface.value, 800, 600) == LeaseMutationStatus::Applied,
            "A resize retains the current attachment identity");
    assertTrue(lease.hasGlesResourcesForFrame(*frame.guard), "Resizing does not reload resources on every frame");

    controller->abandonRendererResources();
    assertTrue(!lease.hasGlesResourcesForFrame(*frame.guard), "A generation marker cannot authorize abandoned resources");
    frame.guard.reset();
    const auto replacement = lease.activateSurface(*worker.value, 2, 800, 600);
    auto next = lease.tryAcquireFrame(*replacement.value);
    gles->resources = true;
    assertTrue(lease.generationSnapshot().glesResourcesControllerGeneration == 0,
            "Recreating the EGL surface invalidates the loaded marker");
    assertTrue(!lease.hasGlesResourcesForFrame(*next.guard), "A new context cannot reuse the prior attachment's loaded marker");
    assertTrue(lease.markGlesResourcesLoaded(next.guard->key().controllerGeneration) == LeaseMutationStatus::Applied,
            "The replacement surface can record its own resources");
    assertTrue(lease.hasGlesResourcesForFrame(*next.guard), "The replacement surface reuses resources after its first load");
    next.guard.reset();
    assertTrue(lease.deactivateSurface(*worker.value) == LeaseMutationStatus::Applied,
            "The current owner can detach its surface");
    assertTrue(lease.generationSnapshot().glesResourcesControllerGeneration == 0, "Surface detach invalidates the GLES cache");
    const auto tokenSurface = lease.activateSurface(*worker.value, 3, 800, 600);
    auto tokenFrame = lease.tryAcquireFrame(*tokenSurface.value);
    assertTrue(lease.markGlesResourcesLoaded(tokenFrame.guard->key().controllerGeneration) == LeaseMutationStatus::Applied,
            "The token-addressed surface records its resources");
    assertTrue(lease.deactivateSurface(worker.value->surfaceToken + 1) == LeaseMutationStatus::StaleSurface,
            "A stale surface token cannot invalidate the current resources");
    assertTrue(lease.hasGlesResourcesForFrame(*tokenFrame.guard), "Stale detach leaves current resources valid");
    tokenFrame.guard.reset();
    assertTrue(lease.deactivateSurface(worker.value->surfaceToken) == LeaseMutationStatus::Applied,
            "The token-addressed surface can detach its resources");
    assertTrue(lease.generationSnapshot().glesResourcesControllerGeneration == 0,
            "Token-addressed surface detach also invalidates the GLES cache");
}

void testGlesVulkanGlesTransitionsInvalidateResources() {
    auto controller = createController();
    NativeRenderAttachmentLease lease(controller);
    for (std::uint64_t cycle = 1; cycle <= 3; ++cycle) {
        auto gles = std::make_unique<FakeRenderSurfaceSession>();
        gles->resources = true;
        controller->installRenderSurfaceSession(std::move(gles));
        const auto glesWorker = lease.authorizeWorker(cycle * 2, cycle * 2);
        const auto surface = lease.activateSurface(*glesWorker.value, 1, 640, 480);
        auto frame = lease.tryAcquireFrame(*surface.value);
        assertTrue(!lease.hasGlesResourcesForFrame(*frame.guard), "Each GLES backend installation requires an initial resource load");
        assertTrue(lease.markGlesResourcesLoaded(frame.guard->key().controllerGeneration) == LeaseMutationStatus::Applied,
                "GLES resources are marked for the retained controller");
        assertTrue(lease.hasGlesResourcesForFrame(*frame.guard), "Loaded GLES resources satisfy the production frame check");
        frame.guard.reset();
        controller->abandonRendererResources();
        assertTrue(lease.markWorkerClosing(*glesWorker.value) == LeaseMutationStatus::Applied, "The GLES worker starts closing");
        assertTrue(lease.finishWorkerClose(*glesWorker.value) == LeaseMutationStatus::Applied, "The GLES worker releases ownership");

        const auto vulkanWorker = lease.authorizeWorker(cycle * 2 + 1, cycle * 2 + 1);
        const auto binding = std::make_shared<FakeVulkanBinding>();
        assertTrue(lease.installBackendBinding(*vulkanWorker.value, binding) == LeaseMutationStatus::Applied,
                "Vulkan replaces the abandoned GLES adapter");
        assertTrue(lease.generationSnapshot().glesResourcesControllerGeneration == 0,
                "Backend replacement clears the old GLES loaded marker");
        assertTrue(lease.removeBackendBinding(*vulkanWorker.value) == LeaseMutationStatus::Applied,
                "Vulkan teardown removes only its own adapter");
        assertTrue(!controller->hasRenderSurfaceSession(), "Vulkan teardown leaves no surface adapter");
        assertTrue(lease.generationSnapshot().glesResourcesControllerGeneration == 0,
                "An absent adapter never retains a GLES loaded marker");
        assertTrue(lease.markWorkerClosing(*vulkanWorker.value) == LeaseMutationStatus::Applied, "The Vulkan worker starts closing");
        assertTrue(lease.finishWorkerClose(*vulkanWorker.value) == LeaseMutationStatus::Applied, "The Vulkan worker releases ownership");
    }
}

void testRenderEndpointHandleOwnsDurableSharedLeases() {
    auto attachment = std::make_shared<NativeRenderAttachmentLease>(
            createController());
    auto assets = std::make_shared<AndroidRenderAssetsLease>();
    const std::weak_ptr<NativeRenderAttachmentLease> attachmentWeak =
            attachment;
    const std::weak_ptr<AndroidRenderAssetsLease> assetsWeak = assets;

    const std::uint64_t handle = createNativeRenderEndpointHandle(
            attachment, assets);
    attachment.reset();
    assets.reset();
    auto borrowedAttachment = renderEndpointLeaseFromHandle(handle);
    auto borrowedAssets = renderEndpointAssetsFromHandle(handle);
    assertTrue(borrowedAttachment != nullptr && borrowedAssets != nullptr,
            "A live endpoint handle lends both durable native leases");

    closeAndDestroyNativeRenderEndpointHandle(handle);
    assertTrue(!attachmentWeak.expired() && !assetsWeak.expired(),
            "In-flight endpoint borrows outlive managed handle closure");
    borrowedAttachment.reset();
    borrowedAssets.reset();
    assertTrue(attachmentWeak.expired() && assetsWeak.expired(),
            "Endpoint resources release after the final in-flight borrow");
}

void testNativeViewportInitializationAndSurfaceRecreation() {
    Environment environment =
            Environment::constructDefaultEnvironment(8, 8, 2.0);
    environment.addRandom = false;
    NativeCellularityController controller(false, environment);
    const auto initial = controller.viewportState();
    assertTrue(initial[0] == 0.0F && initial[1] == 0.0F
                    && initial[2] == 0.5F,
            "A fresh microscope controller owns the canonical default camera");

    controller.setViewportSurfaceSize(800, 600);
    controller.setViewportCamera(0.1F, -0.1F, 2.0F);
    const auto retained = controller.viewportState();
    controller.abandonRendererResources();
    controller.setViewportSurfaceSize(600, 800);
    const auto recreated = controller.viewportState();
    assertTrue(recreated[0] == retained[0]
                    && recreated[1] == retained[1]
                    && recreated[2] == retained[2],
            "Same-controller surface recreation never resets the retained camera");
}

struct ActiveAttachment final {
    WorkerBindingKey worker;
    AttachmentKey attachment;
};

ActiveAttachment activate(NativeRenderAttachmentLease& lease,
        std::uint64_t surfaceToken = 11,
        std::uint64_t workerIdentity = 17,
        std::uint64_t platformGeneration = 23) {
    auto worker = lease.authorizeWorker(surfaceToken, workerIdentity);
    assertTrue(worker.status == LeaseMutationStatus::Applied
                    && worker.value.has_value(),
            "A fresh worker is authorized");
    auto attachment = lease.activateSurface(
            *worker.value, platformGeneration, 640, 480);
    assertTrue(attachment.status == LeaseMutationStatus::Applied
                    && attachment.value.has_value(),
            "An authorized worker activates a surface");
    return {*worker.value, *attachment.value};
}

class CountingWake final : public RebindWakeControl {
public:
    void notifyRebindReady() noexcept override {
        notifications.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> notifications{0};
};

void testSteadyFramesUseCachedAttachmentKey() {
    NativeRenderAttachmentLease lease(createController());
    const auto active = activate(lease);
    const auto before = lease.debugCounters();

    for (int frame = 0; frame < 100; ++frame) {
        auto acquired = lease.tryAcquireFrame(active.attachment);
        assertTrue(acquired.status == FrameAcquireStatus::Acquired
                        && acquired.guard.has_value()
                        && acquired.guard->valid(),
                "A cached current attachment key acquires every frame");
    }

    const auto after = lease.debugCounters();
    assertTrue(after.frameAcquireAttemptCount
                                    - before.frameAcquireAttemptCount
                            == 100
                    && after.frameAcquireSuccessCount
                                    - before.frameAcquireSuccessCount
                            == 100,
            "One frame attempt and one guard cover every steady frame");
    assertTrue(after.attachmentSnapshotCount
                    == before.attachmentSnapshotCount,
            "Steady frames do not perform attachment snapshots");
}

void testRebindDrainsFrameAndInvalidatesOldKey() {
    auto oldController = createController();
    auto candidate = createController();
    NativeRenderAttachmentLease lease(oldController);
    const auto active = activate(lease);
    auto acquired = lease.tryAcquireFrame(active.attachment);
    assertTrue(acquired.guard.has_value(), "The blocking frame is acquired");

    std::promise<void> beginStarted;
    auto beginStartedFuture = beginStarted.get_future();
    std::promise<LeaseMutationResult<ControllerRebindTicket>> resultPromise;
    auto resultFuture = resultPromise.get_future();
    std::thread rebindThread([&] {
        beginStarted.set_value();
        resultPromise.set_value(
                lease.beginControllerRebindAndDrain(1));
    });

    beginStartedFuture.wait();
    assertTrue(resultFuture.wait_for(30ms) == std::future_status::timeout,
            "Controller rebind waits for the active frame guard");
    acquired.guard.reset();
    auto begin = resultFuture.get();
    rebindThread.join();
    assertTrue(begin.status == LeaseMutationStatus::Applied
                    && begin.value.has_value(),
            "Controller rebind starts after frame drain");
    assertTrue(lease.updateSurfaceDimensions(
                       active.attachment, 960, 540)
                    == LeaseMutationStatus::DeferredRebind,
            "A resize during rebind updates logical state without reviving the old key");
    const auto resized = lease.logicalSurfaceState();
    assertTrue(resized.active && resized.width == 960 && resized.height == 540,
            "Replacement commit can snapshot the freshest drained surface dimensions");

    std::shared_ptr<NativeCellularityController> retired;
    auto committed = lease.commitControllerRebind(
            std::move(*begin.value), candidate, retired);
    assertTrue(committed.status == LeaseMutationStatus::Applied
                    && committed.value.has_value()
                    && committed.value->controllerGeneration == 2,
            "Commit publishes exactly one new controller generation");
    assertTrue(retired == oldController,
            "Commit returns the old controller for out-of-lock retirement");
    assertTrue(lease.acquireControllerForColdCall() == candidate,
            "Cold calls resolve the newly published controller");

    auto stale = lease.tryAcquireFrame(active.attachment);
    assertTrue(stale.status == FrameAcquireStatus::StaleKey
                    && !stale.guard.has_value(),
            "A pre-rebind frame key cannot invoke the new controller");
    auto refreshed = lease.snapshotAttachment(active.worker);
    assertTrue(refreshed.status == AttachmentLookupStatus::Ready
                    && refreshed.key.has_value()
                    && refreshed.key->controllerGeneration == 2,
            "One worker refresh obtains the new controller generation");
    auto current = lease.tryAcquireFrame(*refreshed.key);
    assertTrue(current.status == FrameAcquireStatus::Acquired
                    && current.guard->controllerShared() == candidate,
            "The refreshed key invokes only the candidate controller");
}

void testAbortPreservesControllerButNotInputEpoch() {
    auto controller = createController();
    NativeRenderAttachmentLease lease(controller);
    const auto active = activate(lease);
    const auto before = lease.generationSnapshot();
    assertTrue(lease.markGlesResourcesLoaded(before.controllerGeneration)
                    == LeaseMutationStatus::Applied,
            "The surviving controller starts with current GLES resources");

    auto begin = lease.beginControllerRebindAndDrain(
            before.controllerGeneration);
    assertTrue(begin.value.has_value(), "Abort test starts a rebind");
    assertTrue(lease.abortControllerRebind(std::move(*begin.value))
                    == LeaseMutationStatus::Applied,
            "The exact rebind ticket aborts successfully");

    const auto after = lease.generationSnapshot();
    assertTrue(after.controllerGeneration == before.controllerGeneration
                    && after.inputEpoch != before.inputEpoch
                    && after.glesResourcesControllerGeneration == 0,
            "Abort keeps the controller, advances input, and forces lazy GLES reload");
    assertTrue(lease.acquireControllerForColdCall() == controller,
            "Abort preserves the old controller");
    assertTrue(lease.tryAcquireInput(active.attachment.surfaceToken,
                            before.inputEpoch)
                            .status
                    == InputBatchAdmissionStatus::Resyncing,
            "Input remains closed for pointer resynchronization after abort");
    assertTrue(lease.requestInputResync(active.attachment.surfaceToken)
                    == LeaseMutationStatus::AlreadyAuthorized,
            "An explicit resync observes the rebind-owned resync state");
    assertTrue(lease.completeInputResync(
                       active.attachment.surfaceToken, true)
                    == LeaseMutationStatus::Applied,
            "Observed pointer drain reopens the new input epoch");
    const auto completed = lease.generationSnapshot();
    assertTrue(completed.inputEpoch == after.inputEpoch,
            "A rebind-owned input epoch is not advanced twice by explicit resync");
    auto input = lease.tryAcquireInput(
            active.attachment.surfaceToken, after.inputEpoch);
    assertTrue(input.status == InputBatchAdmissionStatus::Accepted
                    && input.guard.has_value()
                    && &input.guard->controller() == controller.get(),
            "The post-abort input epoch reaches the preserved controller");
    assertTrue(lease.requestInputResync(
                       active.attachment.surfaceToken + 1)
                    == LeaseMutationStatus::StaleSurface,
            "A stale surface token cannot close current input admission");
}

void testPlacementGenomeRevisionSurvivesControllerRebind() {
    auto original = createController();
    NativeRenderAttachmentLease lease(original);
    const auto firstGenome =
            std::make_shared<const cellularity::GenomeData>();
    const auto first = lease.publishPlacementGenome(firstGenome);
    assertTrue(first.status == LeaseMutationStatus::Applied
                    && first.value.has_value() && *first.value != 0,
            "Placement genome publication receives a nonzero revision");
    assertTrue(lease.acquirePlacementGenome(*first.value) == firstGenome,
            "The exact placement revision resolves its immutable genome");

    const auto secondGenome =
            std::make_shared<const cellularity::GenomeData>();
    const auto second = lease.publishPlacementGenome(secondGenome);
    assertTrue(second.status == LeaseMutationStatus::Applied
                    && second.value.has_value()
                    && *second.value != *first.value,
            "A new placement genome advances the revision");
    assertTrue(lease.acquirePlacementGenome(*first.value) == nullptr
                    && lease.acquirePlacementGenome(*second.value)
                            == secondGenome,
            "A stale placement revision never aliases the current genome");

    auto begin = lease.beginControllerRebindAndDrain(1);
    assertTrue(begin.value.has_value(),
            "Placement retention test starts controller rebind");
    auto committed = lease.commitControllerRebind(
            std::move(*begin.value), createController());
    assertTrue(committed.status == LeaseMutationStatus::Applied
                    && lease.acquirePlacementGenome(*second.value)
                            == secondGenome,
            "Controller replacement preserves the attachment-owned placement genome");
}

void testUnconsumedAndCrossLeaseTicketsFailOpenSafely() {
    auto original = createController();
    NativeRenderAttachmentLease source(original);
    const auto active = activate(source, 29, 31, 37);

    {
        auto abandoned = source.beginControllerRebindAndDrain(1);
        assertTrue(abandoned.value.has_value(),
                "The abandoned-ticket test starts a rebind");
    }
    assertTrue(source.tryAcquireFrame(active.attachment).status
                    == FrameAcquireStatus::Acquired,
            "Destroying an unconsumed ticket restores the original attachment");

    auto cross = source.beginControllerRebindAndDrain(1);
    assertTrue(cross.value.has_value(),
            "The cross-lease test starts a rebind");
    NativeRenderAttachmentLease other(createController());
    const auto rejected = other.commitControllerRebind(
            std::move(*cross.value), createController());
    assertTrue(rejected.status == LeaseMutationStatus::InvalidArgument,
            "A ticket cannot publish through another attachment lease");
    assertTrue(source.acquireControllerForColdCall() == original
                    && source.tryAcquireFrame(active.attachment).status
                            == FrameAcquireStatus::Acquired,
            "Cross-lease rejection restores the source without changing its controller");
}

void testWorkerCloseIsIndependentFromInputAndAttachmentClose() {
    NativeRenderAttachmentLease lease(createController());
    const auto active = activate(lease);
    const auto epoch = lease.generationSnapshot().inputEpoch;

    assertTrue(lease.markWorkerClosing(active.worker)
                    == LeaseMutationStatus::Applied,
            "The matching worker enters its independent closing state");
    auto input = lease.tryAcquireInput(
            active.attachment.surfaceToken, epoch);
    assertTrue(input.status == InputBatchAdmissionStatus::Accepted,
            "Worker teardown does not close controller input admission");
    input.guard.reset();
    assertTrue(lease.authorizeWorker(31, 37).status
                    == LeaseMutationStatus::WorkerClosing,
            "A replacement waits until matching worker cleanup finishes");
    assertTrue(lease.finishWorkerClose(active.worker)
                    == LeaseMutationStatus::Applied,
            "Matching cleanup releases the worker slot");
    assertTrue(lease.finishWorkerClose(active.worker)
                    == LeaseMutationStatus::StaleWorker,
            "Duplicate old cleanup cannot mutate the released slot");

    auto replacement = lease.authorizeWorker(31, 37);
    assertTrue(replacement.status == LeaseMutationStatus::Applied
                    && replacement.value->bindingGeneration
                            != active.worker.bindingGeneration,
            "A replacement gets a new monotonic worker generation");
    assertTrue(lease.finishWorkerClose(active.worker)
                    == LeaseMutationStatus::StaleWorker,
            "Late old cleanup cannot remove the replacement worker");
}

void testSurfaceReattachNeverRevivesCachedKey() {
    NativeRenderAttachmentLease lease(createController());
    const auto first = activate(lease, 41, 43, 47);
    assertTrue(lease.deactivateSurface(first.attachment)
                    == LeaseMutationStatus::Applied,
            "Surface loss deactivates the exact attachment");
    auto second = lease.activateSurface(first.worker, 47, 800, 600);
    assertTrue(second.status == LeaseMutationStatus::Applied
                    && second.value.has_value()
                    && second.value->backendBindingGeneration
                            != first.attachment.backendBindingGeneration,
            "Equal platform generation reattach gets a new backend generation");
    assertTrue(lease.updateSurfaceDimensions(
                       first.attachment, 320, 240)
                    == LeaseMutationStatus::StaleSurface,
            "A pre-loss key cannot resize the replacement attachment");
    const auto dimensions = lease.logicalSurfaceState();
    assertTrue(dimensions.width == 800 && dimensions.height == 600,
            "A rejected stale resize leaves current logical dimensions intact");
    assertTrue(lease.tryAcquireFrame(first.attachment).status
                    == FrameAcquireStatus::StaleKey,
            "A cached pre-loss frame key cannot revive after reattach");
}

void testWakeRegistrationIsWeakAndNotifiedOutsideRebind() {
    NativeRenderAttachmentLease lease(createController());
    const auto active = activate(lease, 51, 53, 59);
    auto wake = std::make_shared<CountingWake>();
    auto registration = lease.registerRebindWake(active.worker, wake);
    assertTrue(registration.status == LeaseMutationStatus::Applied
                    && registration.value.has_value(),
            "The matching worker registers a weak rebind wake");

    auto begin = lease.beginControllerRebindAndDrain(1);
    assertTrue(begin.value.has_value(), "Wake test begins a rebind");
    auto committed = lease.commitControllerRebind(
            std::move(*begin.value), createController());
    assertTrue(committed.status == LeaseMutationStatus::Applied
                    && wake->notifications.load(std::memory_order_relaxed) == 1,
            "Successful publication emits one rebind-ready wake");

    registration.value.reset();
    auto secondBegin = lease.beginControllerRebindAndDrain(2);
    assertTrue(secondBegin.value.has_value(), "A second rebind begins");
    assertTrue(lease.abortControllerRebind(std::move(*secondBegin.value))
                            == LeaseMutationStatus::Applied
                    && wake->notifications.load(std::memory_order_relaxed) == 1,
            "Destroying registration removes the wake without owning its control block");
}

void testAttachmentCloseIsTerminalAndTakesControllerOnce() {
    auto controller = createController();
    NativeRenderAttachmentLease lease(controller);
    const auto active = activate(lease, 61, 67, 71);
    auto frame = lease.tryAcquireFrame(active.attachment);

    std::promise<LeaseMutationStatus> closePromise;
    auto closeFuture = closePromise.get_future();
    std::thread closeThread([&] {
        closePromise.set_value(lease.beginAttachmentCloseAndDrain());
    });
    assertTrue(closeFuture.wait_for(30ms) == std::future_status::timeout,
            "Terminal close waits for active frames");
    std::promise<LeaseMutationStatus> duplicateClosePromise;
    auto duplicateCloseFuture = duplicateClosePromise.get_future();
    std::thread duplicateCloseThread([&] {
        duplicateClosePromise.set_value(
                lease.beginAttachmentCloseAndDrain());
    });
    assertTrue(duplicateCloseFuture.wait_for(30ms)
                    == std::future_status::timeout,
            "A duplicate terminal close observes the same frame-drain barrier");
    assertTrue(lease.tryAcquireFrame(active.attachment).status
                    == FrameAcquireStatus::AttachmentClosing,
            "Terminal close immediately rejects new frames");
    frame.guard.reset();
    assertTrue(closeFuture.get() == LeaseMutationStatus::Applied,
            "Terminal close completes after frame drain");
    assertTrue(duplicateCloseFuture.get()
                    == LeaseMutationStatus::AttachmentClosing,
            "A duplicate close remains terminal after the shared drain");
    closeThread.join();
    duplicateCloseThread.join();

    auto taken = lease.takeControllerForAttachmentClose();
    assertTrue(taken.status == LeaseMutationStatus::Applied
                    && taken.value.has_value()
                    && *taken.value == controller,
            "Terminal close transfers the controller exactly once");
    auto duplicate = lease.takeControllerForAttachmentClose();
    assertTrue(duplicate.status == LeaseMutationStatus::NotAuthorized
                    && !duplicate.value.has_value(),
            "A duplicate close take never exposes a raw or repeated controller");
    assertTrue(lease.authorizeWorker(73, 79).status
                    == LeaseMutationStatus::AttachmentClosing,
            "A terminal attachment cannot be reopened by another worker");
}

void testAttachmentCloseWakesDeferredWorkerTeardown() {
    NativeRenderAttachmentLease lease(createController());
    const auto active = activate(lease, 101, 103, 107);
    auto wake = std::make_shared<CountingWake>();
    auto registration = lease.registerRebindWake(active.worker, wake);
    assertTrue(registration.status == LeaseMutationStatus::Applied
                    && registration.value.has_value(),
            "Worker teardown wake must register before rebind");
    auto begin = lease.beginControllerRebindAndDrain(1);
    assertTrue(begin.status == LeaseMutationStatus::Applied
                    && begin.value.has_value(),
            "Worker teardown wake test must enter rebind");

    assertTrue(lease.beginAttachmentCloseAndDrain()
                    == LeaseMutationStatus::Applied,
            "Attachment close must win over the pending rebind");
    assertTrue(wake->notifications.load(std::memory_order_relaxed) == 1,
            "Attachment close must wake a worker waiting to remove its binding");
    const auto commit = lease.commitControllerRebind(
            std::move(*begin.value), createController());
    assertTrue(commit.status == LeaseMutationStatus::AttachmentClosing,
            "A close wake must not let the stale rebind commit");
}

void testCloseWinsAgainstPendingRebind() {
    NativeRenderAttachmentLease lease(createController());
    const auto active = activate(lease, 83, 89, 97);
    auto frame = lease.tryAcquireFrame(active.attachment);

    std::promise<LeaseMutationResult<ControllerRebindTicket>> beginPromise;
    auto beginFuture = beginPromise.get_future();
    std::thread rebindThread([&] {
        beginPromise.set_value(lease.beginControllerRebindAndDrain(1));
    });
    std::this_thread::sleep_for(10ms);
    std::promise<LeaseMutationStatus> closePromise;
    auto closeFuture = closePromise.get_future();
    std::thread closeThread([&] {
        closePromise.set_value(lease.beginAttachmentCloseAndDrain());
    });

    frame.guard.reset();
    auto begin = beginFuture.get();
    const auto close = closeFuture.get();
    rebindThread.join();
    closeThread.join();
    const bool closeCompleted = close == LeaseMutationStatus::Applied
            || close == LeaseMutationStatus::AttachmentClosing;
    if (begin.value.has_value()) {
        const auto commit = lease.commitControllerRebind(
                std::move(*begin.value), createController());
        assertTrue(commit.status == LeaseMutationStatus::AttachmentClosing,
                "A rebind ticket obtained first cannot commit after terminal close");
    } else {
        assertTrue(begin.status == LeaseMutationStatus::AttachmentClosing,
                "A close obtained first prevents creation of a rebind ticket");
    }
    assertTrue(closeCompleted
                    && lease.acquireControllerForColdCall() == nullptr,
            "Neither close/reload interleaving reopens the terminal lease");
}

} // namespace

int main() {
    testGlesResourcesRequireCurrentSurfaceAndLiveAdapter();
    testGlesVulkanGlesTransitionsInvalidateResources();
    testRenderEndpointHandleOwnsDurableSharedLeases();
    testNativeViewportInitializationAndSurfaceRecreation();
    testSteadyFramesUseCachedAttachmentKey();
    testRebindDrainsFrameAndInvalidatesOldKey();
    testAbortPreservesControllerButNotInputEpoch();
    testPlacementGenomeRevisionSurvivesControllerRebind();
    testUnconsumedAndCrossLeaseTicketsFailOpenSafely();
    testWorkerCloseIsIndependentFromInputAndAttachmentClose();
    testSurfaceReattachNeverRevivesCachedKey();
    testWakeRegistrationIsWeakAndNotifiedOutsideRebind();
    testAttachmentCloseIsTerminalAndTakesControllerOnce();
    testAttachmentCloseWakesDeferredWorkerTeardown();
    testCloseWinsAgainstPendingRebind();
    std::cout << "Native render attachment lease tests passed\n";
    return 0;
}
