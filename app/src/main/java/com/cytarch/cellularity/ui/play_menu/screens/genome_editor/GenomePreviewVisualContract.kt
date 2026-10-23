package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import androidx.compose.ui.graphics.Color
import com.cytarch.cellularity.platform.render.preview.GenomePreviewAcknowledgment

internal object GenomePreviewVisualContract {
    const val BACKGROUND_ARGB: Int = -14671840
    const val COVER_CLICKABLE = false
    const val COVER_ENABLED = false
    val BackgroundColor = Color(BACKGROUND_ARGB)
}

internal fun isGenomePreviewCaptureAvailable(
    released: Boolean,
    renderSurfaceAvailable: Boolean,
    supportsOffscreenCapture: Boolean
): Boolean =
    !released && renderSurfaceAvailable && supportsOffscreenCapture

internal fun viewportStateFromAcknowledgment(
    value: GenomePreviewAcknowledgment
): FloatArray? = if (
    value.viewportRevision != 0L &&
    value.cameraX.isFinite() &&
    value.cameraY.isFinite() &&
    value.zoom.isFinite() &&
    value.zoom > 0.0f
) {
    floatArrayOf(value.cameraX, value.cameraY, value.zoom)
} else {
    null
}
