#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "core/GenomePool.h"

class NativeCellularityController;

namespace cellularity::render {
class RenderSurfaceSessionBinding;
} // namespace cellularity::render

namespace cellularity::app {

struct NativeRenderAttachmentState;

enum class LeaseMutationStatus : std::uint8_t {
    Applied = 0,
    DeferredRebind = 1,
    StaleWorker = 2,
    StaleSurface = 3,
    AlreadyAuthorized = 4,
    WorkerBusy = 5,
    WorkerClosing = 6,
    NotAuthorized = 7,
    AttachmentClosing = 8,
    InvalidArgument = 9,
    GenerationExhausted = 10,
};

template <typename T>
struct LeaseMutationResult final {
    LeaseMutationStatus status = LeaseMutationStatus::InvalidArgument;
    std::optional<T> value;
};

enum class AttachmentLookupStatus : std::uint8_t {
    Ready = 0,
    Rebinding = 1,
    InactiveSurface = 2,
    StaleIdentity = 3,
    WorkerClosing = 4,
    AttachmentClosing = 5,
};

enum class FrameAcquireStatus : std::uint8_t {
    Acquired = 0,
    Rebinding = 1,
    InactiveSurface = 2,
    StaleKey = 3,
    WorkerClosing = 4,
    AttachmentClosing = 5,
};

enum class InputBatchAdmissionStatus : std::uint8_t {
    Accepted = 0,
    Resyncing = 1,
    StaleEpoch = 2,
    InactiveSurface = 3,
    AttachmentClosing = 4,
};

struct WorkerBindingKey final {
    std::uint64_t surfaceToken = 0;
    std::uint64_t workerIdentity = 0;
    std::uint64_t bindingGeneration = 0;

    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const WorkerBindingKey& left,
            const WorkerBindingKey& right) noexcept {
        return left.surfaceToken == right.surfaceToken
                && left.workerIdentity == right.workerIdentity
                && left.bindingGeneration == right.bindingGeneration;
    }
};

struct AttachmentKey final {
    std::uint64_t surfaceToken = 0;
    std::uint64_t workerBindingGeneration = 0;
    std::uint64_t backendBindingGeneration = 0;
    std::uint64_t platformSurfaceGeneration = 0;
    std::uint64_t controllerGeneration = 0;

    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const AttachmentKey& left,
            const AttachmentKey& right) noexcept {
        return left.surfaceToken == right.surfaceToken
                && left.workerBindingGeneration
                == right.workerBindingGeneration
                && left.backendBindingGeneration
                == right.backendBindingGeneration
                && left.platformSurfaceGeneration
                == right.platformSurfaceGeneration
                && left.controllerGeneration == right.controllerGeneration;
    }
};

struct AttachmentGeneration final {
    std::uint64_t controllerGeneration = 0;
    std::uint64_t inputEpoch = 0;
    std::uint64_t glesResourcesControllerGeneration = 0;
};

struct LogicalSurfaceState final {
    bool active = false;
    std::uint64_t surfaceToken = 0;
    std::uint64_t platformSurfaceGeneration = 0;
    int width = 0;
    int height = 0;
};

struct NativeRenderLeaseCounters final {
    std::uint64_t attachmentSnapshotCount = 0;
    std::uint64_t frameAcquireAttemptCount = 0;
    std::uint64_t frameAcquireSuccessCount = 0;
    std::uint64_t inputAcquireAttemptCount = 0;
    std::uint64_t inputAcquireSuccessCount = 0;
    std::uint64_t controllerRebindCount = 0;
};

class RebindWakeControl {
public:
    virtual ~RebindWakeControl() = default;
    virtual void notifyRebindReady() noexcept = 0;
};

class FrameGuard final {
public:
    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;
    FrameGuard(FrameGuard&& other) noexcept;
    FrameGuard& operator=(FrameGuard&& other) noexcept;
    ~FrameGuard();

    [[nodiscard]] NativeCellularityController& controller() const;
    [[nodiscard]] std::shared_ptr<NativeCellularityController>
    controllerShared() const noexcept;
    [[nodiscard]] const AttachmentKey& key() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class NativeRenderAttachmentLease;
    FrameGuard(std::shared_ptr<NativeRenderAttachmentState> state,
            std::shared_ptr<NativeCellularityController> controller,
            AttachmentKey key) noexcept;
    void release() noexcept;

    std::shared_ptr<NativeRenderAttachmentState> mState;
    std::shared_ptr<NativeCellularityController> mController;
    AttachmentKey mKey{};
};

struct FrameAcquireResult final {
    FrameAcquireStatus status = FrameAcquireStatus::StaleKey;
    std::optional<FrameGuard> guard;
};

struct AttachmentLookupResult final {
    AttachmentLookupStatus status = AttachmentLookupStatus::StaleIdentity;
    std::optional<AttachmentKey> key;
};

class InputBatchGuard final {
public:
    InputBatchGuard(const InputBatchGuard&) = delete;
    InputBatchGuard& operator=(const InputBatchGuard&) = delete;
    InputBatchGuard(InputBatchGuard&& other) noexcept;
    InputBatchGuard& operator=(InputBatchGuard&& other) noexcept;
    ~InputBatchGuard();

    [[nodiscard]] NativeCellularityController& controller() const;
    [[nodiscard]] std::shared_ptr<NativeCellularityController>
    controllerShared() const noexcept;
    [[nodiscard]] std::uint64_t inputEpoch() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class NativeRenderAttachmentLease;
    InputBatchGuard(std::shared_ptr<NativeRenderAttachmentState> state,
            std::shared_ptr<NativeCellularityController> controller,
            std::uint64_t inputEpoch) noexcept;
    void release() noexcept;

    std::shared_ptr<NativeRenderAttachmentState> mState;
    std::shared_ptr<NativeCellularityController> mController;
    std::uint64_t mInputEpoch = 0;
};

struct InputBatchAdmission final {
    InputBatchAdmissionStatus status =
            InputBatchAdmissionStatus::InactiveSurface;
    std::uint64_t currentInputEpoch = 0;
    std::optional<InputBatchGuard> guard;
};

class SurfaceOperationGuard final {
public:
    SurfaceOperationGuard(const SurfaceOperationGuard&) = delete;
    SurfaceOperationGuard& operator=(const SurfaceOperationGuard&) = delete;
    SurfaceOperationGuard(SurfaceOperationGuard&& other) noexcept;
    SurfaceOperationGuard& operator=(SurfaceOperationGuard&& other) noexcept;
    ~SurfaceOperationGuard();

    [[nodiscard]] NativeCellularityController& controller() const;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class NativeRenderAttachmentLease;
    SurfaceOperationGuard(std::shared_ptr<NativeRenderAttachmentState> state,
            std::shared_ptr<NativeCellularityController> controller) noexcept;
    void release() noexcept;

    std::shared_ptr<NativeRenderAttachmentState> mState;
    std::shared_ptr<NativeCellularityController> mController;
};

struct SurfaceOperationAdmission final {
    LeaseMutationStatus status = LeaseMutationStatus::InvalidArgument;
    std::optional<SurfaceOperationGuard> guard;
};

class ControllerRebindTicket final {
public:
    ControllerRebindTicket(const ControllerRebindTicket&) = delete;
    ControllerRebindTicket& operator=(const ControllerRebindTicket&) = delete;
    ControllerRebindTicket(ControllerRebindTicket&& other) noexcept;
    ControllerRebindTicket& operator=(ControllerRebindTicket&&) = delete;
    ~ControllerRebindTicket();

    [[nodiscard]] bool valid() const noexcept;

private:
    friend class NativeRenderAttachmentLease;
    ControllerRebindTicket(
            std::weak_ptr<NativeRenderAttachmentState> owner,
            std::uint64_t transaction,
            std::uint64_t expectedControllerGeneration) noexcept;
    void consume() noexcept;

    std::weak_ptr<NativeRenderAttachmentState> mOwner;
    std::uint64_t mTransaction = 0;
    std::uint64_t mExpectedControllerGeneration = 0;
};

class RebindWakeRegistration final {
public:
    RebindWakeRegistration(const RebindWakeRegistration&) = delete;
    RebindWakeRegistration& operator=(const RebindWakeRegistration&) = delete;
    RebindWakeRegistration(RebindWakeRegistration&& other) noexcept;
    RebindWakeRegistration& operator=(RebindWakeRegistration&& other) noexcept;
    ~RebindWakeRegistration();

    [[nodiscard]] bool valid() const noexcept;

private:
    friend class NativeRenderAttachmentLease;
    RebindWakeRegistration(std::weak_ptr<NativeRenderAttachmentState> owner,
            WorkerBindingKey worker,
            std::uint64_t registrationGeneration) noexcept;
    void release() noexcept;

    std::weak_ptr<NativeRenderAttachmentState> mOwner;
    WorkerBindingKey mWorker{};
    std::uint64_t mRegistrationGeneration = 0;
};

class NativeRenderAttachmentLease final {
public:
    explicit NativeRenderAttachmentLease(
            std::shared_ptr<NativeCellularityController> controller);
    ~NativeRenderAttachmentLease();

    NativeRenderAttachmentLease(const NativeRenderAttachmentLease&) = delete;
    NativeRenderAttachmentLease& operator=(
            const NativeRenderAttachmentLease&) = delete;

    [[nodiscard]] AttachmentLookupResult snapshotAttachment(
            std::uint64_t expectedSurfaceToken) const;
    [[nodiscard]] AttachmentLookupResult snapshotAttachment(
            WorkerBindingKey expectedWorker) const;
    [[nodiscard]] FrameAcquireResult tryAcquireFrame(
            AttachmentKey expectedAttachment);
    [[nodiscard]] FrameAcquireResult tryAcquireFrame(
            std::uint64_t expectedSurfaceToken);
    [[nodiscard]] SurfaceOperationAdmission tryAcquireSurfaceOperation(
            WorkerBindingKey expectedWorker);
    [[nodiscard]] InputBatchAdmission tryAcquireInput(
            std::uint64_t expectedSurfaceToken,
            std::uint64_t expectedInputEpoch);
    [[nodiscard]] LeaseMutationStatus requestInputResync(
            std::uint64_t expectedSurfaceToken);
    [[nodiscard]] LeaseMutationStatus completeInputResync(
            std::uint64_t expectedSurfaceToken,
            bool activePointersDrained);
    [[nodiscard]] LeaseMutationResult<std::uint64_t> publishPlacementGenome(
            std::shared_ptr<const cellularity::GenomeData> genome);
    [[nodiscard]] std::shared_ptr<const cellularity::GenomeData>
    acquirePlacementGenome(std::uint64_t expectedRevision) const;

    [[nodiscard]] LeaseMutationResult<ControllerRebindTicket>
    beginControllerRebindAndDrain(
            std::uint64_t expectedControllerGeneration);
    [[nodiscard]] LeaseMutationResult<AttachmentGeneration>
    commitControllerRebind(ControllerRebindTicket ticket,
            std::shared_ptr<NativeCellularityController> controller);
    [[nodiscard]] LeaseMutationResult<AttachmentGeneration>
    commitControllerRebind(ControllerRebindTicket ticket,
            std::shared_ptr<NativeCellularityController> controller,
            std::shared_ptr<NativeCellularityController>& retiredController);
    [[nodiscard]] LeaseMutationStatus abortControllerRebind(
            ControllerRebindTicket ticket);

    [[nodiscard]] LeaseMutationStatus beginAttachmentCloseAndDrain();
    [[nodiscard]] LeaseMutationResult<
            std::shared_ptr<NativeCellularityController>>
    takeControllerForAttachmentClose();

    [[nodiscard]] LeaseMutationResult<WorkerBindingKey> authorizeWorker(
            std::uint64_t surfaceToken,
            std::uint64_t workerIdentity);
    [[nodiscard]] LeaseMutationStatus installBackendBinding(
            WorkerBindingKey expectedWorker,
            std::shared_ptr<const cellularity::render::
                            RenderSurfaceSessionBinding>
                    binding);
    [[nodiscard]] LeaseMutationStatus removeBackendBinding(
            WorkerBindingKey expectedWorker);
    [[nodiscard]] LeaseMutationResult<RebindWakeRegistration>
    registerRebindWake(WorkerBindingKey expectedWorker,
            std::weak_ptr<RebindWakeControl> wakeControl);
    [[nodiscard]] LeaseMutationResult<AttachmentKey> activateSurface(
            WorkerBindingKey expectedWorker,
            std::uint64_t platformGeneration,
            int width,
            int height);
    [[nodiscard]] LeaseMutationStatus updateSurfaceDimensions(
            AttachmentKey expected,
            int width,
            int height);
    [[nodiscard]] LeaseMutationStatus updateSurfaceDimensions(
            std::uint64_t expectedSurfaceToken,
            int width,
            int height);
    [[nodiscard]] LeaseMutationStatus deactivateSurface(
            AttachmentKey expected);
    [[nodiscard]] LeaseMutationStatus deactivateSurface(
            WorkerBindingKey expectedWorker);
    [[nodiscard]] LeaseMutationStatus deactivateSurface(
            std::uint64_t expectedSurfaceToken);
    [[nodiscard]] LeaseMutationStatus markWorkerClosing(
            WorkerBindingKey expectedWorker);
    [[nodiscard]] LeaseMutationStatus finishWorkerClose(
            WorkerBindingKey expectedWorker);

    [[nodiscard]] std::shared_ptr<NativeCellularityController>
    acquireControllerForColdCall() const;
    [[nodiscard]] AttachmentGeneration generationSnapshot() const;
    [[nodiscard]] LogicalSurfaceState logicalSurfaceState() const;
    [[nodiscard]] std::uint64_t activeInputEpoch(
            std::uint64_t expectedSurfaceToken) const;
    [[nodiscard]] std::optional<std::uint64_t>
    vulkanProfilingWorkerIdentity() const;
    [[nodiscard]] bool hasGlesResourcesForFrame(const FrameGuard& frame) const;
    [[nodiscard]] LeaseMutationStatus markGlesResourcesLoaded(
            std::uint64_t controllerGeneration);
    [[nodiscard]] NativeRenderLeaseCounters debugCounters() const;

private:
    std::shared_ptr<NativeRenderAttachmentState> mState;
};

} // namespace cellularity::app
