package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressSnapshot

internal data class MicroscopeRuntimePublicationEnvelope(
    val frame: MicroscopeFrameSnapshot?,
    val inputIngress: MicroscopeInputIngressSnapshot? = null
)

/** Session-level status source; it is deliberately independent from any render surface. */
internal interface MicroscopeRuntimePublicationSource {
    val publicationIntervalMillis: Long

    /**
     * Starts one serial poll without waiting for runtime ownership.
     *
     * Returns false only when another poll is in flight or the session no
     * longer accepts publications. [completion] runs on the session's
     * publication dispatcher and carries the generation supplied by the View.
     */
    fun tryStartPoll(
        viewGeneration: Long,
        completion: (Long, MicroscopeRuntimePublicationEnvelope) -> Unit
    ): Boolean
}

/** Executes publication work under the session lifetime/runtime guard. */
internal interface MicroscopeRuntimePublicationExecutor {
    fun tryStartPublicationPoll(
        viewGeneration: Long,
        poll: () -> MicroscopeFrameSnapshot?,
        completion: (Long, MicroscopeFrameSnapshot?) -> Unit
    ): Boolean
}
