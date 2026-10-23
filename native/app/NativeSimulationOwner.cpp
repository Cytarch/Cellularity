#include "app/NativeSimulationOwner.h"

#include <utility>

#include "core/SimulationWorld.h"

NativeSimulationOwner::ReadAccess::ReadAccess(
        const NativeSimulationOwner& owner)
        : mOwner(&owner), mWorldAccess(owner.mWorldSession.read()) {}

const SimulationWorld& NativeSimulationOwner::ReadAccess::world() const noexcept {
    return mWorldAccess.world();
}

std::uint64_t NativeSimulationOwner::ReadAccess::worldEpoch() const noexcept {
    return mWorldAccess.worldEpoch();
}

std::int64_t
NativeSimulationOwner::ReadAccess::environmentRevision() const noexcept {
    return mWorldAccess.environmentRevision();
}

std::uint64_t
NativeSimulationOwner::ReadAccess::completedUpdateCount() const noexcept {
    return mOwner->mSimulation.completedUpdateCount();
}

const cellularity::microscope::MicroscopeSimulationState&
NativeSimulationOwner::ReadAccess::microscopeSimulation() const noexcept {
    return mOwner->mMicroscopeSimulation;
}

const cellularity::microscope::MicroscopeWorldInteractionState&
NativeSimulationOwner::ReadAccess::microscopeInteraction() const noexcept {
    return mOwner->mMicroscopeInteraction;
}

NativeSimulationOwner::WriteAccess::WriteAccess(NativeSimulationOwner& owner)
        : mOwner(&owner), mWorldAccess(owner.mWorldSession.write()) {}

SimulationWorld& NativeSimulationOwner::WriteAccess::world() noexcept {
    return mWorldAccess.world();
}

const SimulationWorld& NativeSimulationOwner::WriteAccess::world() const noexcept {
    return mWorldAccess.world();
}

std::uint64_t NativeSimulationOwner::WriteAccess::worldEpoch() const noexcept {
    return mWorldAccess.worldEpoch();
}

std::int64_t
NativeSimulationOwner::WriteAccess::environmentRevision() const noexcept {
    return mWorldAccess.environmentRevision();
}

std::uint64_t
NativeSimulationOwner::WriteAccess::completedUpdateCount() const noexcept {
    return mOwner->mSimulation.completedUpdateCount();
}

NativeSimulationStepper&
NativeSimulationOwner::WriteAccess::simulationStepper() noexcept {
    return mOwner->mSimulation;
}

cellularity::microscope::MicroscopeSimulationState&
NativeSimulationOwner::WriteAccess::microscopeSimulation() noexcept {
    return mOwner->mMicroscopeSimulation;
}

cellularity::microscope::MicroscopeWorldInteractionState&
NativeSimulationOwner::WriteAccess::microscopeInteraction() noexcept {
    return mOwner->mMicroscopeInteraction;
}

void NativeSimulationOwner::WriteAccess::update(bool isForRendering,
        double speed) {
    mOwner->mSimulation.update(world(), isForRendering, speed);
}

void NativeSimulationOwner::WriteAccess::update1(bool isForRendering,
        double speed) {
    mOwner->mSimulation.update1(world(), isForRendering, speed);
}

void NativeSimulationOwner::WriteAccess::update2(bool forView, double speed) {
    mOwner->mSimulation.update2(world(), forView, speed);
}

void NativeSimulationOwner::WriteAccess::updateWithExecutionContext(
        bool isForRendering, double speed) {
    mOwner->mSimulation.update(world(), isForRendering, speed,
            &mOwner->mExecution);
}

void NativeSimulationOwner::WriteAccess::update1WithExecutionContext(
        bool isForRendering, double speed) {
    mOwner->mSimulation.update1(world(), isForRendering, speed,
            &mOwner->mExecution);
}

void NativeSimulationOwner::WriteAccess::update2WithExecutionContext(
        bool forView, double speed) {
    mOwner->mSimulation.update2(world(), forView, speed, &mOwner->mExecution);
}

void NativeSimulationOwner::WriteAccess::
        bindSimulationExecutionToCurrentThread() {
    mOwner->mExecution.bindToCurrentThread();
}

void NativeSimulationOwner::WriteAccess::
        releaseSimulationExecutionFromCurrentThread() noexcept {
    mOwner->mExecution.releaseFromCurrentThread();
}

void NativeSimulationOwner::WriteAccess::setSimulationAdditionalThreadCount(
        std::size_t requestedAdditionalThreadCount) {
    if (world().getUpdateStage() != SimulationWorld::UPDATE_STAGE_CAN_TOUCH) {
        throw std::logic_error(
                "Simulation worker capacity can change only between updates");
    }
    mOwner->mExecution.setRequestedAdditionalThreadCount(
            requestedAdditionalThreadCount);
}

cellularity::execution::SimulationExecutionContext::Snapshot
NativeSimulationOwner::WriteAccess::simulationExecutionSnapshot()
        const noexcept {
    return mOwner->mExecution.snapshot();
}

void NativeSimulationOwner::WriteAccess::resetCompletedUpdateCount() noexcept {
    mOwner->mSimulation.resetCompletedUpdateCount();
}

std::int64_t NativeSimulationOwner::WriteAccess::markEnvironmentChanged() {
    return mWorldAccess.markEnvironmentChanged();
}

std::uint64_t NativeSimulationOwner::WriteAccess::markWorldContentsReplaced() {
    if (world().getUpdateStage() != SimulationWorld::UPDATE_STAGE_CAN_TOUCH) {
        throw std::logic_error(
                "World contents can be replaced only between simulation updates");
    }
    const std::uint64_t worldEpoch = mWorldAccess.markWorldContentsReplaced();
    mOwner->mExecution.resetForWorldReplacement();
    mOwner->mMicroscopeSimulation.resetForWorldReplacement();
    mOwner->mMicroscopeInteraction.resetForWorldReplacement();
    return worldEpoch;
}

NativeSimulationOwner::NativeSimulationOwner(bool previewMode,
        const Environment& environment)
        : mWorldSession(previewMode, environment) {}

NativeSimulationOwner::NativeSimulationOwner(std::unique_ptr<SimulationWorld> world)
        : mWorldSession(std::move(world)) {}

NativeSimulationOwner::ReadAccess NativeSimulationOwner::read() const {
    return ReadAccess(*this);
}

NativeSimulationOwner::WriteAccess NativeSimulationOwner::write() {
    return WriteAccess(*this);
}
