package com.cytarch.cellularity.desktop.nativebridge

import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CompletableDeferred

/**
 * A plate's preview picture, taken from the thread that is allowed to draw it.
 *
 * Saving a plate prepares a preview frame, and drawing that frame is renderer work: the backend
 * records the thread that created it and refuses work from any other, so the capture cannot be made
 * from whatever thread the save happens to run on. The surface owns that thread, so the surface is
 * what takes the picture -- and this holder is how the save reaches it, because the two are created
 * in different places: the surface is made by the composition, the save by the plate's actions.
 *
 * It is a holder rather than a callback for the same reason a store is: the surface comes and goes
 * with the screen, and the caller that wants a picture outlives neither. A capture asked for while
 * no surface is attached answers null, which the save reads as "no picture" and writes the plate
 * without one -- the honest outcome for a plate that is not being drawn.
 */
class SubstratePreviewCapture {
    private val source = AtomicReference<SubstratePreviewSource?>(null)

    internal fun attach(candidate: SubstratePreviewSource) {
        source.set(candidate)
    }

    internal fun detach(candidate: SubstratePreviewSource) {
        source.compareAndSet(candidate, null)
    }

    /**
     * The prepared preview frame [token] names, or null when there is no picture to take.
     *
     * Suspends until the render thread has taken it. Null means the frame was refused, already used,
     * or there is no surface to take it -- none of which fails a save, because a plate without a
     * picture is a plate.
     */
    suspend fun capture(token: Long): ByteArray? {
        if (token == 0L) return null
        return source.get()?.captureSubstratePreview(token)
    }
}

/**
 * What a surface has to be able to do for [SubstratePreviewCapture] to use it.
 *
 * Internal because it exists for one caller: the surface is private to its own file, and the only
 * thing outside it that needs to speak to it is a save.
 */
internal interface SubstratePreviewSource {
    suspend fun captureSubstratePreview(token: Long): ByteArray?
}
