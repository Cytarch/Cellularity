package com.cytarch.cellularity.platform.render.microscope

import android.content.Context
import android.widget.FrameLayout
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTool

/** Main-thread delivery of immutable microscope UI observations. */
interface MicroscopeInfoReceiver {
    fun onInspectionChanged(inspection: CellInspectionSnapshot?)

    fun onZoomViewHeightChanged(viewHeight: Float?)

    fun onRenderPreparationFailureChanged(failed: Boolean)
}

/** Small UI-facing contract shared by every Android microscope render host. */
abstract class AndroidMicroscopeRenderHost(context: Context) : FrameLayout(context) {
    abstract val backendKind: RenderBackendKind

    abstract var inputEnabled: Boolean

    abstract fun setPresentationVisible(visible: Boolean)

    abstract fun updateControls(
        tool: MicroscopeTool,
        temperature: MicroscopeTemperature,
        incubateTargetUps: Double,
        performanceHudEnabled: Boolean
    )

    abstract fun updatePlacementGenome(genome: GenomeDraft)

    abstract fun release()
}

/** Creates the Android surface while retaining the private rendering backend. */
class AndroidMicroscopeSurfaceFactory internal constructor(
    private val simulationFactory: (RenderBackendKind) -> MicroscopeRenderEndpoint,
    private val selectedGenomeSource: () -> GenomeDraft?,
    private val substratePreviewCapture: SubstratePreviewCaptureCoordinator,
    val backendKind: RenderBackendKind,
    private val onRuntimeFailure: (String) -> Unit = {}
) {
    fun create(
        context: Context,
        infoReceiver: MicroscopeInfoReceiver
    ): AndroidMicroscopeRenderHost {
        val surfaceHostFactory = when (backendKind) {
            RenderBackendKind.OPEN_GL_ES -> GlesMicroscopeSurfaceHostFactory
            RenderBackendKind.VULKAN -> VulkanMicroscopeSurfaceHostFactory
        }
        return AndroidMicroscopeView(
            context = context,
            endpoint = simulationFactory(backendKind),
            infoReceiver = infoReceiver,
            substratePreviewCapture = substratePreviewCapture,
            backendKind = backendKind,
            surfaceHostFactory = surfaceHostFactory,
            onRuntimeFailure = onRuntimeFailure
        )
    }

    fun withBackend(
        backendKind: RenderBackendKind,
        onRuntimeFailure: (String) -> Unit
    ): AndroidMicroscopeSurfaceFactory = AndroidMicroscopeSurfaceFactory(
        simulationFactory = simulationFactory,
        selectedGenomeSource = selectedGenomeSource,
        substratePreviewCapture = substratePreviewCapture,
        backendKind = backendKind,
        onRuntimeFailure = onRuntimeFailure
    )

    fun selectedGenome(): GenomeDraft? = selectedGenomeSource()
}
