package com.cytarch.cellularity.platform.render.preview

internal interface GenomePreviewCaptureEndpoint {
    fun enqueueGenomePreviewCapture(
        requestId: Long,
        genomeDraftBytes: ByteArray,
        maximumSimulationTime: Double
    ): NativePreviewCaptureRequest?
}

/** Owns file-save request identity and timeout; rendering remains native-owned. */
internal class GenomePreviewCaptureCoordinator(
    private val timeoutMillis: Long = CAPTURE_TIMEOUT_MILLIS
) {
    private val lock = Any()
    private var endpoint: GenomePreviewCaptureEndpoint? = null
    private var endpointEpoch = 0L
    private var nextRequestId = 1L

    init {
        require(timeoutMillis > 0L) { "Genome preview capture timeout must be positive" }
    }

    fun attach(candidate: GenomePreviewCaptureEndpoint) {
        synchronized(lock) {
            endpoint = candidate
            endpointEpoch = incrementEpoch(endpointEpoch)
        }
    }

    fun detach(candidate: GenomePreviewCaptureEndpoint) {
        synchronized(lock) {
            if (endpoint === candidate) {
                endpoint = null
                endpointEpoch = incrementEpoch(endpointEpoch)
            }
        }
    }

    fun capture(
        genomeDraftBytes: ByteArray,
        maximumSimulationTime: Double
    ): NativePreviewCaptureResult? {
        if (genomeDraftBytes.isEmpty()) return null
        if (!maximumSimulationTime.isFinite() || maximumSimulationTime < 0.0) return null
        val admission = synchronized(lock) {
            val current = endpoint ?: return null
            val requestId = takeRequestIdLocked() ?: return null
            CaptureAdmission(current, endpointEpoch, requestId)
        }
        val request = admission.endpoint.enqueueGenomePreviewCapture(
            requestId = admission.requestId,
            genomeDraftBytes = genomeDraftBytes,
            maximumSimulationTime = maximumSimulationTime
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
        val endpoint: GenomePreviewCaptureEndpoint,
        val endpointEpoch: Long,
        val requestId: Long
    )

    private companion object {
        const val CAPTURE_TIMEOUT_MILLIS = 30_000L

        fun incrementEpoch(value: Long): Long =
            if (value == Long.MAX_VALUE) 1L else value + 1L
    }
}
