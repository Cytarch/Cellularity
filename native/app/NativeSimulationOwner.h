#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
#include <optional>
#include <stdexcept>

#include "app/microscope/MicroscopeCommandMailbox.h"
#endif

#include "app/NativeSimulationStepper.h"
#include "app/NativeWorldSession.h"
#include "app/microscope/MicroscopeSimulationState.h"
#include "app/microscope/interaction/MicroscopeWorldInteractionState.h"
#include "execution/SimulationExecutionContext.h"

class SimulationWorld;
class Environment;

// Sole storage owner for mutable simulation state and its non-serialized
// execution context. Compatibility callers retain a direct serial path; the
// microscope owner thread explicitly supplies the execution context.
class NativeSimulationOwner final {
public:
    class ReadAccess final {
    public:
        ReadAccess(ReadAccess&&) noexcept = default;
        ReadAccess& operator=(ReadAccess&&) noexcept = default;
        ReadAccess(const ReadAccess&) = delete;
        ReadAccess& operator=(const ReadAccess&) = delete;

        [[nodiscard]] const SimulationWorld& world() const noexcept;
        [[nodiscard]] std::uint64_t worldEpoch() const noexcept;
        [[nodiscard]] std::int64_t environmentRevision() const noexcept;
        [[nodiscard]] std::uint64_t completedUpdateCount() const noexcept;
        [[nodiscard]] const cellularity::microscope::MicroscopeSimulationState&
        microscopeSimulation() const noexcept;
        [[nodiscard]] const cellularity::microscope::
                MicroscopeWorldInteractionState&
                microscopeInteraction() const noexcept;

    private:
        friend class NativeSimulationOwner;

        explicit ReadAccess(const NativeSimulationOwner& owner);

        const NativeSimulationOwner* mOwner;
        NativeWorldSession::ReadAccess mWorldAccess;
    };

    class WriteAccess final {
    public:
        WriteAccess(WriteAccess&&) noexcept = default;
        WriteAccess& operator=(WriteAccess&&) noexcept = default;
        WriteAccess(const WriteAccess&) = delete;
        WriteAccess& operator=(const WriteAccess&) = delete;

        [[nodiscard]] SimulationWorld& world() noexcept;
        [[nodiscard]] const SimulationWorld& world() const noexcept;
        [[nodiscard]] std::uint64_t worldEpoch() const noexcept;
        [[nodiscard]] std::int64_t environmentRevision() const noexcept;
        [[nodiscard]] std::uint64_t completedUpdateCount() const noexcept;
        [[nodiscard]] NativeSimulationStepper& simulationStepper() noexcept;
        [[nodiscard]] cellularity::microscope::MicroscopeSimulationState&
        microscopeSimulation() noexcept;
        [[nodiscard]]
        cellularity::microscope::MicroscopeWorldInteractionState&
        microscopeInteraction() noexcept;

        void update(bool isForRendering, double speed);
        void update1(bool isForRendering, double speed);
        void update2(bool forView, double speed);
        void updateWithExecutionContext(bool isForRendering, double speed);
        void update1WithExecutionContext(bool isForRendering, double speed);
        void update2WithExecutionContext(bool forView, double speed);
        void bindSimulationExecutionToCurrentThread();
        void releaseSimulationExecutionFromCurrentThread() noexcept;
        void setSimulationAdditionalThreadCount(
                std::size_t requestedAdditionalThreadCount);
        [[nodiscard]] cellularity::execution::SimulationExecutionContext::Snapshot
        simulationExecutionSnapshot() const noexcept;
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        template <typename Service, typename ServiceRequested, typename Now>
        [[nodiscard]] cellularity::simulation::SimulationServiceCycleResult
        executeScheduledSimulationCycle(Service&& service,
                ServiceRequested&& serviceRequested,
                Now&& now) {
            auto& simulationState = mOwner->mMicroscopeSimulation;
            auto* scheduler = simulationState.scheduler();
            if (scheduler == nullptr) {
                throw std::logic_error(
                        "Scheduled simulation cycle requires an enabled scheduler");
            }

            std::optional<cellularity::microscope::MicroscopeSchedulerModeContract>
                    modeContract;
            auto serviceAndRefreshContract = [&](cellularity::simulation::
                                                             SimulationServicePhase phase) {
                service(phase);
                if (phase == cellularity::simulation::SimulationServicePhase::BeforeBatch) {
                    if (simulationState.scheduler() != scheduler) {
                        throw std::logic_error(
                                "Owner service replaced the scheduler during a cycle");
                    }
                    modeContract = simulationState.schedulerModeContract();
                    const auto expectedPolicy =
                            simulationState.schedulerLifecyclePaused()
                            ? cellularity::simulation::SimulationRatePolicyKind::Paused
                            : modeContract->ratePolicy.kind();
                    if (scheduler->policyKind() != expectedPolicy) {
                        throw std::logic_error(
                                "Owner scheduler policy does not match its mode contract");
                    }
                }
            };
            auto executeUpdate = [&] {
                update(modeContract->updateForRendering, modeContract->updateSpeed);
            };

            return cellularity::simulation::executeSimulationServiceCycle(
                    *scheduler, serviceAndRefreshContract, executeUpdate,
                    serviceRequested, now);
        }

        // Command application is deliberately bound to WriteAccess: an ingress
        // producer can only enqueue value data, while every world read/write in the
        // handler occurs at this owner safe point.
        template <typename Apply, typename Now>
        [[nodiscard]] cellularity::microscope::MicroscopeCommandServiceResult
        serviceMicroscopeCommands(
                cellularity::microscope::MicroscopeCommandMailbox& mailbox,
                cellularity::microscope::MicroscopeCommandTimePoint deadline,
                std::uint32_t hardCommandLimit, Apply&& apply, Now&& now) {
            return mailbox.serviceUntil(
                    deadline, hardCommandLimit,
                    [&](const cellularity::microscope::MicroscopeWorldCommand& command) {
                        return apply(*this, command);
                    },
                    now);
        }
#endif
        void resetCompletedUpdateCount() noexcept;
        std::int64_t markEnvironmentChanged();
        std::uint64_t markWorldContentsReplaced();

    private:
        friend class NativeSimulationOwner;

        explicit WriteAccess(NativeSimulationOwner& owner);

        NativeSimulationOwner* mOwner;
        NativeWorldSession::WriteAccess mWorldAccess;
    };

    NativeSimulationOwner(bool previewMode, const Environment& environment);
    explicit NativeSimulationOwner(std::unique_ptr<SimulationWorld> world);

    NativeSimulationOwner(const NativeSimulationOwner&) = delete;
    NativeSimulationOwner& operator=(const NativeSimulationOwner&) = delete;
    NativeSimulationOwner(NativeSimulationOwner&&) = delete;
    NativeSimulationOwner& operator=(NativeSimulationOwner&&) = delete;

    [[nodiscard]] ReadAccess read() const;
    [[nodiscard]] WriteAccess write();

private:
    NativeWorldSession mWorldSession;
    NativeSimulationStepper mSimulation;
    cellularity::execution::SimulationExecutionContext mExecution;
    cellularity::microscope::MicroscopeSimulationState mMicroscopeSimulation;
    cellularity::microscope::MicroscopeWorldInteractionState
            mMicroscopeInteraction;
};
