/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.play.genome_editor

/**
 * What the editor remembers about its own layout between visits.
 *
 * Not the genome, and not a file: this is where the person left the preview, and how they had
 * zoomed and panned the organism inside it. Both platforms want the same memory for the same
 * reason -- returning to an editor that has thrown away the size you dragged it to is the kind of
 * small betrayal that makes an editor feel like a form -- so it lives here rather than in either
 * platform's coordinator.
 *
 * It is deliberately not observable. Nothing is drawn from these values; they are read once when
 * the editor appears and written when the person moves something, so making them state would
 * redraw the editor on every pan.
 */
class GenomeEditorLayoutMemory {
    private var viewportState: FloatArray? = null
    private var previewHeightDp: Float? = null
    private var previewWidthDp: Float? = null

    fun restoreViewport(): FloatArray? = viewportState?.copyOf()

    fun rememberViewport(value: FloatArray) {
        if (value.size >= VIEWPORT_COMPONENTS) viewportState = value.copyOf()
    }

    fun restorePreviewHeightDp(): Float? = previewHeightDp

    fun rememberPreviewHeightDp(value: Float) {
        if (value.isFinite() && value > 0.0f) previewHeightDp = value
    }

    fun restorePreviewWidthDp(): Float? = previewWidthDp

    fun rememberPreviewWidthDp(value: Float) {
        if (value.isFinite() && value > 0.0f) previewWidthDp = value
    }

    companion object {
        /** A camera needs a position and a zoom, and a state shorter than that is not one. */
        const val VIEWPORT_COMPONENTS = 3
    }
}
