/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.play.genome_editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue

/**
 * What the editor asks of whatever draws the organism.
 *
 * The editor owns the gestures and the buttons; the drawing belongs to a platform. This is the
 * seam between them, and it is deliberately the smallest one that works: the editor says a tap
 * landed somewhere, or that the person dragged or pinched, and whoever is drawing decides what
 * that means for the camera it holds. On the phone that is the native preview view; on a desktop
 * it is whatever that port draws with.
 */
interface GenomePreviewInteraction {
    fun handleTap(x: Float, y: Float)

    fun handleMove(dx: Float, dy: Float)

    fun handleZoom(factor: Float, x: Float, y: Float)

    /** Freezes the organism on screen, for a drag that moves a gene rather than the camera. */
    fun captureGhost()

    fun clearGhost()
}

/**
 * The one piece of the drawing that the editor itself holds.
 *
 * It is handed out rather than owned, because the editor exists for as long as the screen and the
 * drawing exists for as long as the panel: whoever is drawing puts itself here when it appears and
 * takes itself away when it goes.
 */
class GenomePreviewHandle {
    var interaction: GenomePreviewInteraction? = null
}

/**
 * The numbers the editor shows over the organism.
 *
 * Counted by whoever is drawing, because only it knows how many cells it has grown, and shown by
 * the editor, because it already knows where the corner of the panel is.
 */
class GenomePreviewMetrics {
    var cellCount by mutableIntStateOf(0)
    var maximumCellCount by mutableIntStateOf(0)
}
