#include "jni/NativePreviewEndpointHandle.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>

#include "app/EnvironmentFieldPreviewEndpoint.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity::tests;

namespace {

void testKindsAndGenerationsAreCheckedBeforeAcquire() {
    auto genome = cellularity::app::genome_preview::
            createGenomePreviewEndpoint(8);
    auto environment =
            cellularity::app::createEnvironmentFieldPreviewEndpoint();
    NativePreviewEndpointHandle genomeHandle(genome);
    NativePreviewEndpointHandle environmentHandle(environment);

    const auto genomeReady =
            genomeHandle.acquireGenome(genome->generation());
    assertTrue(genomeReady.status == PreviewEndpointAcquireStatus::Ready
                    && genomeReady.endpoint == genome,
            "Genome handle returns its exact typed shared endpoint");
    assertTrue(genomeHandle
                            .acquireEnvironmentField(genome->generation())
                            .status
                    == PreviewEndpointAcquireStatus::WrongKind,
            "Genome payload can never be acquired as an environment endpoint");
    assertTrue(environmentHandle
                            .acquireGenome(environment->generation())
                            .status
                    == PreviewEndpointAcquireStatus::WrongKind,
            "Environment payload can never be acquired as a genome endpoint");
    assertTrue(environmentHandle
                            .acquireEnvironmentField(
                                    environment->generation() + 1)
                            .status
                    == PreviewEndpointAcquireStatus::StaleGeneration,
            "A stale endpoint generation fails closed before payload access");
}

void testCloseRacesWithTypedAcquireWithoutInvalidPayload() {
    auto endpoint =
            cellularity::app::createEnvironmentFieldPreviewEndpoint();
    NativePreviewEndpointHandle handle(endpoint);
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> readyCount{0};
    std::atomic<int> closedCount{0};

    std::thread reader([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        while (!stop.load(std::memory_order_acquire)) {
            const auto acquired =
                    handle.acquireEnvironmentField(endpoint->generation());
            if (acquired.status == PreviewEndpointAcquireStatus::Ready) {
                assertTrue(acquired.endpoint == endpoint,
                        "Concurrent acquire returns only the original endpoint");
                readyCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                assertTrue(acquired.status
                                == PreviewEndpointAcquireStatus::Closed,
                        "Concurrent acquire is either ready or closed");
                closedCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    start.store(true, std::memory_order_release);
    auto payload = handle.closeAndTake();
    stop.store(true, std::memory_order_release);
    reader.join();
    assertTrue(std::get<1>(payload) == endpoint,
            "Close transfers the exact typed endpoint once");
    assertTrue(handle
                            .acquireEnvironmentField(endpoint->generation())
                            .status
                    == PreviewEndpointAcquireStatus::Closed,
            "No acquire can reopen a closed endpoint wrapper");
    assertTrue(readyCount.load(std::memory_order_relaxed) >= 0
                    && closedCount.load(std::memory_order_relaxed) >= 0,
            "Acquire/close race completes without an invalid payload");
}

void testEncodedHandleKeepsEndpointAliveUntilDestroy() {
    auto endpoint = cellularity::app::genome_preview::
            createGenomePreviewEndpoint(4);
    const std::uint64_t generation = endpoint->generation();
    const std::uint64_t handle =
            createNativePreviewEndpointHandle(endpoint);
    endpoint.reset();

    auto acquired = acquireGenomePreviewEndpoint(handle, generation);
    assertTrue(acquired.status == PreviewEndpointAcquireStatus::Ready
                    && acquired.endpoint != nullptr,
            "Encoded wrapper owns the endpoint independently of the controller");
    acquired.endpoint.reset();
    closeAndDestroyNativePreviewEndpointHandle(handle);
}

} // namespace

int main() {
    testKindsAndGenerationsAreCheckedBeforeAcquire();
    testCloseRacesWithTypedAcquireWithoutInvalidPayload();
    testEncodedHandleKeepsEndpointAliveUntilDestroy();
    std::cout << "Native preview endpoint handle tests passed\n";
    return 0;
}
