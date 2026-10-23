package com.cytarch.cellularity.platform.render.preview

import android.content.Context
import com.cytarch.cellularity.NativeSimulationFacade
import java.nio.ByteBuffer

/** Narrow owner for one native Genome Preview world and its render endpoint. */
internal class NativeGenomePreviewSession(
    context: Context,
    previewMaxCells: Int
) : AutoCloseable {
    private val facade = NativeSimulationFacade(
        context.applicationContext,
        true,
        previewMaxCells,
        0,
        1.0
    )

    val renderEndpoint = facade.microscopeRenderEndpoint()
    val endpointGeneration: Long = facade.genomePreviewEndpointGeneration()

    fun borrowVulkanEndpoint(): NativePreviewEndpointOwner.Borrow =
        facade.borrowGenomePreviewEndpoint()

    fun draw(acknowledgment: ByteBuffer): Long =
        facade.drawGenomePreview(acknowledgment)

    fun readAcknowledgment(acknowledgment: ByteBuffer): Boolean =
        facade.readGenomePreviewAcknowledgment(acknowledgment)

    fun publishContent(
        genomeDraftBytes: ByteArray,
        simulationTimeHours: Double,
        producerSequence: Long,
        result: ByteBuffer
    ) = facade.publishGenomePreviewContent(
        genomeDraftBytes,
        simulationTimeHours,
        producerSequence,
        result
    )

    fun publishViewState(
        selectedGeneIndex: Int,
        gridEnabled: Boolean,
        treeEnabled: Boolean,
        viewportWidth: Int,
        viewportHeight: Int,
        producerSequence: Long,
        result: ByteBuffer
    ) = facade.publishGenomePreviewViewState(
        selectedGeneIndex,
        gridEnabled,
        treeEnabled,
        viewportWidth,
        viewportHeight,
        producerSequence,
        result
    )

    fun enqueueInteraction(
        completionToken: Long,
        kind: Int,
        first: Float,
        second: Float,
        third: Float
    ): Int = facade.enqueueGenomePreviewInteraction(
        completionToken,
        kind,
        first,
        second,
        third
    )

    fun enqueueCapture(
        requestId: Long,
        genomeDraftBytes: ByteArray,
        maximumSimulationTime: Double
    ): NativePreviewCaptureRequest? = facade.enqueueGenomePreviewCapture(
        requestId,
        genomeDraftBytes,
        maximumSimulationTime
    )

    override fun close() = facade.close()
}
