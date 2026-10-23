#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "core/SimulationWorld.h"

class NativeWorldSession {
public:
    // Access objects are the only world/revision boundary. They hold the same
    // session mutex for their entire lifetime and cannot be copied or retained
    // by rendering code. A recursive mutex permits controller composition to
    // call another guarded operation on the same thread without exposing it.
    class ReadAccess {
    public:
        ReadAccess(ReadAccess&&) noexcept = default;
        ReadAccess& operator=(ReadAccess&&) noexcept = default;
        ReadAccess(const ReadAccess&) = delete;
        ReadAccess& operator=(const ReadAccess&) = delete;

        const SimulationWorld& world() const;
        std::uint64_t worldEpoch() const;
        std::int64_t environmentRevision() const;

    private:
        friend class NativeWorldSession;

        explicit ReadAccess(const NativeWorldSession& session);

        const NativeWorldSession* mSession;
        std::unique_lock<std::recursive_mutex> mLock;
    };

    class WriteAccess {
    public:
        WriteAccess(WriteAccess&&) noexcept = default;
        WriteAccess& operator=(WriteAccess&&) noexcept = default;
        WriteAccess(const WriteAccess&) = delete;
        WriteAccess& operator=(const WriteAccess&) = delete;

        SimulationWorld& world();
        const SimulationWorld& world() const;
        std::uint64_t worldEpoch() const;
        std::int64_t environmentRevision() const;
        std::int64_t markEnvironmentChanged();
        std::uint64_t markWorldContentsReplaced();

    private:
        friend class NativeWorldSession;

        explicit WriteAccess(NativeWorldSession& session);

        NativeWorldSession* mSession;
        std::unique_lock<std::recursive_mutex> mLock;
    };

    NativeWorldSession(bool previewMode, const Environment& environment);
    explicit NativeWorldSession(std::unique_ptr<SimulationWorld> world);

    [[nodiscard]] ReadAccess read() const;
    [[nodiscard]] WriteAccess write();

private:
    std::unique_ptr<SimulationWorld> mWorld;
    mutable std::recursive_mutex mMutex;
    std::uint64_t mWorldEpoch = 0;
    std::int64_t mEnvironmentRevision = 0;
};
