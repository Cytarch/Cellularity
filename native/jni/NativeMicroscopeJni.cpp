#include <jni.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>

#include "NativeControllerHandle.h"
#include "NativeJniUtils.h"
#include "NativeRenderEndpointHandle.h"
#include "app/NativeCellularityController.h"
#include "app/microscope/MicroscopeInputBatchLayout.h"
#include "app/microscope/MicroscopeRuntimePublicationContract.h"
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "app/render/NativeRenderAttachmentLease.h"
#include "app/render/NativeRenderProfilingState.h"
#include "core/CellData.h"
#include "core/Gene.h"
#include "formats/GenomeDraftCodec.h"
#include "io/NativeByteStream.h"

namespace {

std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>
renderEndpointLeaseFromHandle(JNIEnv*, jlong endpointHandle) {
    return ::renderEndpointLeaseFromHandle(
            static_cast<std::uint64_t>(endpointHandle));
}

constexpr jlong kRateTelemetrySchemaVersion = 1;
constexpr std::size_t kRateTelemetryFieldCount = 42;

jlong doubleBits(double value) noexcept {
    static_assert(sizeof(jlong) == sizeof(double));
    jlong bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint8_t* requireDirectOutputBuffer(
        JNIEnv* env, jobject buffer, std::size_t requiredCapacity,
        const char* message) {
    if (buffer == nullptr) {
        throwJavaException(
                env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, message);
        return nullptr;
    }
    void* address = env->GetDirectBufferAddress(buffer);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (address == nullptr || capacity < 0
            || static_cast<std::uint64_t>(capacity) < requiredCapacity) {
        throwJavaException(
                env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, message);
        return nullptr;
    }
    return static_cast<std::uint8_t*>(address);
}

std::shared_ptr<NativeCellularityController> controllerFromRenderEndpoint(
        JNIEnv* env, jlong endpointHandle) {
    auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
    return lease == nullptr ? nullptr : lease->acquireControllerForColdCall();
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMicroscopeRuntimePublicationIntervalMillis(
        JNIEnv*, jclass) {
    return static_cast<jlong>(
            cellularity::microscope::kPublicationIntervalMillis);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeStartMicroscopeRuntime(
        JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller != nullptr) {
            controller->startMicroscopeRuntime();
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetMicroscopeLifecyclePaused(
        JNIEnv* env, jclass, jlong handle, jboolean paused) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller != nullptr) {
            controller->setMicroscopeLifecyclePaused(paused == JNI_TRUE);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativePollMicroscopeRuntimePublication(
        JNIEnv* env, jclass, jlong handle, jlong lastRevision, jlong lastWorldEpoch,
        jlong lastEnvironmentRevision) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return nullptr;
        }
        const auto snapshot = controller->microscopeRuntimeSnapshot();
        if (snapshot.revision == 0 || snapshot.revision <= static_cast<std::uint64_t>(lastRevision)) {
            return nullptr;
        }
        const auto failure = controller->microscopePreparationFailureState();
        const jlong values[] = {
                static_cast<jlong>(snapshot.revision),
                static_cast<jlong>(snapshot.worldEpoch),
                static_cast<jlong>(snapshot.completedUpdateCount),
                static_cast<jlong>(snapshot.environmentRevision),
                static_cast<jlong>(snapshot.cellCount),
                doubleBits(snapshot.foodMass),
                doubleBits(snapshot.simulationTime),
                static_cast<jlong>(snapshot.temperatureMode),
                static_cast<jlong>(snapshot.toolOrdinal),
                static_cast<jlong>(snapshot.selectedCellId),
                snapshot.diagnosisRequested ? 1 : 0,
                snapshot.failed ? 1 : 0,
                static_cast<jlong>(failure[0]),
                static_cast<jlong>(failure[1]),
                static_cast<jlong>(failure[2]),
                static_cast<jlong>(failure[3]),
                static_cast<jlong>(snapshot.lastResolvedInputSequence),
                static_cast<jlong>(snapshot.appliedWorldRevision),
        };
        jlongArray status = env->NewLongArray(
                static_cast<jsize>(sizeof(values) / sizeof(values[0])));
        if (status == nullptr) {
            return nullptr;
        }
        env->SetLongArrayRegion(
                status, 0, static_cast<jsize>(sizeof(values) / sizeof(values[0])),
                values);

        const auto& rate = snapshot.rateTelemetry;
        const auto& scheduler = rate.scheduler;
        const bool hasRequestedTarget = scheduler.requestedTargetUps.has_value();
        const jlong rateValues[] = {
                kRateTelemetrySchemaVersion,
                static_cast<jlong>(scheduler.policy),
                hasRequestedTarget ? 1 : 0,
                hasRequestedTarget ? doubleBits(*scheduler.requestedTargetUps) : 0,
                static_cast<jlong>(rate.actualRateSampleSequence),
                rate.actualRateValid ? 1 : 0,
                doubleBits(rate.actualCompletedUps),
                doubleBits(rate.actualSimulationHoursPerSecond),
                static_cast<jlong>(rate.actualRateWindow.count()),
                static_cast<jlong>(rate.actualRateWindowUpdates),
                static_cast<jlong>(scheduler.policyGeneration),
                static_cast<jlong>(scheduler.learningResetCount),
                static_cast<jlong>(scheduler.plannedCycleCount),
                static_cast<jlong>(scheduler.observedBatchCount),
                static_cast<jlong>(scheduler.observedUpdateCount),
                static_cast<jlong>(scheduler.totalObservedBatchDuration.count()),
                static_cast<jlong>(scheduler.maximumObservedBatchDuration.count()),
                static_cast<jlong>(scheduler.lastObservedUpdateCount),
                static_cast<jlong>(scheduler.maximumObservedUpdateCount),
                static_cast<jlong>(scheduler.lastObservedBatchDuration.count()),
                static_cast<jlong>(scheduler.lastPlannedUpdateQuota),
                static_cast<jlong>(scheduler.lastClockCheckStride),
                scheduler.updateCostInitialized ? 1 : 0,
                static_cast<jlong>(scheduler.fastUpdateCost.count()),
                static_cast<jlong>(scheduler.slowUpdateCost.count()),
                static_cast<jlong>(scheduler.estimatedUpdateCost.count()),
                static_cast<jlong>(scheduler.quotaExceededCount),
                static_cast<jlong>(scheduler.deadlineOverrunCount),
                static_cast<jlong>(scheduler.maximumDeadlineOverrun.count()),
                static_cast<jlong>(scheduler.lastStopReason),
                static_cast<jlong>(rate.ownerServiceCount),
                static_cast<jlong>(rate.lastOwnerServiceInterval.count()),
                static_cast<jlong>(rate.maximumOwnerServiceInterval.count()),
                static_cast<jlong>(rate.publishedSourceFrameCount),
                static_cast<jlong>(rate.droppedSourceFrameCount),
                static_cast<jlong>(rate.publishedPreparedFrameCount),
                static_cast<jlong>(rate.droppedPreparedFrameCount),
                static_cast<jlong>(rate.statusPublishedCount),
                static_cast<jlong>(rate.statusConsumedCount),
                static_cast<jlong>(rate.statusProducerReplacedCount),
                static_cast<jlong>(rate.statusConsumerDiscardedCount),
                static_cast<jlong>(rate.statusPublishRetriesExhausted),
        };
        static_assert(sizeof(rateValues) / sizeof(rateValues[0]) == kRateTelemetryFieldCount);
        jlongArray rateTelemetry =
                env->NewLongArray(static_cast<jsize>(kRateTelemetryFieldCount));
        if (rateTelemetry == nullptr) {
            env->DeleteLocalRef(status);
            return nullptr;
        }
        env->SetLongArrayRegion(rateTelemetry, 0,
                static_cast<jsize>(kRateTelemetryFieldCount),
                rateValues);

        const bool environmentChanged =
                snapshot.worldEpoch != static_cast<std::uint64_t>(lastWorldEpoch) || snapshot.environmentRevision != static_cast<std::int64_t>(lastEnvironmentRevision);
        jbyteArray environment =
                environmentChanged && snapshot.environmentState != nullptr
                ? toJavaByteArray(env, *snapshot.environmentState)
                : nullptr;
        jbyteArray selectedCell =
                snapshot.selectedCellState != nullptr
                ? toJavaByteArray(env, *snapshot.selectedCellState)
                : nullptr;
        jbyteArray selectedGenome =
                snapshot.selectedGenomeState != nullptr
                ? toJavaByteArray(env, *snapshot.selectedGenomeState)
                : nullptr;

        jclass publicationClass = env->FindClass(
                "com/cytarch/cellularity/NativeMicroscopeRuntimePublication");
        if (publicationClass == nullptr) {
            return nullptr;
        }
        jmethodID constructor =
                env->GetMethodID(publicationClass, "<init>", "([J[J[B[B[B)V");
        jobject result = constructor == nullptr
                ? nullptr
                : env->NewObject(publicationClass, constructor, status,
                          rateTelemetry, environment,
                          selectedCell, selectedGenome);
        env->DeleteLocalRef(publicationClass);
        env->DeleteLocalRef(status);
        env->DeleteLocalRef(rateTelemetry);
        if (environment != nullptr) {
            env->DeleteLocalRef(environment);
        }
        if (selectedCell != nullptr) {
            env->DeleteLocalRef(selectedCell);
        }
        if (selectedGenome != nullptr) {
            env->DeleteLocalRef(selectedGenome);
        }
        return result;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMicroscopeRuntimeCurrentStats(
        JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return nullptr;
        }
        const auto snapshot = controller->inspectMicroscopeStats();
        const jlong values[] = {
                static_cast<jlong>(snapshot.cellCount),
                doubleBits(snapshot.foodMass),
                doubleBits(snapshot.simulationTime),
                static_cast<jlong>(snapshot.completedUpdateCount),
        };
        jlongArray result = env->NewLongArray(4);
        if (result != nullptr) {
            env->SetLongArrayRegion(result, 0, 4, values);
        }
        return result;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMicroscopeRuntimeSelectedGenomeDraft(
        JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return nullptr;
        }
        auto genome = controller->inspectMicroscopeSelectedGenome();
        return genome.has_value() ? toJavaByteArray(env, *genome) : nullptr;
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetMicroscopeTemperatureMode(
        JNIEnv* env, jclass, jlong endpointHandle, jint temperatureMode) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setMicroscopeTemperatureMode(temperatureMode);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetMicroscopeToolOrdinal(
        JNIEnv* env, jclass, jlong endpointHandle, jint toolOrdinal) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setMicroscopeToolOrdinal(toolOrdinal);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetMicroscopeSlowMotionSpeed(
        JNIEnv* env, jclass, jlong endpointHandle, jfloat slowMotionSpeed) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setMicroscopeSlowMotionSpeed(slowMotionSpeed);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetMicroscopeIncubateTargetUps(
        JNIEnv* env, jclass, jlong endpointHandle, jdouble targetUps) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setMicroscopeIncubateTargetUps(targetUps);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSetMicroscopePerformanceHudEnabled(
        JNIEnv* env, jclass, jlong endpointHandle, jboolean enabled) {
    try {
        auto controller = controllerFromRenderEndpoint(env, endpointHandle);
        if (controller != nullptr) {
            controller->setMicroscopePerformanceHudEnabled(enabled == JNI_TRUE);
        }
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativePublishMicroscopePlacementGenome(
        JNIEnv* env, jclass, jlong endpointHandle, jbyteArray genomeBytes) {
    try {
        if (genomeBytes == nullptr) {
            throw std::invalid_argument(
                    "Microscope placement genome payload is required");
        }
        ByteReader reader(readJavaBytes(
                env, genomeBytes, "Microscope placement genome draft"));
        auto genome = std::make_shared<const cellularity::GenomeData>(
                readGenomeDraft(reader));
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return 0;
        }
        const auto published = lease->publishPlacementGenome(
                std::move(genome));
        if (published.status
                        != cellularity::app::LeaseMutationStatus::Applied
                || !published.value.has_value()) {
            throw std::logic_error(
                    "Microscope placement genome publication failed");
        }
        return static_cast<jlong>(*published.value);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

void submitMicroscopeInputBatch(
        JNIEnv* env, jlong endpointHandle, std::uint64_t surfaceToken,
        jobject inputBuffer, jobject resultBuffer,
        std::uint64_t expectedPlacementGenomeRevision) {
    using cellularity::microscope::MicroscopeInputBatch;
    using cellularity::microscope::MicroscopeInputBatchResult;
    using cellularity::microscope::MicroscopeInputIngressStatus;

    std::uint8_t* resultBytes = requireDirectOutputBuffer(
            env,
            resultBuffer,
            cellularity::microscope::kMicroscopeInputResultBufferCapacity,
            "Microscope input result buffer must be writable, direct, and at least 320 bytes");
    if (resultBytes == nullptr) {
        return;
    }

    std::uint64_t authoritativeEpoch = 0;
    MicroscopeInputBatchResult result;
    cellularity::microscope::initializeMicroscopeInputBatchResult(
            result, 0, authoritativeEpoch,
            MicroscopeInputIngressStatus::Malformed);
    cellularity::microscope::writeMicroscopeInputBatchResult(
            resultBytes, result);

    std::shared_ptr<NativeCellularityController> controller;
    std::optional<cellularity::app::InputBatchGuard> inputGuard;
    bool dispatchStarted = false;
    try {
        if (inputBuffer == nullptr) {
            return;
        }
        const void* inputAddress = env->GetDirectBufferAddress(inputBuffer);
        const jlong inputCapacity = env->GetDirectBufferCapacity(inputBuffer);
        if (inputAddress == nullptr || inputCapacity < 0) {
            return;
        }

        const auto inputByteCount = static_cast<std::size_t>(inputCapacity);
        result.batchSequence =
                cellularity::microscope::readMicroscopeInputBatchSequence(
                        static_cast<const std::uint8_t*>(inputAddress),
                        inputByteCount);
        cellularity::microscope::writeMicroscopeInputBatchResult(
                resultBytes, result);

        MicroscopeInputBatch batch;
        if (!cellularity::microscope::decodeMicroscopeInputBatch(
                    static_cast<const std::uint8_t*>(inputAddress),
                    inputByteCount, batch)) {
            return;
        }
        authoritativeEpoch = batch.expectedInputEpoch;
        cellularity::microscope::initializeMicroscopeInputBatchResult(
                result, batch.sequence, authoritativeEpoch,
                MicroscopeInputIngressStatus::Malformed);

        if ((batch.tapCount == 0 && expectedPlacementGenomeRevision != 0)
                || (batch.tapCount == 1
                        && expectedPlacementGenomeRevision == 0)) {
            cellularity::microscope::writeMicroscopeInputBatchResult(
                    resultBytes, result);
            return;
        }

        if (endpointHandle == 0) {
            result.ingressStatus = MicroscopeInputIngressStatus::Closing;
            cellularity::microscope::writeMicroscopeInputBatchResult(
                    resultBytes, result);
            return;
        }
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            result.ingressStatus = MicroscopeInputIngressStatus::Closing;
            cellularity::microscope::writeMicroscopeInputBatchResult(
                    resultBytes, result);
            return;
        }
        auto admission = lease->tryAcquireInput(
                surfaceToken, authoritativeEpoch);
        result.inputEpoch = admission.currentInputEpoch;
        switch (admission.status) {
            case cellularity::app::InputBatchAdmissionStatus::Accepted:
                inputGuard = std::move(admission.guard);
                break;
            case cellularity::app::InputBatchAdmissionStatus::Resyncing:
                result.ingressStatus = MicroscopeInputIngressStatus::Resyncing;
                break;
            case cellularity::app::InputBatchAdmissionStatus::StaleEpoch:
                result.ingressStatus =
                        MicroscopeInputIngressStatus::StaleInputEpoch;
                break;
            case cellularity::app::InputBatchAdmissionStatus::InactiveSurface:
                result.ingressStatus =
                        MicroscopeInputIngressStatus::InactiveSurface;
                break;
            case cellularity::app::InputBatchAdmissionStatus::AttachmentClosing:
                result.ingressStatus = MicroscopeInputIngressStatus::Closing;
                break;
        }
        if (!inputGuard.has_value()) {
            cellularity::microscope::writeMicroscopeInputBatchResult(
                    resultBytes, result);
            return;
        }
        controller = inputGuard->controllerShared();
        if (controller == nullptr) {
            result.ingressStatus = MicroscopeInputIngressStatus::Closing;
            cellularity::microscope::writeMicroscopeInputBatchResult(
                    resultBytes, result);
            return;
        }
        std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                tapGenome;
        bool rejectTapForStalePlacementGenome = false;
        if (batch.tapCount == 1) {
            tapGenome = lease->acquirePlacementGenome(
                    expectedPlacementGenomeRevision);
            rejectTapForStalePlacementGenome = tapGenome == nullptr;
        }
        dispatchStarted = true;
        controller->submitMicroscopeInputBatch(
                batch, authoritativeEpoch, std::move(tapGenome), result,
                rejectTapForStalePlacementGenome);
        if (result.ingressStatus == MicroscopeInputIngressStatus::Applied
                && (result.aggregateFlags
                           & cellularity::microscope::
                                   kMicroscopeInputAggregateCameraChanged)
                        != 0
                && batch.profilingIngressTimestampNanos > 0) {
            const auto workerIdentity =
                    lease->vulkanProfilingWorkerIdentity();
            if (workerIdentity.has_value()) {
                cellularity::app::render_worker::
                        registerNativeRenderCameraInput(
                                *workerIdentity,
                                batch.profilingIngressTimestampNanos);
            }
        }
        cellularity::microscope::writeMicroscopeInputBatchResult(
                resultBytes, result);
    } catch (...) {
        if (controller != nullptr) {
            controller->closeMicroscopeInputAdmission();
        }
        cellularity::microscope::markMicroscopeInputBatchInternalFailure(
                result, dispatchStarted);
        cellularity::microscope::writeMicroscopeInputBatchResult(
                resultBytes, result);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeSubmitMicroscopeInputBatch(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken,
        jobject inputBuffer, jobject resultBuffer,
        jlong expectedPlacementGenomeRevision) {
    submitMicroscopeInputBatch(env, endpointHandle,
            static_cast<std::uint64_t>(surfaceToken), inputBuffer,
            resultBuffer,
            static_cast<std::uint64_t>(expectedPlacementGenomeRevision));
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeReadMicroscopePresentationMotionSnapshot(
        JNIEnv* env, jclass, jlong endpointHandle, jobject resultBuffer) {
    std::uint8_t* resultBytes = requireDirectOutputBuffer(
            env,
            resultBuffer,
            cellularity::microscope::kPresentationMotionSnapshotSize,
            "Presentation motion result buffer must be writable, direct, and at least 32 bytes");
    if (resultBytes == nullptr) {
        return;
    }
    try {
        if (endpointHandle == 0) {
            throw std::invalid_argument(
                    "Native microscope render endpoint handle is null");
        }
        const auto controller = controllerFromRenderEndpoint(
                env, endpointHandle);
        if (controller == nullptr) {
            throw std::logic_error(
                    "Native microscope render controller is unavailable");
        }
        cellularity::microscope::writePresentationMotionSnapshot(
                resultBytes,
                controller->microscopePresentationMotionSnapshot());
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeRequestMicroscopeInputResync(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken) {
    if (endpointHandle == 0 || surfaceToken == 0) {
        return JNI_FALSE;
    }
    auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
    if (lease == nullptr) {
        return JNI_FALSE;
    }
    const auto resync = lease->requestInputResync(
            static_cast<std::uint64_t>(surfaceToken));
    if (resync != cellularity::app::LeaseMutationStatus::Applied
            && resync
                    != cellularity::app::LeaseMutationStatus::AlreadyAuthorized) {
        return JNI_FALSE;
    }
    const auto controller = lease->acquireControllerForColdCall();
    return controller != nullptr && controller->requestMicroscopeInputResync()
            ? JNI_TRUE
            : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMicroscopeActiveInputEpoch(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken) {
    if (endpointHandle == 0 || surfaceToken == 0)
        return 0;
    auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
    return lease == nullptr
            ? 0
            : static_cast<jlong>(lease->activeInputEpoch(
                      static_cast<std::uint64_t>(surfaceToken)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCompleteMicroscopeInputResync(
        JNIEnv* env, jclass, jlong endpointHandle, jlong surfaceToken,
        jboolean activePointersDrained) {
    try {
        auto lease = renderEndpointLeaseFromHandle(env, endpointHandle);
        if (lease == nullptr) {
            return 0;
        }
        const auto status = lease->completeInputResync(
                static_cast<std::uint64_t>(surfaceToken),
                activePointersDrained == JNI_TRUE);
        if (status != cellularity::app::LeaseMutationStatus::Applied
                && status
                        != cellularity::app::LeaseMutationStatus::AlreadyAuthorized) {
            return 0;
        }
        return static_cast<jlong>(lease->generationSnapshot().inputEpoch);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeMicroscopeActivePointerGenerationCount(
        JNIEnv* env, jclass, jlong endpointHandle) {
    if (endpointHandle == 0) {
        return 0;
    }
    const auto controller = controllerFromRenderEndpoint(env, endpointHandle);
    if (controller == nullptr) {
        return 0;
    }
    return static_cast<jint>(
            controller->microscopeActivePointerGenerationCount());
}
