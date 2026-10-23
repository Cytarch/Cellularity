package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureResult
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureStatus

internal interface SubstratePreviewCaptureEndpoint {
    fun enqueueSubstratePreviewCapture(
        requestId: Long,
        substratePreviewToken: Long
    ): NativePreviewCaptureRequest?
}

/** Owns save request identity and timeout; capture execution remains native-owned. */
internal class SubstratePreviewCaptureCoordinator(
    private val timeoutMillis: Long = CAPTURE_TIMEOUT_MILLIS
) {
    private val lock = Any()
    private var endpoint: SubstratePreviewCaptureEndpoint? = null
    private var endpointEpoch = 0L
    private var nextRequestId = 1L

    init {
        require(timeoutMillis > 0L) { "Substrate preview capture timeout must be positive" }
    }

    fun attach(candidate: SubstratePreviewCaptureEndpoint) {
        synchronized(lock) {
            endpoint = candidate
            endpointEpoch = incrementEpoch(endpointEpoch)
        }
    }

    fun detach(candidate: SubstratePreviewCaptureEndpoint) {
        synchronized(lock) {
            if (endpoint === candidate) {
                endpoint = null
                endpointEpoch = incrementEpoch(endpointEpoch)
            }
        }
    }

    fun capture(substratePreviewToken: Long): NativePreviewCaptureResult? {
        if (substratePreviewToken == 0L) return null
        val admission = synchronized(lock) {
            val current = endpoint ?: return null
            val requestId = takeRequestIdLocked() ?: return null
            CaptureAdmission(current, endpointEpoch, requestId)
        }
        val request = admission.endpoint.enqueueSubstratePreviewCapture(
            requestId = admission.requestId,
            substratePreviewToken = substratePreviewToken
        ) ?: return null
        return try {
            val result = request.await(timeoutMillis)
            if (result.status == NativePreviewCaptureStatus.TIMED_OUT) {
                request.cancel()
            }
            val stillCurrent = synchronized(lock) {
                endpoint === admission.endpoint && endpointEpoch == admission.endpointEpoch
            }
            result.takeIf {
                stillCurrent &&
                    it.requestId == admission.requestId &&
                    it.status == NativePreviewCaptureStatus.COMPLETED &&
                    it.payload != null
            }
        } finally {
            request.close()
        }
    }

    private fun takeRequestIdLocked(): Long? {
        val requestId = nextRequestId
        if (requestId <= 0L) return null
        nextRequestId = if (requestId == Long.MAX_VALUE) 0L else requestId + 1L
        return requestId
    }

    private data class CaptureAdmission(
        val endpoint: SubstratePreviewCaptureEndpoint,
        val endpointEpoch: Long,
        val requestId: Long
    )

    private companion object {
        const val CAPTURE_TIMEOUT_MILLIS = 5_000L

        fun incrementEpoch(value: Long): Long =
            if (value == Long.MAX_VALUE) 1L else value + 1L
    }
}
