package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.platform.render.preview.NativePreviewAlphaMode
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureCancelResult
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureResult
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureStatus
import com.cytarch.cellularity.platform.render.preview.NativePreviewOrigin
import com.cytarch.cellularity.platform.render.preview.NativePreviewPixelFormat
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SubstratePreviewCaptureCoordinatorTest {
    @Test
    fun routesOneTokenToTheActiveNativeEndpoint() {
        val expected = byteArrayOf(1, 2, 3, 4)
        val request = FakeRequest(completedResult(1, expected))
        val endpoint = RecordingEndpoint(request)
        val coordinator = SubstratePreviewCaptureCoordinator(timeoutMillis = 100)
        coordinator.attach(endpoint)

        val result = requireNotNull(coordinator.capture(17))

        assertArrayEquals(expected, result.payload)
        assertEquals(17L, endpoint.lastToken)
        assertEquals(1L, endpoint.lastRequestId)
        assertEquals(1, request.closeCount)
    }

    @Test
    fun preservesRawUint64TokenBitsAcrossTheManagedBoundary() {
        val request = FakeRequest(completedResult(1, byteArrayOf(1, 2, 3, 4)))
        val endpoint = RecordingEndpoint(request)
        val coordinator = SubstratePreviewCaptureCoordinator(timeoutMillis = 100)
        coordinator.attach(endpoint)

        val result = coordinator.capture(Long.MIN_VALUE)

        assertEquals(NativePreviewCaptureStatus.COMPLETED, result?.status)
        assertEquals(Long.MIN_VALUE, endpoint.lastToken)
    }

    @Test
    fun absentOrDetachedSurfaceProducesNoPreview() {
        val endpoint = RecordingEndpoint(FakeRequest(completedResult(1, byteArrayOf(1))))
        val coordinator = SubstratePreviewCaptureCoordinator(timeoutMillis = 100)

        assertNull(coordinator.capture(1))
        coordinator.attach(endpoint)
        coordinator.detach(endpoint)
        assertNull(coordinator.capture(2))
    }

    @Test
    fun timeoutIsExplicitlyCancelledAndClosed() {
        val request = FakeRequest(
            NativePreviewCaptureResult(
                requestId = 1,
                status = NativePreviewCaptureStatus.TIMED_OUT
            )
        )
        val coordinator = SubstratePreviewCaptureCoordinator(timeoutMillis = 1)
        coordinator.attach(RecordingEndpoint(request))

        assertNull(coordinator.capture(3))
        assertEquals(1, request.cancelCount)
        assertEquals(1, request.closeCount)
    }

    private class RecordingEndpoint(
        private val request: NativePreviewCaptureRequest
    ) : SubstratePreviewCaptureEndpoint {
        var lastToken = 0L
        var lastRequestId = 0L

        override fun enqueueSubstratePreviewCapture(
            requestId: Long,
            substratePreviewToken: Long
        ): NativePreviewCaptureRequest {
            lastRequestId = requestId
            lastToken = substratePreviewToken
            return request
        }
    }

    private class FakeRequest(
        private val result: NativePreviewCaptureResult
    ) : NativePreviewCaptureRequest {
        var cancelCount = 0
        var closeCount = 0

        override fun await(timeoutMillis: Long): NativePreviewCaptureResult = result

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
        alphaMode = NativePreviewAlphaMode.OPAQUE,
        payload = payload
    )
}
