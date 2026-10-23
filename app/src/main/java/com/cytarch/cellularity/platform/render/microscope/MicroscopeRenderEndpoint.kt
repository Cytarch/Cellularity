package com.cytarch.cellularity.platform.render.microscope

import android.content.Context
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest
import com.cytarch.cellularity.platform.simulation.MicroscopeRuntimePublicationSource
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanRenderWorkerBridge
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanWorkerStatus
import com.cytarch.cellularity.shared.model.GenomeDraft
import java.nio.ByteBuffer

/** Surface-bound microscope endpoint exposed to the Android render host. */
internal interface MicroscopeRenderEndpoint {
    val runtimePublicationSource: MicroscopeRuntimePublicationSource

    fun createVulkanRenderWorker(
        context: Context,
        initialContinuousRendering: Boolean,
        statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
    ): NativeVulkanRenderWorkerBridge

    fun onSurfaceCreated()

    fun onSurfaceChanged(width: Int, height: Int)

    fun onSurfaceDestroyed()

    fun onVulkanSurfaceReady(): Boolean

    fun onVulkanSurfaceUnavailable()

    fun draw(): Long

    fun clearWithoutRendering(): Long

    fun enqueueSubstratePreviewCapture(
        requestId: Long,
        substratePreviewToken: Long
    ): NativePreviewCaptureRequest?

    fun setLifecyclePaused(paused: Boolean)

    fun publishPlacementGenome(genome: GenomeDraft): Long

    fun submitMicroscopeInputBatch(
        expectedInputEpoch: Long,
        batchSequence: Long,
        input: ByteBuffer,
        result: ByteBuffer,
        expectedPlacementGenomeRevision: Long
    )

    fun readPresentationMotionSnapshot(result: ByteBuffer): Boolean

    fun inputIngressSnapshot(): MicroscopeInputIngressSnapshot

    fun requestInputResync(): Boolean

    fun setToolOrdinal(toolOrdinal: Int)

    fun setTemperatureMode(temperatureMode: Int)

    fun setSlowMotionSpeed(speed: Float)

    fun setIncubateTargetUps(targetUps: Double)

    fun setPerformanceHudEnabled(enabled: Boolean)
}
