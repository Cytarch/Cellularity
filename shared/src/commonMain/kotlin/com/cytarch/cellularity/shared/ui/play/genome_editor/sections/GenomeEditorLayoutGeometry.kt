/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.play.genome_editor.sections

import kotlin.math.roundToInt

/**
 * The arithmetic behind the editor's split layout.
 *
 * These are separated from the screen because they are the part of it that can be wrong
 * without looking wrong: an extent that is off by a few pixels still draws, and a minimum
 * that is never enforced only shows up when someone drags the divider to the edge. Keeping
 * them here lets both the phone's editor and the desktop one share the rules rather than
 * each growing its own idea of how wide a preview may be.
 */

/** Whether the editor puts the preview beside its controls rather than above them. */
fun useSideBySideGenomeEditorLayout(widthPx: Float, heightPx: Float): Boolean =
    widthPx > heightPx

/** The physical surface and the visible viewport it is shown through. */
data class GenomePreviewSurfaceGeometry(
    val viewportWidth: Int,
    val viewportHeight: Int,
    val backingWidth: Int,
    val backingHeight: Int
)

/**
 * Keeps the physical surface at the editor's bounds while the divider changes only the
 * visible viewport. A renderer that ignores the explicit backing size keeps its established
 * match-parent child, which is why the two are reported separately rather than as one size.
 */
fun genomePreviewSurfaceGeometry(
    visibleWidthPx: Float,
    visibleHeightPx: Float,
    parentWidthPx: Float,
    parentHeightPx: Float
): GenomePreviewSurfaceGeometry = GenomePreviewSurfaceGeometry(
    viewportWidth = visibleWidthPx.roundToInt().coerceAtLeast(1),
    viewportHeight = visibleHeightPx.roundToInt().coerceAtLeast(1),
    backingWidth = parentWidthPx.roundToInt().coerceAtLeast(1),
    backingHeight = parentHeightPx.roundToInt().coerceAtLeast(1)
)

/**
 * The extent a drag asks for, held between the two limits.
 *
 * The maximum is floored at the minimum so a window squeezed narrower than one preview
 * leaves the preview at its minimum rather than producing an inverted range, which would
 * otherwise throw inside coerceIn.
 */
fun resizedPreviewExtent(
    currentExtentPx: Float,
    deltaPx: Float,
    minimumExtentPx: Float,
    maximumExtentPx: Float
): Float = (currentExtentPx + deltaPx).coerceIn(
    minimumExtentPx,
    maximumExtentPx.coerceAtLeast(minimumExtentPx)
)

/**
 * The extent a requested size can actually have, given the room beside it.
 *
 * A parent of zero width means the layout has not been measured yet, and the request is
 * taken at face value so the first frame keeps whatever the editor asked for instead of
 * collapsing to the minimum.
 */
fun constrainedPreviewExtent(
    requestedExtentPx: Float,
    parentExtentPx: Float,
    minimumExtentPx: Float,
    reservedSecondaryExtentPx: Float
): Float {
    if (parentExtentPx <= 0.0f) return requestedExtentPx.coerceAtLeast(minimumExtentPx)
    val effectiveMinimumExtentPx = minimumExtentPx.coerceAtMost(parentExtentPx)
    val maximumExtentPx = (parentExtentPx - reservedSecondaryExtentPx)
        .coerceIn(effectiveMinimumExtentPx, parentExtentPx)
    return requestedExtentPx.coerceIn(effectiveMinimumExtentPx, maximumExtentPx)
}
