package com.cytarch.cellularity.shared.ui.play.genome_editor

import androidx.compose.ui.graphics.Color

/**
 * What the editor and whatever draws the organism agree on visually.
 *
 * The panel under the organism is filled with this colour before anything is drawn into it, so
 * that a frame that has not arrived yet looks like the medium rather than like a hole in the
 * screen. It is shared because the agreement is: the phone's renderer and a desktop's are asked
 * for the same picture, and a panel filled with a different colour would flash.
 */
object GenomePreviewVisualContract {
    const val BACKGROUND_ARGB: Int = -14671840
    val BackgroundColor = Color(BACKGROUND_ARGB)
}
