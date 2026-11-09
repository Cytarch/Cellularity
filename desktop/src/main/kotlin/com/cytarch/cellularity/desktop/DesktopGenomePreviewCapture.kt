/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/** A captured genome picture: tightly packed straight-alpha RGBA. */
class CapturedGenomePreview(
    val rgba: ByteArray,
    val pixelWidth: Int,
    val pixelHeight: Int
)

/** The thread-owned preview session a capture runs on (the editor's preview surface). */
fun interface GenomePreviewCaptureEndpoint {
    fun enqueueCapture(
        genomeDraftBytes: ByteArray,
        maximumSimulationTime: Double,
        completion: CompletableFuture<CapturedGenomePreview?>
    ): Boolean
}

/**
 * The picture a saved genome carries, taken the way Android's GenomePreviewCaptureCoordinator
 * takes it: the editor's preview session grows the genome to the time limit off-screen and
 * hands back the frame. The session belongs to its own thread, so a save enqueues the request
 * there and waits, bounded, for the answer; no endpoint or no answer means no picture.
 */
class DesktopGenomePreviewCapture(
    private val timeoutMillis: Long = CAPTURE_TIMEOUT_MILLIS
) {
    @Volatile
    private var endpoint: GenomePreviewCaptureEndpoint? = null

    fun attach(candidate: GenomePreviewCaptureEndpoint) {
        endpoint = candidate
    }

    fun detach(candidate: GenomePreviewCaptureEndpoint) {
        if (endpoint === candidate) endpoint = null
    }

    fun capture(genomeDraftBytes: ByteArray, maximumSimulationTime: Double): CapturedGenomePreview? {
        if (genomeDraftBytes.isEmpty()) return null
        if (!maximumSimulationTime.isFinite() || maximumSimulationTime < 0.0) return null
        val current = endpoint ?: return null
        val completion = CompletableFuture<CapturedGenomePreview?>()
        if (!current.enqueueCapture(genomeDraftBytes, maximumSimulationTime, completion)) return null
        return try {
            completion.get(timeoutMillis, TimeUnit.MILLISECONDS)
        } catch (timedOut: TimeoutException) {
            completion.cancel(false)
            null
        }
    }

    private companion object {
        const val CAPTURE_TIMEOUT_MILLIS = 30_000L // Android's timeout
    }
}
