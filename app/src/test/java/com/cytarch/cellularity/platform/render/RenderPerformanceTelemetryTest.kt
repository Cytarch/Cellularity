package com.cytarch.cellularity.platform.render

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RenderPerformanceTelemetryTest {
    @After
    fun tearDown() {
        RenderPerformanceTelemetry.setEnabled(enabled = false, reset = true)
    }

    @Test
    fun recordsBackendSpecificRequestsOutcomesAndDistributions() {
        RenderPerformanceTelemetry.setEnabled(enabled = true, reset = true)

        RenderPerformanceTelemetry.recordFrameRequest(
            RenderBackendKind.VULKAN,
            coalesced = false
        )
        RenderPerformanceTelemetry.recordFrameRequest(
            RenderBackendKind.VULKAN,
            coalesced = true
        )
        RenderPerformanceTelemetry.recordCameraInput(
            RenderBackendKind.VULKAN,
            RenderPerformanceTelemetry.captureInputTimestampNanos()
        )
        RenderPerformanceTelemetry.finishFrame(
            RenderPerformanceTelemetry.beginFrame(RenderBackendKind.VULKAN),
            RenderFrameProfileOutcome.PRESENTED
        )
        RenderPerformanceTelemetry.finishFrame(
            RenderPerformanceTelemetry.beginFrame(RenderBackendKind.OPEN_GL_ES),
            RenderFrameProfileOutcome.DEFERRED
        )

        val report = RenderPerformanceTelemetry.snapshot()
        assertTrue(report.contains("enabled=true"))
        assertTrue(report.contains("vulkan_request_count=2"))
        assertTrue(report.contains("vulkan_coalesced_request_count=1"))
        assertTrue(report.contains("vulkan_presented_count=1"))
        assertTrue(report.contains("vulkan_cpu_frame_sample_count=1"))
        assertTrue(report.contains("vulkan_camera_input_to_present_sample_count=0"))
        assertTrue(report.contains("vulkan_input_to_native_accept_sample_count=0"))
        assertTrue(report.contains("vulkan_native_accept_to_present_sample_count=0"))
        assertTrue(report.contains("gles_deferred_count=1"))
        assertTrue(report.contains("gles_cpu_frame_sample_count=1"))
    }

    @Test
    fun recordsBoundedBoundaryCountersWithoutPerEventPayloads() {
        RenderPerformanceTelemetry.setEnabled(enabled = true, reset = true)

        RenderPerformanceTelemetry.recordBoundaryJniCall(
            RenderPerformanceTelemetry.JNI_DRAW_MICROSCOPE
        )
        RenderPerformanceTelemetry.recordBoundaryJniCall(
            RenderPerformanceTelemetry.JNI_MICROSCOPE_INPUT_COMMAND
        )
        RenderPerformanceTelemetry.recordRuntimePublicationPoll(successful = false)
        RenderPerformanceTelemetry.recordRuntimePublicationPoll(successful = true)
        RenderPerformanceTelemetry.recordViewportSnapshot(floatCount = 7)
        RenderPerformanceTelemetry.recordRenderTaskQueued(RenderBackendKind.VULKAN)
        RenderPerformanceTelemetry.recordRenderTaskExecuted(
            RenderBackendKind.VULKAN,
            drainedBeforeFrame = true
        )
        RenderPerformanceTelemetry.recordInputEvent()
        RenderPerformanceTelemetry.recordSemanticInputCommands(4)
        RenderPerformanceTelemetry.recordFullDiagnostics(
            durationNanos = 123L,
            payloadBytes = 456
        )

        val report = RenderPerformanceTelemetry.snapshot()
        assertTrue(report.contains("schema=2"))
        assertTrue(report.contains("boundary_schema=1"))
        assertTrue(report.contains("boundary_jni_draw_microscope_count=1"))
        assertTrue(report.contains("boundary_native_input_jni_call_count=1"))
        assertTrue(report.contains("boundary_runtime_publication_poll_count=2"))
        assertTrue(report.contains("boundary_runtime_publication_success_count=1"))
        assertTrue(report.contains("boundary_viewport_snapshot_allocation_count=1"))
        assertTrue(report.contains("boundary_viewport_snapshot_copy_bytes=28"))
        assertTrue(report.contains("boundary_vulkan_render_task_queued_count=1"))
        assertTrue(report.contains("boundary_vulkan_render_task_executed_count=1"))
        assertTrue(report.contains("boundary_vulkan_render_task_pre_frame_drain_count=1"))
        assertTrue(report.contains("boundary_vulkan_render_task_coalesced_count=0"))
        assertTrue(report.contains("boundary_input_event_count=1"))
        assertTrue(report.contains("boundary_semantic_input_command_count=4"))
        assertTrue(report.contains("boundary_full_diagnostics_build_count=1"))
        assertTrue(report.contains("boundary_full_diagnostics_transfer_count=1"))
        assertTrue(report.contains("boundary_full_diagnostics_transfer_bytes=456"))
        assertTrue(report.contains("boundary_full_diagnostics_sample_count=1"))
        assertTrue(report.contains("boundary_full_diagnostics_p50_ns=123"))
        assertTrue(report.contains("boundary_managed_allocated_bytes_delta="))
    }

    @Test
    fun disabledRecorderDoesNotCollectSamples() {
        RenderPerformanceTelemetry.setEnabled(enabled = false, reset = true)

        RenderPerformanceTelemetry.recordFrameRequest(
            RenderBackendKind.VULKAN,
            coalesced = true
        )
        RenderPerformanceTelemetry.recordCameraInput(
            RenderBackendKind.VULKAN,
            RenderPerformanceTelemetry.captureInputTimestampNanos()
        )
        val token = RenderPerformanceTelemetry.beginFrame(RenderBackendKind.VULKAN)
        RenderPerformanceTelemetry.finishFrame(token, RenderFrameProfileOutcome.FAILED)

        val report = RenderPerformanceTelemetry.snapshot()
        assertTrue(report.contains("enabled=false"))
        assertTrue(report.contains("vulkan_request_count=0"))
        assertTrue(report.contains("vulkan_cpu_frame_sample_count=0"))
        assertTrue(report.contains("vulkan_camera_input_to_present_sample_count=0"))
        assertTrue(report.contains("boundary_jni_total_count=0"))
        assertTrue(report.contains("boundary_input_event_count=0"))
        assertTrue(report.contains("boundary_managed_allocated_bytes_per_frame=-1"))
        assertTrue(report.contains("boundary_managed_allocated_bytes_per_input_event=-1"))
    }

    @Test
    fun frameResultCodecRoundTripsEveryOutcomeAndSignedSemanticValue() {
        val outcomes = intArrayOf(
            RenderFrameResultCodec.OUTCOME_PRESENTED,
            RenderFrameResultCodec.OUTCOME_DEFERRED,
            RenderFrameResultCodec.OUTCOME_SWAPCHAIN_RECREATED,
            RenderFrameResultCodec.OUTCOME_SURFACE_LOST,
            RenderFrameResultCodec.OUTCOME_SKIPPED_NOT_READY,
            RenderFrameResultCodec.OUTCOME_FAILED
        )
        outcomes.forEach { outcome ->
            val flags = if (outcome == RenderFrameResultCodec.OUTCOME_PRESENTED) {
                RenderFrameResultCodec.FLAG_PRIMARY_CONTENT_PRESENTED
            } else {
                0
            }
            val bits = RenderFrameResultCodec.pack(outcome, flags, -1_234_567)
            assertTrue(RenderFrameResultCodec.isValid(bits))
            assertEquals(outcome, RenderFrameResultCodec.outcomeCode(bits))
            assertEquals(flags, RenderFrameResultCodec.flags(bits))
            assertEquals(-1_234_567, RenderFrameResultCodec.semanticValue(bits))
        }

        assertEquals(
            0xFFFFFFF900000100uL.toLong(),
            RenderFrameResultCodec.pack(
                RenderFrameResultCodec.OUTCOME_PRESENTED,
                RenderFrameResultCodec.FLAG_PRIMARY_CONTENT_PRESENTED,
                -7
            )
        )
    }

    @Test
    fun frameResultCodecRejectsUnknownReservedAndAmbiguousValues() {
        val invalid = longArrayOf(
            RenderFrameResultCodec.INVALID_WIRE,
            6L,
            0L,
            0x300L,
            0x101L,
            0x10100L
        )
        invalid.forEach { assertFalse(RenderFrameResultCodec.isValid(it)) }
        assertEquals(
            RenderFrameResultCodec.INVALID_WIRE,
            RenderFrameResultCodec.pack(
                RenderFrameResultCodec.OUTCOME_PRESENTED,
                RenderFrameResultCodec.FLAG_PRIMARY_CONTENT_PRESENTED or
                    RenderFrameResultCodec.FLAG_FALLBACK_CLEAR_PRESENTED
            )
        )
        assertEquals(
            RenderFrameResultCodec.INVALID_WIRE,
            RenderFrameResultCodec.pack(
                256,
                RenderFrameResultCodec.FLAG_PRIMARY_CONTENT_PRESENTED
            )
        )
        assertEquals(
            RenderFrameResultCodec.INVALID_WIRE,
            RenderFrameResultCodec.pack(
                RenderFrameResultCodec.OUTCOME_PRESENTED,
                257
            )
        )
    }
}
