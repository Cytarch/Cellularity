package com.cytarch.cellularity.platform.render.preview

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomePreviewCaptureCoordinatorTest {
    @Test
    fun routesOneRequestToTheActiveNativeEndpoint() {
        val genome = byteArrayOf(4, 3, 2, 1)
        val expected = byteArrayOf(9, 8, 7, 6)
        val request = FakeRequest(completedResult(1, expected))
        val endpoint = RecordingEndpoint(request)
        val coordinator = GenomePreviewCaptureCoordinator(timeoutMillis = 100)
        coordinator.attach(endpoint)

        val result = requireNotNull(coordinator.capture(genome, 48.0))

        assertArrayEquals(expected, result.payload)
        assertSame(genome, endpoint.lastGenome)
        assertEquals(48.0, endpoint.lastMaximumTime, 0.0)
        assertEquals(1L, endpoint.lastRequestId)
        assertEquals(1, request.closeCount)
    }

    @Test
    fun absentDetachedOrInvalidRequestProducesNoPreview() {
        val endpoint = RecordingEndpoint(FakeRequest(completedResult(1, byteArrayOf(1))))
        val coordinator = GenomePreviewCaptureCoordinator(timeoutMillis = 100)

        assertNull(coordinator.capture(byteArrayOf(1), 1.0))
        coordinator.attach(endpoint)
        assertNull(coordinator.capture(byteArrayOf(), 1.0))
        assertNull(coordinator.capture(byteArrayOf(1), Double.NaN))
        coordinator.detach(endpoint)
        assertNull(coordinator.capture(byteArrayOf(1), 1.0))
    }

    @Test
    fun timeoutIsExplicitlyCancelledAndClosed() {
        val request = FakeRequest(
            NativePreviewCaptureResult(
                requestId = 1,
                status = NativePreviewCaptureStatus.TIMED_OUT
            )
        )
        val coordinator = GenomePreviewCaptureCoordinator(timeoutMillis = 1)
        coordinator.attach(RecordingEndpoint(request))

        assertNull(coordinator.capture(byteArrayOf(1), 48.0))
        assertEquals(1, request.cancelCount)
        assertEquals(1, request.closeCount)
    }

    @Test
    fun completionFromDetachedEndpointIsIgnored() {
        lateinit var coordinator: GenomePreviewCaptureCoordinator
        lateinit var endpoint: GenomePreviewCaptureEndpoint
        val request = FakeRequest(completedResult(1, byteArrayOf(1))) {
            coordinator.detach(endpoint)
        }
        endpoint = RecordingEndpoint(request)
        coordinator = GenomePreviewCaptureCoordinator(timeoutMillis = 100)
        coordinator.attach(endpoint)

        assertNull(coordinator.capture(byteArrayOf(1), 1.0))
        assertTrue(request.closeCount == 1)
    }

    private class RecordingEndpoint(
        private val request: NativePreviewCaptureRequest
    ) : GenomePreviewCaptureEndpoint {
        var lastGenome: ByteArray? = null
        var lastMaximumTime = Double.NaN
        var lastRequestId = 0L

        override fun enqueueGenomePreviewCapture(
            requestId: Long,
            genomeDraftBytes: ByteArray,
            maximumSimulationTime: Double
        ): NativePreviewCaptureRequest {
            lastRequestId = requestId
            lastGenome = genomeDraftBytes
            lastMaximumTime = maximumSimulationTime
            return request
        }
    }

    private class FakeRequest(
        private val result: NativePreviewCaptureResult,
        private val beforeAwaitReturn: () -> Unit = {}
    ) : NativePreviewCaptureRequest {
        var cancelCount = 0
        var closeCount = 0

        override fun await(timeoutMillis: Long): NativePreviewCaptureResult {
            beforeAwaitReturn()
            return result
        }

        override fun cancel(): NativePreviewCaptureCancelResult {
            cancelCount += 1
            return NativePreviewCaptureCancelResult.CANCELLATION_REQUESTED
        }

        override fun close() {
            closeCount += 1
        }
    }

    private fun completedResult(
        requestId: Long,
        payload: ByteArray
    ) = NativePreviewCaptureResult(
        requestId = requestId,
        status = NativePreviewCaptureStatus.COMPLETED,
        width = 1,
        height = 1,
        rowStrideBytes = 4,
        pixelFormat = NativePreviewPixelFormat.RGBA8,
        origin = NativePreviewOrigin.TOP_LEFT,
        alphaMode = NativePreviewAlphaMode.STRAIGHT,
        payload = payload
    )
}
