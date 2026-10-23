#include "app/render/NativeRenderAttachmentLease.h"

#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "app/NativeCellularityController.h"
#include "app/render/RenderSurfaceSessionBinding.h"
#include "render/backend/RenderSurfaceSession.h"

namespace cellularity::app {

namespace {

enum class AttachmentLifecycle : std::uint8_t {
    Open,
    Rebinding,
    Closing,
};

enum class WorkerLifecycle : std::uint8_t {
    Unbound,
    Authorized,
    Closing,
};

enum class InputLifecycle : std::uint8_t {
    Inactive,
    Accepting,
    Resyncing,
    Closing,
};

bool incrementGeneration(std::uint64_t& generation) noexcept {
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    ++generation;
    return generation != 0;
}

} // namespace

struct NativeRenderAttachmentState final {
    mutable std::mutex mutex;
    std::condition_variable drained;
    AttachmentLifecycle lifecycle = AttachmentLifecycle::Open;
    WorkerLifecycle workerLifecycle = WorkerLifecycle::Unbound;
    InputLifecycle inputLifecycle = InputLifecycle::Inactive;
    std::shared_ptr<NativeCellularityController> controller;
    std::shared_ptr<const cellularity::render::RenderSurfaceSessionBinding>
            backendBinding;
    WorkerBindingKey worker{};
    AttachmentKey attachment{};
    AttachmentKey glesResourcesAttachment{};
    LogicalSurfaceState logicalSurface{};
    std::weak_ptr<RebindWakeControl> wakeControl;
    std::shared_ptr<const cellularity::GenomeData> placementGenome;
    std::uint64_t wakeRegistrationGeneration = 0;
    std::uint64_t controllerGeneration = 1;
    std::uint64_t inputEpoch = 1;
    std::uint64_t workerBindingGeneration = 0;
    std::uint64_t backendBindingGeneration = 0;
    std::uint64_t rebindTransaction = 0;
    std::uint64_t glesResourcesControllerGeneration = 0;
    std::uint64_t placementGenomeRevision = 0;
    std::uint64_t activeFrameCount = 0;
    std::uint64_t activeInputCount = 0;
    std::uint64_t activeSurfaceOperationCount = 0;
    bool controllerTaken = false;
    bool surfaceEpochIssued = false;
    bool rebindAdvancedInputEpoch = false;
    NativeRenderLeaseCounters counters{};
};

namespace {

bool workerMatches(const NativeRenderAttachmentState& state,
        const WorkerBindingKey& expected) noexcept {
    return expected.valid() && state.worker == expected;
}

bool attachmentMatches(const NativeRenderAttachmentState& state,
        const AttachmentKey& expected) noexcept {
    return expected.valid() && state.attachment == expected;
}

void invalidateGlesResources(NativeRenderAttachmentState& state) noexcept {
    state.glesResourcesControllerGeneration = 0;
    state.glesResourcesAttachment = {};
}

AttachmentLookupStatus lookupUnavailableStatus(
        const NativeRenderAttachmentState& state) noexcept {
    if (state.lifecycle == AttachmentLifecycle::Closing) {
        return AttachmentLookupStatus::AttachmentClosing;
    }
    if (state.lifecycle == AttachmentLifecycle::Rebinding) {
        return AttachmentLookupStatus::Rebinding;
    }
    if (state.workerLifecycle == WorkerLifecycle::Closing) {
        return AttachmentLookupStatus::WorkerClosing;
    }
    if (!state.logicalSurface.active) {
        return AttachmentLookupStatus::InactiveSurface;
    }
    return AttachmentLookupStatus::StaleIdentity;
}

FrameAcquireStatus frameUnavailableStatus(
        const NativeRenderAttachmentState& state) noexcept {
    if (state.lifecycle == AttachmentLifecycle::Closing) {
        return FrameAcquireStatus::AttachmentClosing;
    }
    if (state.lifecycle == AttachmentLifecycle::Rebinding) {
        return FrameAcquireStatus::Rebinding;
    }
    if (state.workerLifecycle == WorkerLifecycle::Closing) {
        return FrameAcquireStatus::WorkerClosing;
    }
    if (!state.logicalSurface.active) {
        return FrameAcquireStatus::InactiveSurface;
    }
    return FrameAcquireStatus::StaleKey;
}

std::shared_ptr<RebindWakeControl> takeWakeControlLocked(
        NativeRenderAttachmentState& state) {
    return state.wakeControl.lock();
}

void notifyDrained(NativeRenderAttachmentState& state) noexcept {
    state.drained.notify_all();
}

void unregisterWake(const std::shared_ptr<NativeRenderAttachmentState>& state,
        WorkerBindingKey worker,
        std::uint64_t registrationGeneration) noexcept {
    if (state == nullptr || registrationGeneration == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->wakeRegistrationGeneration == registrationGeneration
            && state->worker == worker) {
        state->wakeControl.reset();
    }
}

} // namespace

bool WorkerBindingKey::valid() const noexcept {
    return surfaceToken != 0 && workerIdentity != 0 && bindingGeneration != 0;
}

bool AttachmentKey::valid() const noexcept {
    return surfaceToken != 0 && workerBindingGeneration != 0
            && backendBindingGeneration != 0
            && platformSurfaceGeneration != 0 && controllerGeneration != 0;
}

FrameGuard::FrameGuard(std::shared_ptr<NativeRenderAttachmentState> state,
        std::shared_ptr<NativeCellularityController> controller,
        AttachmentKey key) noexcept
        : mState(std::move(state)),
          mController(std::move(controller)),
          mKey(key) {
}

FrameGuard::FrameGuard(FrameGuard&& other) noexcept
        : mState(std::move(other.mState)),
          mController(std::move(other.mController)),
          mKey(other.mKey) {
    other.mKey = {};
}

FrameGuard& FrameGuard::operator=(FrameGuard&& other) noexcept {
    if (this != &other) {
        release();
        mState = std::move(other.mState);
        mController = std::move(other.mController);
        mKey = other.mKey;
        other.mKey = {};
    }
    return *this;
}

FrameGuard::~FrameGuard() {
    release();
}

NativeCellularityController& FrameGuard::controller() const {
    if (mController == nullptr) {
        throw std::logic_error("FrameGuard has no controller");
    }
    return *mController;
}

std::shared_ptr<NativeCellularityController>
FrameGuard::controllerShared() const noexcept {
    return mController;
}

const AttachmentKey& FrameGuard::key() const noexcept {
    return mKey;
}

bool FrameGuard::valid() const noexcept {
    return mState != nullptr && mController != nullptr && mKey.valid();
}

void FrameGuard::release() noexcept {
    if (mState == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (mState->activeFrameCount != 0) {
            --mState->activeFrameCount;
        }
        notifyDrained(*mState);
    }
    mController.reset();
    mState.reset();
    mKey = {};
}

InputBatchGuard::InputBatchGuard(
        std::shared_ptr<NativeRenderAttachmentState> state,
        std::shared_ptr<NativeCellularityController> controller,
        std::uint64_t inputEpoch) noexcept
        : mState(std::move(state)),
          mController(std::move(controller)),
          mInputEpoch(inputEpoch) {
}

InputBatchGuard::InputBatchGuard(InputBatchGuard&& other) noexcept
        : mState(std::move(other.mState)),
          mController(std::move(other.mController)),
          mInputEpoch(other.mInputEpoch) {
    other.mInputEpoch = 0;
}

InputBatchGuard& InputBatchGuard::operator=(InputBatchGuard&& other) noexcept {
    if (this != &other) {
        release();
        mState = std::move(other.mState);
        mController = std::move(other.mController);
        mInputEpoch = other.mInputEpoch;
        other.mInputEpoch = 0;
    }
    return *this;
}

InputBatchGuard::~InputBatchGuard() {
    release();
}

NativeCellularityController& InputBatchGuard::controller() const {
    if (mController == nullptr) {
        throw std::logic_error("InputBatchGuard has no controller");
    }
    return *mController;
}

std::shared_ptr<NativeCellularityController>
InputBatchGuard::controllerShared() const noexcept {
    return mController;
}

std::uint64_t InputBatchGuard::inputEpoch() const noexcept {
    return mInputEpoch;
}

bool InputBatchGuard::valid() const noexcept {
    return mState != nullptr && mController != nullptr && mInputEpoch != 0;
}

void InputBatchGuard::release() noexcept {
    if (mState == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (mState->activeInputCount != 0) {
            --mState->activeInputCount;
        }
        notifyDrained(*mState);
    }
    mController.reset();
    mState.reset();
    mInputEpoch = 0;
}

SurfaceOperationGuard::SurfaceOperationGuard(
        std::shared_ptr<NativeRenderAttachmentState> state,
        std::shared_ptr<NativeCellularityController> controller) noexcept
        : mState(std::move(state)),
          mController(std::move(controller)) {
}

SurfaceOperationGuard::SurfaceOperationGuard(
        SurfaceOperationGuard&& other) noexcept
        : mState(std::move(other.mState)),
          mController(std::move(other.mController)) {
}

SurfaceOperationGuard& SurfaceOperationGuard::operator=(
        SurfaceOperationGuard&& other) noexcept {
    if (this != &other) {
        release();
        mState = std::move(other.mState);
        mController = std::move(other.mController);
    }
    return *this;
}

SurfaceOperationGuard::~SurfaceOperationGuard() {
    release();
}

NativeCellularityController& SurfaceOperationGuard::controller() const {
    if (mController == nullptr) {
        throw std::logic_error("SurfaceOperationGuard has no controller");
    }
    return *mController;
}

bool SurfaceOperationGuard::valid() const noexcept {
    return mState != nullptr && mController != nullptr;
}

void SurfaceOperationGuard::release() noexcept {
    if (mState == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (mState->activeSurfaceOperationCount != 0) {
            --mState->activeSurfaceOperationCount;
        }
        notifyDrained(*mState);
    }
    mController.reset();
    mState.reset();
}

ControllerRebindTicket::ControllerRebindTicket(
        std::weak_ptr<NativeRenderAttachmentState> owner,
        std::uint64_t transaction,
        std::uint64_t expectedControllerGeneration) noexcept
        : mOwner(std::move(owner)),
          mTransaction(transaction),
          mExpectedControllerGeneration(expectedControllerGeneration) {
}

ControllerRebindTicket::ControllerRebindTicket(
        ControllerRebindTicket&& other) noexcept
        : mOwner(std::move(other.mOwner)),
          mTransaction(other.mTransaction),
          mExpectedControllerGeneration(other.mExpectedControllerGeneration) {
    other.consume();
}

ControllerRebindTicket::~ControllerRebindTicket() {
    auto owner = mOwner.lock();
    std::shared_ptr<RebindWakeControl> wake;
    if (owner != nullptr && mTransaction != 0
            && mExpectedControllerGeneration != 0) {
        {
            std::lock_guard<std::mutex> lock(owner->mutex);
            if (owner->rebindTransaction == mTransaction
                    && owner->controllerGeneration
                            == mExpectedControllerGeneration
                    && owner->lifecycle == AttachmentLifecycle::Rebinding) {
                owner->lifecycle = AttachmentLifecycle::Open;
                invalidateGlesResources(*owner);
                if (!owner->logicalSurface.active) {
                    owner->inputLifecycle = InputLifecycle::Inactive;
                }
                wake = takeWakeControlLocked(*owner);
            }
        }
        if (wake != nullptr) {
            wake->notifyRebindReady();
        }
    }
    consume();
}

bool ControllerRebindTicket::valid() const noexcept {
    return mTransaction != 0 && mExpectedControllerGeneration != 0
            && !mOwner.expired();
}

void ControllerRebindTicket::consume() noexcept {
    mOwner.reset();
    mTransaction = 0;
    mExpectedControllerGeneration = 0;
}

RebindWakeRegistration::RebindWakeRegistration(
        std::weak_ptr<NativeRenderAttachmentState> owner,
        WorkerBindingKey worker,
        std::uint64_t registrationGeneration) noexcept
        : mOwner(std::move(owner)),
          mWorker(worker),
          mRegistrationGeneration(registrationGeneration) {
}

RebindWakeRegistration::RebindWakeRegistration(
        RebindWakeRegistration&& other) noexcept
        : mOwner(std::move(other.mOwner)),
          mWorker(other.mWorker),
          mRegistrationGeneration(other.mRegistrationGeneration) {
    other.mWorker = {};
    other.mRegistrationGeneration = 0;
}

RebindWakeRegistration& RebindWakeRegistration::operator=(
        RebindWakeRegistration&& other) noexcept {
    if (this != &other) {
        release();
        mOwner = std::move(other.mOwner);
        mWorker = other.mWorker;
        mRegistrationGeneration = other.mRegistrationGeneration;
        other.mWorker = {};
        other.mRegistrationGeneration = 0;
    }
    return *this;
}

RebindWakeRegistration::~RebindWakeRegistration() {
    release();
}

bool RebindWakeRegistration::valid() const noexcept {
    return mRegistrationGeneration != 0 && mWorker.valid()
            && !mOwner.expired();
}

void RebindWakeRegistration::release() noexcept {
    unregisterWake(mOwner.lock(), mWorker, mRegistrationGeneration);
    mOwner.reset();
    mWorker = {};
    mRegistrationGeneration = 0;
}

NativeRenderAttachmentLease::NativeRenderAttachmentLease(
        std::shared_ptr<NativeCellularityController> controller)
        : mState(std::make_shared<NativeRenderAttachmentState>()) {
    if (controller == nullptr) {
        throw std::invalid_argument(
                "NativeRenderAttachmentLease requires a controller");
    }
    mState->controller = std::move(controller);
}

NativeRenderAttachmentLease::~NativeRenderAttachmentLease() = default;

AttachmentLookupResult NativeRenderAttachmentLease::snapshotAttachment(
        std::uint64_t expectedSurfaceToken) const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    ++mState->counters.attachmentSnapshotCount;
    const auto unavailable = lookupUnavailableStatus(*mState);
    if (unavailable != AttachmentLookupStatus::StaleIdentity) {
        return {unavailable, std::nullopt};
    }
    if (expectedSurfaceToken == 0
            || mState->attachment.surfaceToken != expectedSurfaceToken) {
        return {AttachmentLookupStatus::StaleIdentity, std::nullopt};
    }
    return {AttachmentLookupStatus::Ready, mState->attachment};
}

AttachmentLookupResult NativeRenderAttachmentLease::snapshotAttachment(
        WorkerBindingKey expectedWorker) const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    ++mState->counters.attachmentSnapshotCount;
    const auto unavailable = lookupUnavailableStatus(*mState);
    if (unavailable != AttachmentLookupStatus::StaleIdentity) {
        return {unavailable, std::nullopt};
    }
    if (!workerMatches(*mState, expectedWorker)) {
        return {AttachmentLookupStatus::StaleIdentity, std::nullopt};
    }
    return {AttachmentLookupStatus::Ready, mState->attachment};
}

FrameAcquireResult NativeRenderAttachmentLease::tryAcquireFrame(
        AttachmentKey expectedAttachment) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    ++mState->counters.frameAcquireAttemptCount;
    const auto unavailable = frameUnavailableStatus(*mState);
    if (unavailable != FrameAcquireStatus::StaleKey) {
        return {unavailable, std::nullopt};
    }
    if (!attachmentMatches(*mState, expectedAttachment)
            || mState->controller == nullptr) {
        return {FrameAcquireStatus::StaleKey, std::nullopt};
    }
    ++mState->activeFrameCount;
    ++mState->counters.frameAcquireSuccessCount;
    return {FrameAcquireStatus::Acquired,
            FrameGuard(mState, mState->controller, mState->attachment)};
}

FrameAcquireResult NativeRenderAttachmentLease::tryAcquireFrame(
        std::uint64_t expectedSurfaceToken) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    ++mState->counters.frameAcquireAttemptCount;
    const auto unavailable = frameUnavailableStatus(*mState);
    if (unavailable != FrameAcquireStatus::StaleKey) {
        return {unavailable, std::nullopt};
    }
    if (expectedSurfaceToken == 0
            || mState->attachment.surfaceToken != expectedSurfaceToken
            || mState->controller == nullptr) {
        return {FrameAcquireStatus::StaleKey, std::nullopt};
    }
    ++mState->activeFrameCount;
    ++mState->counters.frameAcquireSuccessCount;
    return {FrameAcquireStatus::Acquired,
            FrameGuard(mState, mState->controller, mState->attachment)};
}

SurfaceOperationAdmission
NativeRenderAttachmentLease::tryAcquireSurfaceOperation(
        WorkerBindingKey expectedWorker) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    if (!workerMatches(*mState, expectedWorker)) {
        return {LeaseMutationStatus::StaleWorker, std::nullopt};
    }
    if (mState->workerLifecycle == WorkerLifecycle::Closing) {
        return {LeaseMutationStatus::WorkerClosing, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Rebinding) {
        return {LeaseMutationStatus::DeferredRebind, std::nullopt};
    }
    ++mState->activeSurfaceOperationCount;
    return {LeaseMutationStatus::Applied,
            SurfaceOperationGuard(mState, mState->controller)};
}

InputBatchAdmission NativeRenderAttachmentLease::tryAcquireInput(
        std::uint64_t expectedSurfaceToken,
        std::uint64_t expectedInputEpoch) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    ++mState->counters.inputAcquireAttemptCount;
    if (mState->lifecycle == AttachmentLifecycle::Closing
            || mState->inputLifecycle == InputLifecycle::Closing) {
        return {InputBatchAdmissionStatus::AttachmentClosing,
                mState->inputEpoch, std::nullopt};
    }
    if (!mState->logicalSurface.active
            || expectedSurfaceToken == 0
            || expectedSurfaceToken != mState->logicalSurface.surfaceToken
            || mState->inputLifecycle == InputLifecycle::Inactive) {
        return {InputBatchAdmissionStatus::InactiveSurface,
                mState->inputEpoch, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Rebinding
            || mState->inputLifecycle == InputLifecycle::Resyncing) {
        return {InputBatchAdmissionStatus::Resyncing,
                mState->inputEpoch, std::nullopt};
    }
    if (expectedInputEpoch == 0 || expectedInputEpoch != mState->inputEpoch) {
        return {InputBatchAdmissionStatus::StaleEpoch,
                mState->inputEpoch, std::nullopt};
    }
    ++mState->activeInputCount;
    ++mState->counters.inputAcquireSuccessCount;
    return {InputBatchAdmissionStatus::Accepted, mState->inputEpoch,
            InputBatchGuard(mState, mState->controller, mState->inputEpoch)};
}

LeaseMutationStatus NativeRenderAttachmentLease::requestInputResync(
        std::uint64_t expectedSurfaceToken) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing
            || mState->inputLifecycle == InputLifecycle::Closing) {
        return LeaseMutationStatus::AttachmentClosing;
    }
    if (expectedSurfaceToken == 0 || !mState->logicalSurface.active
            || mState->logicalSurface.surfaceToken
                    != expectedSurfaceToken) {
        return LeaseMutationStatus::StaleSurface;
    }
    if (mState->inputLifecycle == InputLifecycle::Resyncing) {
        return LeaseMutationStatus::AlreadyAuthorized;
    }
    mState->inputLifecycle = InputLifecycle::Resyncing;
    mState->rebindAdvancedInputEpoch = false;
    return LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::completeInputResync(
        std::uint64_t expectedSurfaceToken,
        bool activePointersDrained) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return LeaseMutationStatus::AttachmentClosing;
    }
    if (!mState->logicalSurface.active
            || mState->logicalSurface.surfaceToken != expectedSurfaceToken) {
        return LeaseMutationStatus::StaleSurface;
    }
    if (!activePointersDrained) {
        return LeaseMutationStatus::DeferredRebind;
    }
    if (mState->inputLifecycle != InputLifecycle::Resyncing) {
        return LeaseMutationStatus::AlreadyAuthorized;
    }
    if (!mState->rebindAdvancedInputEpoch
            && !incrementGeneration(mState->inputEpoch)) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        return LeaseMutationStatus::GenerationExhausted;
    }
    mState->rebindAdvancedInputEpoch = false;
    mState->inputLifecycle = InputLifecycle::Accepting;
    return LeaseMutationStatus::Applied;
}

LeaseMutationResult<std::uint64_t>
NativeRenderAttachmentLease::publishPlacementGenome(
        std::shared_ptr<const cellularity::GenomeData> genome) {
    if (genome == nullptr) {
        return {LeaseMutationStatus::InvalidArgument, std::nullopt};
    }
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    if (!incrementGeneration(mState->placementGenomeRevision)) {
        return {LeaseMutationStatus::GenerationExhausted, std::nullopt};
    }
    mState->placementGenome = std::move(genome);
    return {LeaseMutationStatus::Applied,
            mState->placementGenomeRevision};
}

std::shared_ptr<const cellularity::GenomeData>
NativeRenderAttachmentLease::acquirePlacementGenome(
        std::uint64_t expectedRevision) const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing
            || expectedRevision == 0
            || expectedRevision != mState->placementGenomeRevision) {
        return nullptr;
    }
    return mState->placementGenome;
}

LeaseMutationResult<ControllerRebindTicket>
NativeRenderAttachmentLease::beginControllerRebindAndDrain(
        std::uint64_t expectedControllerGeneration) {
    std::unique_lock<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Rebinding) {
        return {LeaseMutationStatus::DeferredRebind, std::nullopt};
    }
    if (expectedControllerGeneration == 0
            || expectedControllerGeneration != mState->controllerGeneration) {
        return {LeaseMutationStatus::StaleSurface, std::nullopt};
    }
    if (mState->controllerGeneration
                    == std::numeric_limits<std::uint64_t>::max()
            || mState->rebindTransaction
                    == std::numeric_limits<std::uint64_t>::max()
            || mState->inputEpoch
                    == std::numeric_limits<std::uint64_t>::max()) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        return {LeaseMutationStatus::GenerationExhausted, std::nullopt};
    }
    (void)incrementGeneration(mState->rebindTransaction);
    (void)incrementGeneration(mState->inputEpoch);
    mState->lifecycle = AttachmentLifecycle::Rebinding;
    if (mState->logicalSurface.active) {
        mState->inputLifecycle = InputLifecycle::Resyncing;
        mState->rebindAdvancedInputEpoch = true;
    }
    const std::uint64_t transaction = mState->rebindTransaction;
    mState->drained.wait(lock, [&] {
        return mState->activeFrameCount == 0
                && mState->activeInputCount == 0
                && mState->activeSurfaceOperationCount == 0;
    });
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    return {LeaseMutationStatus::Applied,
            ControllerRebindTicket(
                    mState, transaction, expectedControllerGeneration)};
}

LeaseMutationResult<AttachmentGeneration>
NativeRenderAttachmentLease::commitControllerRebind(
        ControllerRebindTicket ticket,
        std::shared_ptr<NativeCellularityController> controller) {
    std::shared_ptr<NativeCellularityController> retired;
    return commitControllerRebind(
            std::move(ticket), std::move(controller), retired);
}

LeaseMutationResult<AttachmentGeneration>
NativeRenderAttachmentLease::commitControllerRebind(
        ControllerRebindTicket ticket,
        std::shared_ptr<NativeCellularityController> controller,
        std::shared_ptr<NativeCellularityController>& retiredController) {
    if (controller == nullptr || !ticket.valid()) {
        return {LeaseMutationStatus::InvalidArgument, std::nullopt};
    }
    std::shared_ptr<const cellularity::render::RenderSurfaceSessionBinding>
            backendBinding;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        const auto owner = ticket.mOwner.lock();
        if (owner.get() != mState.get()
                || ticket.mTransaction != mState->rebindTransaction
                || ticket.mExpectedControllerGeneration
                        != mState->controllerGeneration
                || mState->lifecycle != AttachmentLifecycle::Rebinding) {
            const LeaseMutationStatus status =
                    mState->lifecycle == AttachmentLifecycle::Closing
                    ? LeaseMutationStatus::AttachmentClosing
                    : LeaseMutationStatus::InvalidArgument;
            return {status, std::nullopt};
        }
        backendBinding = mState->backendBinding;
    }

    if (backendBinding != nullptr) {
        auto adapter = backendBinding->createControllerAdapter();
        if (adapter == nullptr) {
            return {LeaseMutationStatus::InvalidArgument, std::nullopt};
        }
        try {
            controller->installWorkerRenderSurfaceSession(std::move(adapter));
        } catch (...) {
            return {LeaseMutationStatus::InvalidArgument, std::nullopt};
        }
    }

    std::shared_ptr<RebindWakeControl> wake;
    AttachmentGeneration generation{};
    bool committed = false;
    LeaseMutationStatus commitStatus = LeaseMutationStatus::Applied;
    bool removeCandidateBinding = false;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        const auto owner = ticket.mOwner.lock();
        if (owner.get() != mState.get()
                || ticket.mTransaction != mState->rebindTransaction
                || ticket.mExpectedControllerGeneration
                        != mState->controllerGeneration
                || mState->lifecycle != AttachmentLifecycle::Rebinding) {
            const LeaseMutationStatus status =
                    mState->lifecycle == AttachmentLifecycle::Closing
                    ? LeaseMutationStatus::AttachmentClosing
                    : LeaseMutationStatus::InvalidArgument;
            commitStatus = status;
            removeCandidateBinding = backendBinding != nullptr;
        } else if (!incrementGeneration(mState->controllerGeneration)) {
            mState->lifecycle = AttachmentLifecycle::Closing;
            mState->inputLifecycle = InputLifecycle::Closing;
            ticket.consume();
            commitStatus = LeaseMutationStatus::GenerationExhausted;
            removeCandidateBinding = backendBinding != nullptr;
        } else {
            retiredController = std::move(mState->controller);
            mState->controller = std::move(controller);
            mState->attachment.controllerGeneration =
                    mState->controllerGeneration;
            invalidateGlesResources(*mState);
            mState->lifecycle = AttachmentLifecycle::Open;
            if (!mState->logicalSurface.active) {
                mState->inputLifecycle = InputLifecycle::Inactive;
            }
            ++mState->counters.controllerRebindCount;
            generation = {mState->controllerGeneration, mState->inputEpoch,
                    mState->glesResourcesControllerGeneration};
            wake = takeWakeControlLocked(*mState);
            ticket.consume();
            committed = true;
        }
    }
    if (removeCandidateBinding) {
        (void)controller->uninstallRenderSurfaceSession(
                backendBinding->kind(), backendBinding->identity());
        return {commitStatus, std::nullopt};
    }
    if (committed && backendBinding != nullptr
            && retiredController != nullptr) {
        // The old controller can no longer be acquired from the lease. Drop
        // its forwarding adapter before waking the render owner; no Vulkan
        // call is made by this operation.
        (void)retiredController->uninstallRenderSurfaceSession(
                backendBinding->kind(), backendBinding->identity());
    }
    if (wake != nullptr) {
        wake->notifyRebindReady();
    }
    return {LeaseMutationStatus::Applied, generation};
}

LeaseMutationStatus NativeRenderAttachmentLease::abortControllerRebind(
        ControllerRebindTicket ticket) {
    if (!ticket.valid()) {
        return LeaseMutationStatus::InvalidArgument;
    }
    std::shared_ptr<RebindWakeControl> wake;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        const auto owner = ticket.mOwner.lock();
        if (owner.get() != mState.get()
                || ticket.mTransaction != mState->rebindTransaction
                || ticket.mExpectedControllerGeneration
                        != mState->controllerGeneration) {
            return LeaseMutationStatus::InvalidArgument;
        }
        if (mState->lifecycle == AttachmentLifecycle::Closing) {
            ticket.consume();
            return LeaseMutationStatus::AttachmentClosing;
        }
        if (mState->lifecycle != AttachmentLifecycle::Rebinding) {
            ticket.consume();
            return LeaseMutationStatus::InvalidArgument;
        }
        mState->lifecycle = AttachmentLifecycle::Open;
        // A replacement transaction may already have abandoned controller-
        // owned GLES names before a late publication failure. Force the next
        // EGL frame to establish the surviving controller generation again.
        invalidateGlesResources(*mState);
        if (!mState->logicalSurface.active) {
            mState->inputLifecycle = InputLifecycle::Inactive;
        }
        wake = takeWakeControlLocked(*mState);
        ticket.consume();
    }
    if (wake != nullptr) {
        wake->notifyRebindReady();
    }
    return LeaseMutationStatus::Applied;
}

LeaseMutationStatus
NativeRenderAttachmentLease::beginAttachmentCloseAndDrain() {
    std::shared_ptr<RebindWakeControl> wake;
    std::unique_lock<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        mState->drained.wait(lock, [&] {
            return mState->activeFrameCount == 0
                    && mState->activeInputCount == 0
                    && mState->activeSurfaceOperationCount == 0;
        });
        return LeaseMutationStatus::AttachmentClosing;
    }
    LeaseMutationStatus result = LeaseMutationStatus::Applied;
    if (!incrementGeneration(mState->inputEpoch)) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        mState->workerLifecycle = mState->workerLifecycle
                        == WorkerLifecycle::Unbound
                ? WorkerLifecycle::Unbound
                : WorkerLifecycle::Closing;
        result = LeaseMutationStatus::GenerationExhausted;
    } else {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        if (mState->workerLifecycle == WorkerLifecycle::Authorized) {
            mState->workerLifecycle = WorkerLifecycle::Closing;
        }
    }
    wake = takeWakeControlLocked(*mState);
    mState->drained.wait(lock, [&] {
        return mState->activeFrameCount == 0
                && mState->activeInputCount == 0
                && mState->activeSurfaceOperationCount == 0;
    });
    lock.unlock();
    if (wake != nullptr) {
        wake->notifyRebindReady();
    }
    return result;
}

LeaseMutationResult<std::shared_ptr<NativeCellularityController>>
NativeRenderAttachmentLease::takeControllerForAttachmentClose() {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle != AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::NotAuthorized, std::nullopt};
    }
    if (mState->controllerTaken || mState->controller == nullptr) {
        return {LeaseMutationStatus::NotAuthorized, std::nullopt};
    }
    mState->controllerTaken = true;
    auto controller = std::move(mState->controller);
    return {LeaseMutationStatus::Applied, std::move(controller)};
}

LeaseMutationResult<WorkerBindingKey>
NativeRenderAttachmentLease::authorizeWorker(
        std::uint64_t surfaceToken,
        std::uint64_t workerIdentity) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (surfaceToken == 0 || workerIdentity == 0) {
        return {LeaseMutationStatus::InvalidArgument, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Rebinding) {
        return {LeaseMutationStatus::DeferredRebind, std::nullopt};
    }
    if (mState->workerLifecycle == WorkerLifecycle::Closing) {
        return {LeaseMutationStatus::WorkerClosing, std::nullopt};
    }
    if (mState->workerLifecycle == WorkerLifecycle::Authorized) {
        if (mState->worker.surfaceToken == surfaceToken
                && mState->worker.workerIdentity == workerIdentity) {
            return {LeaseMutationStatus::AlreadyAuthorized, mState->worker};
        }
        return {LeaseMutationStatus::WorkerBusy, std::nullopt};
    }
    if (!incrementGeneration(mState->workerBindingGeneration)) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        return {LeaseMutationStatus::GenerationExhausted, std::nullopt};
    }
    mState->worker = {surfaceToken, workerIdentity,
            mState->workerBindingGeneration};
    mState->workerLifecycle = WorkerLifecycle::Authorized;
    return {LeaseMutationStatus::Applied, mState->worker};
}

LeaseMutationStatus NativeRenderAttachmentLease::installBackendBinding(
        WorkerBindingKey expectedWorker,
        std::shared_ptr<const cellularity::render::RenderSurfaceSessionBinding>
                binding) {
    if (binding == nullptr || binding->identity() == nullptr) {
        return LeaseMutationStatus::InvalidArgument;
    }
    auto adapter = binding->createControllerAdapter();
    if (adapter == nullptr) {
        return LeaseMutationStatus::InvalidArgument;
    }
    const auto bindingKind = binding->kind();
    const void* const bindingIdentity = binding->identity();

    std::shared_ptr<NativeCellularityController> controller;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (mState->lifecycle == AttachmentLifecycle::Closing) {
            return LeaseMutationStatus::AttachmentClosing;
        }
        if (!workerMatches(*mState, expectedWorker)) {
            return LeaseMutationStatus::StaleWorker;
        }
        if (mState->backendBinding != nullptr) {
            return mState->backendBinding->identity() == bindingIdentity
                    ? LeaseMutationStatus::Applied
                    : LeaseMutationStatus::WorkerBusy;
        }
        if (mState->lifecycle == AttachmentLifecycle::Rebinding) {
            return LeaseMutationStatus::DeferredRebind;
        }
        controller = mState->controller;
        if (controller == nullptr) {
            return LeaseMutationStatus::AttachmentClosing;
        }
        ++mState->activeSurfaceOperationCount;
    }

    bool installed = false;
    try {
        controller->installWorkerRenderSurfaceSession(std::move(adapter));
        installed = true;
    } catch (...) {
    }

    LeaseMutationStatus result = LeaseMutationStatus::InvalidArgument;
    bool rollback = false;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (mState->activeSurfaceOperationCount != 0) {
            --mState->activeSurfaceOperationCount;
        }
        if (!installed) {
            result = LeaseMutationStatus::InvalidArgument;
        } else if (mState->lifecycle == AttachmentLifecycle::Closing) {
            result = LeaseMutationStatus::AttachmentClosing;
            rollback = true;
        } else if (!workerMatches(*mState, expectedWorker)) {
            result = LeaseMutationStatus::StaleWorker;
            rollback = true;
        } else if (mState->backendBinding != nullptr) {
            result = mState->backendBinding->identity() == bindingIdentity
                    ? LeaseMutationStatus::Applied
                    : LeaseMutationStatus::WorkerBusy;
            rollback = result != LeaseMutationStatus::Applied;
        } else {
            mState->backendBinding = binding;
            invalidateGlesResources(*mState);
            result = LeaseMutationStatus::Applied;
        }
        notifyDrained(*mState);
    }
    if (rollback) {
        (void)controller->uninstallRenderSurfaceSession(
                bindingKind, bindingIdentity);
    }
    return result;
}

LeaseMutationStatus NativeRenderAttachmentLease::removeBackendBinding(
        WorkerBindingKey expectedWorker) {
    std::shared_ptr<NativeCellularityController> controller;
    std::shared_ptr<const cellularity::render::RenderSurfaceSessionBinding>
            binding;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (!workerMatches(*mState, expectedWorker)) {
            return LeaseMutationStatus::StaleWorker;
        }
        if (mState->lifecycle == AttachmentLifecycle::Rebinding) {
            return LeaseMutationStatus::DeferredRebind;
        }
        if (mState->backendBinding == nullptr) {
            return LeaseMutationStatus::Applied;
        }
        controller = mState->controller;
        binding = mState->backendBinding;
        if (mState->lifecycle == AttachmentLifecycle::Closing
                && controller == nullptr) {
            mState->backendBinding.reset();
            invalidateGlesResources(*mState);
            return LeaseMutationStatus::Applied;
        }
        ++mState->activeSurfaceOperationCount;
    }

    const bool removed = controller != nullptr
            && controller->uninstallRenderSurfaceSession(
                    binding->kind(), binding->identity());
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        if (mState->activeSurfaceOperationCount != 0) {
            --mState->activeSurfaceOperationCount;
        }
        if (removed && mState->backendBinding == binding) {
            mState->backendBinding.reset();
            invalidateGlesResources(*mState);
        }
        notifyDrained(*mState);
    }
    return removed
            ? LeaseMutationStatus::Applied
            : LeaseMutationStatus::WorkerBusy;
}

LeaseMutationResult<RebindWakeRegistration>
NativeRenderAttachmentLease::registerRebindWake(
        WorkerBindingKey expectedWorker,
        std::weak_ptr<RebindWakeControl> wakeControl) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (wakeControl.expired()) {
        return {LeaseMutationStatus::InvalidArgument, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    if (!workerMatches(*mState, expectedWorker)) {
        return {LeaseMutationStatus::StaleWorker, std::nullopt};
    }
    if (mState->workerLifecycle == WorkerLifecycle::Closing) {
        return {LeaseMutationStatus::WorkerClosing, std::nullopt};
    }
    if (!incrementGeneration(mState->wakeRegistrationGeneration)) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        return {LeaseMutationStatus::GenerationExhausted, std::nullopt};
    }
    mState->wakeControl = std::move(wakeControl);
    return {LeaseMutationStatus::Applied,
            RebindWakeRegistration(mState, expectedWorker,
                    mState->wakeRegistrationGeneration)};
}

LeaseMutationResult<AttachmentKey>
NativeRenderAttachmentLease::activateSurface(
        WorkerBindingKey expectedWorker,
        std::uint64_t platformGeneration,
        int width,
        int height) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (platformGeneration == 0 || width <= 0 || height <= 0) {
        return {LeaseMutationStatus::InvalidArgument, std::nullopt};
    }
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return {LeaseMutationStatus::AttachmentClosing, std::nullopt};
    }
    if (!workerMatches(*mState, expectedWorker)) {
        return {LeaseMutationStatus::StaleWorker, std::nullopt};
    }
    if (mState->workerLifecycle == WorkerLifecycle::Closing) {
        return {LeaseMutationStatus::WorkerClosing, std::nullopt};
    }
    mState->logicalSurface = {true, expectedWorker.surfaceToken,
            platformGeneration, width, height};
    invalidateGlesResources(*mState);
    if (mState->lifecycle == AttachmentLifecycle::Rebinding) {
        return {LeaseMutationStatus::DeferredRebind, std::nullopt};
    }
    if (!incrementGeneration(mState->backendBindingGeneration)) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        return {LeaseMutationStatus::GenerationExhausted, std::nullopt};
    }
    mState->attachment = {expectedWorker.surfaceToken,
            expectedWorker.bindingGeneration,
            mState->backendBindingGeneration,
            platformGeneration,
            mState->controllerGeneration};
    if (mState->surfaceEpochIssued
            && !incrementGeneration(mState->inputEpoch)) {
        mState->lifecycle = AttachmentLifecycle::Closing;
        mState->inputLifecycle = InputLifecycle::Closing;
        return {LeaseMutationStatus::GenerationExhausted, std::nullopt};
    }
    mState->surfaceEpochIssued = true;
    mState->inputLifecycle = InputLifecycle::Accepting;
    mState->rebindAdvancedInputEpoch = false;
    return {LeaseMutationStatus::Applied, mState->attachment};
}

LeaseMutationStatus NativeRenderAttachmentLease::updateSurfaceDimensions(
        AttachmentKey expected,
        int width,
        int height) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (width <= 0 || height <= 0) {
        return LeaseMutationStatus::InvalidArgument;
    }
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return LeaseMutationStatus::AttachmentClosing;
    }
    if (!mState->logicalSurface.active
            || expected.surfaceToken != mState->logicalSurface.surfaceToken
            || expected.platformSurfaceGeneration
                    != mState->logicalSurface.platformSurfaceGeneration) {
        return LeaseMutationStatus::StaleSurface;
    }
    if (!attachmentMatches(*mState, expected)) {
        return LeaseMutationStatus::StaleSurface;
    }
    mState->logicalSurface.width = width;
    mState->logicalSurface.height = height;
    return mState->lifecycle == AttachmentLifecycle::Rebinding
            ? LeaseMutationStatus::DeferredRebind
            : LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::deactivateSurface(
        AttachmentKey expected) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (!attachmentMatches(*mState, expected)) {
        return LeaseMutationStatus::StaleSurface;
    }
    mState->logicalSurface = {};
    mState->attachment = {};
    invalidateGlesResources(*mState);
    mState->inputLifecycle = mState->lifecycle == AttachmentLifecycle::Closing
            ? InputLifecycle::Closing
            : InputLifecycle::Inactive;
    return LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::updateSurfaceDimensions(
        std::uint64_t expectedSurfaceToken,
        int width,
        int height) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (width <= 0 || height <= 0 || expectedSurfaceToken == 0) {
        return LeaseMutationStatus::InvalidArgument;
    }
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return LeaseMutationStatus::AttachmentClosing;
    }
    if (!mState->logicalSurface.active
            || mState->logicalSurface.surfaceToken != expectedSurfaceToken) {
        return LeaseMutationStatus::StaleSurface;
    }
    mState->logicalSurface.width = width;
    mState->logicalSurface.height = height;
    return mState->lifecycle == AttachmentLifecycle::Rebinding
            ? LeaseMutationStatus::DeferredRebind
            : LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::deactivateSurface(
        WorkerBindingKey expectedWorker) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (!workerMatches(*mState, expectedWorker)) {
        return LeaseMutationStatus::StaleWorker;
    }
    if (!mState->logicalSurface.active) {
        return LeaseMutationStatus::StaleSurface;
    }
    mState->logicalSurface = {};
    mState->attachment = {};
    invalidateGlesResources(*mState);
    mState->inputLifecycle = mState->lifecycle == AttachmentLifecycle::Closing
            ? InputLifecycle::Closing
            : InputLifecycle::Inactive;
    return LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::deactivateSurface(
        std::uint64_t expectedSurfaceToken) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (expectedSurfaceToken == 0 || !mState->logicalSurface.active
            || mState->logicalSurface.surfaceToken != expectedSurfaceToken) {
        return LeaseMutationStatus::StaleSurface;
    }
    mState->logicalSurface = {};
    mState->attachment = {};
    invalidateGlesResources(*mState);
    mState->inputLifecycle = mState->lifecycle == AttachmentLifecycle::Closing
            ? InputLifecycle::Closing
            : InputLifecycle::Inactive;
    return LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::markWorkerClosing(
        WorkerBindingKey expectedWorker) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (!workerMatches(*mState, expectedWorker)) {
        return LeaseMutationStatus::StaleWorker;
    }
    if (mState->workerLifecycle == WorkerLifecycle::Closing) {
        return LeaseMutationStatus::WorkerClosing;
    }
    if (mState->workerLifecycle != WorkerLifecycle::Authorized) {
        return LeaseMutationStatus::NotAuthorized;
    }
    mState->workerLifecycle = WorkerLifecycle::Closing;
    return LeaseMutationStatus::Applied;
}

LeaseMutationStatus NativeRenderAttachmentLease::finishWorkerClose(
        WorkerBindingKey expectedWorker) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (!workerMatches(*mState, expectedWorker)) {
        return LeaseMutationStatus::StaleWorker;
    }
    if (mState->workerLifecycle != WorkerLifecycle::Closing) {
        return LeaseMutationStatus::NotAuthorized;
    }
    mState->logicalSurface = {};
    mState->attachment = {};
    invalidateGlesResources(*mState);
    mState->wakeControl.reset();
    mState->worker = {};
    mState->workerLifecycle = WorkerLifecycle::Unbound;
    if (mState->lifecycle != AttachmentLifecycle::Closing) {
        mState->inputLifecycle = InputLifecycle::Inactive;
    }
    notifyDrained(*mState);
    return LeaseMutationStatus::Applied;
}

std::shared_ptr<NativeCellularityController>
NativeRenderAttachmentLease::acquireControllerForColdCall() const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return nullptr;
    }
    return mState->controller;
}

AttachmentGeneration
NativeRenderAttachmentLease::generationSnapshot() const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    return {mState->controllerGeneration, mState->inputEpoch,
            mState->glesResourcesControllerGeneration};
}

LogicalSurfaceState NativeRenderAttachmentLease::logicalSurfaceState() const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    return mState->logicalSurface;
}

std::uint64_t NativeRenderAttachmentLease::activeInputEpoch(
        std::uint64_t expectedSurfaceToken) const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (expectedSurfaceToken == 0
            || mState->lifecycle != AttachmentLifecycle::Open
            || !mState->logicalSurface.active
            || mState->logicalSurface.surfaceToken != expectedSurfaceToken
            || mState->inputLifecycle != InputLifecycle::Accepting) {
        return 0;
    }
    return mState->inputEpoch;
}

std::optional<std::uint64_t>
NativeRenderAttachmentLease::vulkanProfilingWorkerIdentity() const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->workerLifecycle != WorkerLifecycle::Authorized
            || !mState->worker.valid() || mState->backendBinding == nullptr
            || mState->backendBinding->kind()
                    != cellularity::render::RenderBackendKind::Vulkan) {
        return std::nullopt;
    }
    return mState->worker.workerIdentity;
}

bool NativeRenderAttachmentLease::hasGlesResourcesForFrame(const FrameGuard& frame) const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    // A retained controller can outlive both its backend and EGL context.
    // The generation marker alone must never authorize a replacement surface.
    return frame.mState == mState && attachmentMatches(*mState, frame.mKey)
            && mState->glesResourcesAttachment == frame.mKey
            && mState->glesResourcesControllerGeneration == frame.mKey.controllerGeneration
            && frame.controller().hasRenderSurfaceSession()
            && frame.controller().renderBackendKind() == cellularity::render::RenderBackendKind::OpenGlEs
            && frame.controller().hasRendererResources();
}

LeaseMutationStatus NativeRenderAttachmentLease::markGlesResourcesLoaded(
        std::uint64_t controllerGeneration) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->lifecycle == AttachmentLifecycle::Closing) {
        return LeaseMutationStatus::AttachmentClosing;
    }
    if (controllerGeneration == 0
            || controllerGeneration != mState->controllerGeneration) {
        return LeaseMutationStatus::StaleSurface;
    }
    mState->glesResourcesControllerGeneration = controllerGeneration;
    mState->glesResourcesAttachment = mState->attachment;
    return LeaseMutationStatus::Applied;
}

NativeRenderLeaseCounters NativeRenderAttachmentLease::debugCounters() const {
    std::lock_guard<std::mutex> lock(mState->mutex);
    return mState->counters;
}

} // namespace cellularity::app
