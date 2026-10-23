#include <jni.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

#include "NativeJniUtils.h"
#include "NativePreviewCaptureRequestHandle.h"
#include "NativePreviewEndpointHandle.h"
#include "NativeRenderEndpointHandle.h"
#include "app/NativeCellularityController.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "app/microscope/SubstratePreviewCaptureEndpoint.h"
#include "app/render/NativeRenderAttachmentLease.h"
#include "app/render/preview/NativePreviewCaptureLayouts.h"
#include "formats/GenomeDraftCodec.h"

namespace {

using cellularity::app::preview::PreviewCaptureAdmissionStatus;
using cellularity::app::preview::PreviewCaptureRequestKind;
using cellularity::app::preview::PreviewCaptureResultHeader;
using cellularity::app::preview::PreviewCaptureStatus;

std::uint8_t* requireDirectBuffer(JNIEnv* env, jobject buffer,
        std::size_t minimumBytes, const char* message) {
    if (buffer == nullptr) {
        throwJavaException(
                env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, message);
        return nullptr;
    }
    jclass bufferClass = env->GetObjectClass(buffer);
    if (bufferClass == nullptr) {
        return nullptr;
    }
    const jmethodID isReadOnlyMethod =
            env->GetMethodID(bufferClass, "isReadOnly", "()Z");
    if (isReadOnlyMethod == nullptr) {
        env->DeleteLocalRef(bufferClass);
        return nullptr;
    }
    const jboolean isReadOnly =
            env->CallBooleanMethod(buffer, isReadOnlyMethod);
    env->DeleteLocalRef(bufferClass);
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    void* address = env->GetDirectBufferAddress(buffer);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (isReadOnly == JNI_TRUE
            || address == nullptr
            || capacity < 0
            || static_cast<std::uint64_t>(capacity) < minimumBytes) {
        throwJavaException(
                env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, message);
        return nullptr;
    }
    return static_cast<std::uint8_t*>(address);
}

void writeAdmission(std::uint8_t* destination,
        PreviewCaptureAdmissionStatus status,
        std::uint64_t requestHandle = 0) noexcept {
    const auto encoded =
            cellularity::app::preview::encodePreviewCaptureAdmission(
                    {status, requestHandle});
    std::memcpy(destination, encoded.data(), encoded.size());
}

void writeResult(std::uint8_t* destination,
        const PreviewCaptureResultHeader& result) noexcept {
    const auto encoded =
            cellularity::app::preview::encodePreviewCaptureResultHeader(
                    result);
    std::memcpy(destination, encoded.data(), encoded.size());
}

bool hasGenomeDraftHeader(JNIEnv* env, jbyteArray bytes) {
    if (bytes == nullptr
            || env->GetArrayLength(bytes)
                    < static_cast<jsize>(
                            cellularity::kGenomeDraftWireHeaderBytes)) {
        return false;
    }
    std::array<std::uint8_t, cellularity::kGenomeDraftWireHeaderBytes>
            header{};
    env->GetByteArrayRegion(bytes, 0,
            static_cast<jsize>(header.size()),
            reinterpret_cast<jbyte*>(header.data()));
    return !env->ExceptionCheck()
            && cellularity::hasGenomeDraftWireHeader(
                    header.data(), header.size());
}

PreviewCaptureAdmissionStatus mapGenomeAdmission(
        cellularity::app::genome_preview::
                GenomePreviewCaptureAdmissionStatus status) noexcept {
    using Source = cellularity::app::genome_preview::
            GenomePreviewCaptureAdmissionStatus;
    switch (status) {
        case Source::Accepted:
            return PreviewCaptureAdmissionStatus::Accepted;
        case Source::Busy:
            return PreviewCaptureAdmissionStatus::Busy;
        case Source::InvalidArgument:
            return PreviewCaptureAdmissionStatus::InvalidArgument;
        case Source::Closing:
            return PreviewCaptureAdmissionStatus::Closing;
        case Source::BudgetExceeded:
            return PreviewCaptureAdmissionStatus::BudgetExceeded;
        case Source::DuplicateRequestId:
            return PreviewCaptureAdmissionStatus::DuplicateRequestId;
        case Source::SequenceExhausted:
        case Source::InternalFailure:
            return PreviewCaptureAdmissionStatus::InternalFailure;
    }
    return PreviewCaptureAdmissionStatus::InternalFailure;
}

PreviewCaptureAdmissionStatus mapSubstrateAdmission(
        cellularity::app::microscope::
                SubstratePreviewCaptureAdmissionStatus status) noexcept {
    using Source = cellularity::app::microscope::
            SubstratePreviewCaptureAdmissionStatus;
    switch (status) {
        case Source::Accepted:
            return PreviewCaptureAdmissionStatus::Accepted;
        case Source::Busy:
            return PreviewCaptureAdmissionStatus::Busy;
        case Source::InvalidArgument:
            return PreviewCaptureAdmissionStatus::InvalidArgument;
        case Source::Closing:
            return PreviewCaptureAdmissionStatus::Closing;
        case Source::DuplicateRequestId:
            return PreviewCaptureAdmissionStatus::DuplicateRequestId;
        case Source::SequenceExhausted:
        case Source::InternalFailure:
            return PreviewCaptureAdmissionStatus::InternalFailure;
    }
    return PreviewCaptureAdmissionStatus::InternalFailure;
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewCaptureBridge_nativeEnqueueGenome(
        JNIEnv* env, jclass, jlong endpointHandle, jlong requestId,
        jbyteArray genomeDraftBytes, jdouble maximumSimulationTime,
        jobject admissionBuffer) {
    std::uint8_t* admissionBytes = requireDirectBuffer(env, admissionBuffer,
            cellularity::app::preview::kPreviewCaptureAdmissionSize,
            "Preview capture admission buffer must be direct and at least 24 bytes");
    if (admissionBytes == nullptr || env->ExceptionCheck()) {
        return;
    }
    std::memset(admissionBytes, 0,
            cellularity::app::preview::kPreviewCaptureAdmissionSize);
    writeAdmission(
            admissionBytes, PreviewCaptureAdmissionStatus::InvalidArgument);
    if (requestId <= 0
            || !hasGenomeDraftHeader(env, genomeDraftBytes)
            || env->ExceptionCheck()) {
        return;
    }
    const jsize length = env->GetArrayLength(genomeDraftBytes);
    if (length <= 0
            || static_cast<std::size_t>(length)
                    > cellularity::kGenomeDraftMaximumWireBytes) {
        return;
    }

    std::unique_ptr<NativePreviewCaptureRequestHandle> wrapper;
    try {
        wrapper = std::make_unique<NativePreviewCaptureRequestHandle>();
    } catch (const std::bad_alloc&) {
        writeAdmission(
                admissionBytes, PreviewCaptureAdmissionStatus::InternalFailure);
        return;
    }

    jbyte* pinned = env->GetByteArrayElements(genomeDraftBytes, nullptr);
    if (pinned == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        writeAdmission(
                admissionBytes, PreviewCaptureAdmissionStatus::InternalFailure);
        return;
    }
    try {
        const auto acquired = acquireGenomePreviewEndpoint(
                static_cast<std::uint64_t>(endpointHandle),
                nativePreviewEndpointGeneration(
                        static_cast<std::uint64_t>(endpointHandle)));
        const auto endpoint = acquired.endpoint;
        const auto admission = endpoint != nullptr
                ? endpoint->enqueueFileCapture(
                          static_cast<std::uint64_t>(requestId),
                          reinterpret_cast<const std::uint8_t*>(pinned),
                          static_cast<std::size_t>(length),
                          static_cast<double>(maximumSimulationTime),
                          endpoint->generation(),
                          endpoint->activeAttachmentGeneration())
                : cellularity::app::genome_preview::
                          GenomePreviewCaptureAdmission{
                                  cellularity::app::genome_preview::
                                          GenomePreviewCaptureAdmissionStatus::
                                                  Closing,
                                  nullptr
                          };
        env->ReleaseByteArrayElements(
                genomeDraftBytes, pinned, JNI_ABORT);
        const auto status = mapGenomeAdmission(admission.status);
        if (status != PreviewCaptureAdmissionStatus::Accepted
                || admission.request == nullptr) {
            writeAdmission(admissionBytes, status);
            return;
        }
        wrapper->request = admission.request;
        wrapper->kind = PreviewCaptureRequestKind::Genome;
        const jlong handleValue =
                previewCaptureRequestHandleToJlong(wrapper.get());
        writeAdmission(admissionBytes,
                PreviewCaptureAdmissionStatus::Accepted,
                static_cast<std::uint64_t>(handleValue));
        (void)wrapper.release();
    } catch (const std::bad_alloc&) {
        env->ReleaseByteArrayElements(
                genomeDraftBytes, pinned, JNI_ABORT);
        writeAdmission(
                admissionBytes, PreviewCaptureAdmissionStatus::InternalFailure);
    } catch (const std::exception&) {
        env->ReleaseByteArrayElements(
                genomeDraftBytes, pinned, JNI_ABORT);
        writeAdmission(
                admissionBytes, PreviewCaptureAdmissionStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewCaptureBridge_nativeEnqueueSubstrate(
        JNIEnv* env, jclass, jlong endpointHandle, jlong requestId,
        jlong substratePreviewToken, jlong attachmentGeneration,
        jobject admissionBuffer) {
    std::uint8_t* admissionBytes = requireDirectBuffer(env, admissionBuffer,
            cellularity::app::preview::kPreviewCaptureAdmissionSize,
            "Preview capture admission buffer must be direct and at least 24 bytes");
    if (admissionBytes == nullptr || env->ExceptionCheck()) {
        return;
    }
    std::memset(admissionBytes, 0,
            cellularity::app::preview::kPreviewCaptureAdmissionSize);
    writeAdmission(
            admissionBytes, PreviewCaptureAdmissionStatus::InvalidArgument);
    if (requestId <= 0
            || substratePreviewToken == 0
            || attachmentGeneration == 0) {
        return;
    }

    std::unique_ptr<NativePreviewCaptureRequestHandle> wrapper;
    try {
        wrapper = std::make_unique<NativePreviewCaptureRequestHandle>();
        auto lease = renderEndpointLeaseFromHandle(
                static_cast<std::uint64_t>(endpointHandle));
        auto controller = lease != nullptr
                ? lease->acquireControllerForColdCall()
                : nullptr;
        const auto endpoint = controller != nullptr
                ? controller->substratePreviewCaptureEndpoint()
                : nullptr;
        const auto admission = endpoint != nullptr
                ? endpoint->enqueue(
                          static_cast<std::uint64_t>(requestId),
                          static_cast<std::uint64_t>(substratePreviewToken),
                          static_cast<std::uint64_t>(attachmentGeneration))
                : cellularity::app::microscope::
                          SubstratePreviewCaptureAdmission{
                                  cellularity::app::microscope::
                                          SubstratePreviewCaptureAdmissionStatus::
                                                  Closing,
                                  nullptr
                          };
        const auto status = mapSubstrateAdmission(admission.status);
        if (status != PreviewCaptureAdmissionStatus::Accepted
                || admission.request == nullptr) {
            writeAdmission(admissionBytes, status);
            return;
        }
        wrapper->request = admission.request;
        wrapper->kind = PreviewCaptureRequestKind::Substrate;
        const jlong handleValue =
                previewCaptureRequestHandleToJlong(wrapper.get());
        writeAdmission(admissionBytes,
                PreviewCaptureAdmissionStatus::Accepted,
                static_cast<std::uint64_t>(handleValue));
        (void)wrapper.release();
    } catch (const std::bad_alloc&) {
        writeAdmission(
                admissionBytes, PreviewCaptureAdmissionStatus::InternalFailure);
    } catch (const std::exception&) {
        writeAdmission(
                admissionBytes, PreviewCaptureAdmissionStatus::InvalidArgument);
    }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_platform_render_preview_NativePreviewCaptureBridge_nativeAwait(
        JNIEnv* env, jclass, jlong requestHandle, jlong timeoutMillis,
        jobject resultBuffer) {
    std::uint8_t* resultBytes = requireDirectBuffer(env, resultBuffer,
            cellularity::app::preview::kPreviewCaptureResultSize,
            "Preview capture result buffer must be direct and at least 56 bytes");
    if (resultBytes == nullptr || env->ExceptionCheck()) {
        return nullptr;
    }
    if (timeoutMillis <= 0) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION,
                "Preview capture timeout must be positive");
        return nullptr;
    }
    std::memset(resultBytes, 0,
            cellularity::app::preview::kPreviewCaptureResultSize);
    PreviewCaptureResultHeader initial;
    initial.status = PreviewCaptureStatus::InternalFailure;
    writeResult(resultBytes, initial);

    NativePreviewCaptureRequestHandle* handle =
            previewCaptureRequestHandleFromJlong(requestHandle);
    if (handle == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_STATE_EXCEPTION,
                "Preview capture request handle is null");
        return nullptr;
    }

    std::unique_lock<std::mutex> handleLock(handle->mutex);
    if (handle->closed || handle->request == nullptr) {
        return nullptr;
    }
    const auto ticket = handle->request->await(
            std::chrono::milliseconds(timeoutMillis));
    writeResult(resultBytes, ticket.header);
    if (ticket.header.status != PreviewCaptureStatus::Completed) {
        if (ticket.deliveryToken != 0) {
            (void)handle->request->consumeDelivery(ticket.deliveryToken);
        }
        return nullptr;
    }
    if (ticket.payload == nullptr
            || !cellularity::app::preview::
                    validateCompletedPreviewCaptureResult(
                            ticket.header, ticket.payload->bytes.size())
            || ticket.payload->bytes.size()
                    > static_cast<std::size_t>(
                            std::numeric_limits<jsize>::max())) {
        handle->request->abandonDelivery(ticket.deliveryToken);
        PreviewCaptureResultHeader failure;
        failure.requestId = ticket.header.requestId;
        failure.status = PreviewCaptureStatus::InternalFailure;
        writeResult(resultBytes, failure);
        return nullptr;
    }
    jbyteArray payload = env->NewByteArray(
            static_cast<jsize>(ticket.payload->bytes.size()));
    if (payload == nullptr) {
        handle->request->abandonDelivery(ticket.deliveryToken);
        return nullptr;
    }
    if (!ticket.payload->bytes.empty()) {
        env->SetByteArrayRegion(payload, 0,
                static_cast<jsize>(ticket.payload->bytes.size()),
                reinterpret_cast<const jbyte*>(
                        ticket.payload->bytes.data()));
    }
    if (env->ExceptionCheck()) {
        handle->request->abandonDelivery(ticket.deliveryToken);
        return nullptr;
    }
    if (!handle->request->consumeDelivery(ticket.deliveryToken)) {
        PreviewCaptureResultHeader failure;
        failure.requestId = ticket.header.requestId;
        failure.status = PreviewCaptureStatus::InternalFailure;
        writeResult(resultBytes, failure);
        return nullptr;
    }
    return payload;
}
