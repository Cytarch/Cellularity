#include "app/NativeWorldSession.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace {

std::mutex gWorldEpochMutex;
std::uint64_t gNextWorldEpoch = 1;

std::uint64_t allocateWorldEpoch() {
    const std::lock_guard<std::mutex> lock(gWorldEpochMutex);
    if (gNextWorldEpoch == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
                "NativeWorldSession world epoch space is exhausted");
    }
    return gNextWorldEpoch++;
}

} // namespace

NativeWorldSession::ReadAccess::ReadAccess(const NativeWorldSession& session)
        : mSession(&session), mLock(session.mMutex) {}

const SimulationWorld& NativeWorldSession::ReadAccess::world() const {
    return *mSession->mWorld;
}

std::uint64_t NativeWorldSession::ReadAccess::worldEpoch() const {
    return mSession->mWorldEpoch;
}

std::int64_t NativeWorldSession::ReadAccess::environmentRevision() const {
    return mSession->mEnvironmentRevision;
}

NativeWorldSession::WriteAccess::WriteAccess(NativeWorldSession& session)
        : mSession(&session), mLock(session.mMutex) {}

SimulationWorld& NativeWorldSession::WriteAccess::world() {
    return *mSession->mWorld;
}

const SimulationWorld& NativeWorldSession::WriteAccess::world() const {
    return *mSession->mWorld;
}

std::uint64_t NativeWorldSession::WriteAccess::worldEpoch() const {
    return mSession->mWorldEpoch;
}

std::int64_t NativeWorldSession::WriteAccess::environmentRevision() const {
    return mSession->mEnvironmentRevision;
}

std::int64_t NativeWorldSession::WriteAccess::markEnvironmentChanged() {
    return ++mSession->mEnvironmentRevision;
}

std::uint64_t NativeWorldSession::WriteAccess::markWorldContentsReplaced() {
    mSession->mWorldEpoch = allocateWorldEpoch();
    mSession->mEnvironmentRevision = 0;
    return mSession->mWorldEpoch;
}

NativeWorldSession::NativeWorldSession(bool previewMode,
        const Environment& environment)
        : NativeWorldSession(
                  std::make_unique<SimulationWorld>(previewMode, environment)) {}

NativeWorldSession::NativeWorldSession(std::unique_ptr<SimulationWorld> world)
        : mWorld(std::move(world)), mWorldEpoch(allocateWorldEpoch()) {
    if (mWorld == nullptr) {
        throw std::invalid_argument("NativeWorldSession requires a SimulationWorld");
    }
}

NativeWorldSession::ReadAccess NativeWorldSession::read() const {
    return ReadAccess(*this);
}

NativeWorldSession::WriteAccess NativeWorldSession::write() {
    return WriteAccess(*this);
}
