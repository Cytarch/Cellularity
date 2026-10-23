package com.cytarch.cellularity.ui.play_menu.screens.microscope

import android.view.View
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import com.cytarch.cellularity.R
import com.cytarch.cellularity.platform.render.microscope.AndroidMicroscopeRenderHost
import com.cytarch.cellularity.platform.render.microscope.AndroidMicroscopeSurfaceFactory
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInfoReceiver
import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeState
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeScreen as SharedMicroscopeScreen

@Composable
fun MicroscopeScreen(
    state: MicroscopeState,
    surfaceFactory: AndroidMicroscopeSurfaceFactory,
    placementGenome: GenomeDraft,
    phoneWidthMeter: Float,
    isVisible: Boolean,
    incubateTargetUps: Double,
    performanceHudEnabled: Boolean,
    onPlateActionClick: () -> Unit,
    onAction: (MicroscopeAction) -> Unit
) {
    val infoReceiver = remember(phoneWidthMeter, onAction) {
        object : MicroscopeInfoReceiver {
            override fun onInspectionChanged(inspection: CellInspectionSnapshot?) {
                onAction(MicroscopeAction.InspectionObserved(inspection))
            }

            override fun onZoomViewHeightChanged(viewHeight: Float?) {
                val magnification = viewHeight
                    ?.takeIf { it.isFinite() && it > 0f }
                    ?.let { phoneWidthMeter.toDouble() / it }
                onAction(MicroscopeAction.ZoomObserved(magnification))
            }

            override fun onRenderPreparationFailureChanged(failed: Boolean) {
                onAction(MicroscopeAction.RenderPreparationFailureObserved(failed))
            }
        }
    }

    SharedMicroscopeScreen(
        state = state,
        isVisible = isVisible,
        modifier = Modifier.fillMaxSize(),
        onPlateActionClick = onPlateActionClick,
        onAction = onAction
    ) { viewportState ->
        key(surfaceFactory) {
            AndroidView(
                factory = { ctx ->
                    surfaceFactory.create(ctx, infoReceiver).apply {
                        id = R.id.microscope_view
                        updatePlacementGenome(placementGenome)
                        setPresentationVisible(isVisible)
                        updateControls(
                            viewportState.selectedTool,
                            viewportState.selectedTemperature,
                            incubateTargetUps,
                            performanceHudEnabled
                        )
                    }
                },
                modifier = Modifier.fillMaxSize(),
                update = { view ->
                    view.updatePlacementGenome(placementGenome)
                    view.setPresentationVisible(isVisible)
                    view.visibility = View.VISIBLE
                    view.inputEnabled = isVisible
                    view.alpha = if (isVisible) 1f else 0f
                    view.translationX = if (isVisible) 0f else -100_000f

                    view.updateControls(
                        viewportState.selectedTool,
                        viewportState.selectedTemperature,
                        incubateTargetUps,
                        performanceHudEnabled
                    )
                },
                onRelease = AndroidMicroscopeRenderHost::release
            )
        }
    }
}
