#include "execution/DispatcherCapacity.h"

#include <algorithm>
#include <thread>

#include "hwy/contrib/thread_pool/thread_pool.h"
#include "hwy/contrib/thread_pool/topology.h"

namespace cellularity::execution {

DispatcherCapacity detectDispatcherCapacity() {
    DispatcherCapacity result;
    result.threadingSupported = hwy::HaveThreadingSupport();
    result.hardwareConcurrency = std::thread::hardware_concurrency();
    if (!result.threadingSupported) {
        return result;
    }

    hwy::LogicalProcessorSet affinity;
    if (hwy::GetThreadAffinity(affinity)) {
        result.affinityProcessorCount = affinity.Count();
        // A successful query with an empty mask is pathological. Treat it as
        // unknown instead of falling back to a potentially broader machine-wide
        // count that the process may not actually use.
        if (result.affinityProcessorCount == 0) {
            return result;
        }
        result.processorCountSource = ProcessorCountSource::Affinity;
        result.usableProcessorCount = result.affinityProcessorCount;
    } else if (result.hardwareConcurrency != 0) {
        result.processorCountSource = ProcessorCountSource::HardwareConcurrency;
        result.usableProcessorCount = result.hardwareConcurrency;
    } else {
        // In particular, do not evaluate Highway's MaxThreads() here. Its current
        // implementation subtracts one in an unsigned type and can underflow when
        // the platform reports zero processors.
        return result;
    }

    if (result.usableProcessorCount <= 1) {
        return result;
    }

    result.processorMaxAdditionalThreads = result.usableProcessorCount - 1;

    // Calling MaxThreads is safe only after independently proving that the
    // usable processor count exceeds one. Its result remains untrusted and is
    // intersected with both the independently derived limit and the project cap.
    result.highwayReportedMaxAdditionalThreads = hwy::ThreadPool::MaxThreads();
    result.safeMaxAdditionalThreads = deriveSafeAdditionalThreadCount(
            result.threadingSupported, result.usableProcessorCount,
            result.highwayReportedMaxAdditionalThreads,
            result.projectMaxAdditionalThreads);
    return result;
}

std::size_t deriveSafeAdditionalThreadCount(
        bool threadingSupported, std::size_t usableProcessorCount,
        std::size_t highwayReportedMaxAdditionalThreads,
        std::size_t projectMaxAdditionalThreads) noexcept {
    if (!threadingSupported || usableProcessorCount <= 1) {
        return 0;
    }
    return std::min({usableProcessorCount - 1,
            highwayReportedMaxAdditionalThreads,
            projectMaxAdditionalThreads});
}

std::size_t
clampAdditionalThreadCount(std::size_t requestedAdditionalThreads,
        const DispatcherCapacity& capacity) noexcept {
    return std::min(requestedAdditionalThreads,
            capacity.safeMaxAdditionalThreads);
}

const char* processorCountSourceName(ProcessorCountSource source) noexcept {
    switch (source) {
        case ProcessorCountSource::Unavailable:
            return "unavailable";
        case ProcessorCountSource::Affinity:
            return "affinity";
        case ProcessorCountSource::HardwareConcurrency:
            return "hardware-concurrency";
    }
    return "invalid";
}

} // namespace cellularity::execution
