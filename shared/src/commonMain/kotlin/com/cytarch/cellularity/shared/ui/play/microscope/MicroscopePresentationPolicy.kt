package com.cytarch.cellularity.shared.ui.play.microscope

/**
 * Describes the base presentation cadence independently from the simulation scheduler.
 * Shared by Android and the desktop, so both draw the microscope at the same rates.
 * A hidden Play tab never needs a swap, but its simulation keeps the selected
 * temperature policy and continues running.
 */
object MicroscopePresentationPolicy {
    const val INCUBATE_FRAME_INTERVAL_MILLIS = 34L
    const val NO_RENDER_STATUS_INTERVAL_MILLIS = 100L
    const val TEST_PUBLICATION_INTERVAL_MILLIS = 50L

    fun plan(
        temperature: MicroscopeTemperature,
        presentationVisible: Boolean,
        publicationIntervalMillis: Long
    ): MicroscopePresentationPlan {
        require(publicationIntervalMillis > 0L) {
            "Microscope publication interval must be positive"
        }
        if (!presentationVisible) {
            return MicroscopePresentationPlan(
                cadence = MicroscopeRenderCadence.ON_DEMAND,
                requestInitialFrame = false,
                frameIntervalMillis = null,
                statusPollIntervalMillis = NO_RENDER_STATUS_INTERVAL_MILLIS
            )
        }

        return when (temperature) {
            MicroscopeTemperature.INCUBATE -> MicroscopePresentationPlan(
                cadence = MicroscopeRenderCadence.ON_DEMAND,
                requestInitialFrame = true,
                frameIntervalMillis = INCUBATE_FRAME_INTERVAL_MILLIS,
                statusPollIntervalMillis = publicationIntervalMillis
            )
            MicroscopeTemperature.INCUBATE_NO_RENDER -> MicroscopePresentationPlan(
                cadence = MicroscopeRenderCadence.ON_DEMAND,
                requestInitialFrame = true,
                frameIntervalMillis = null,
                statusPollIntervalMillis = NO_RENDER_STATUS_INTERVAL_MILLIS
            )
            else -> MicroscopePresentationPlan(
                cadence = MicroscopeRenderCadence.CONTINUOUS,
                requestInitialFrame = false,
                frameIntervalMillis = null,
                statusPollIntervalMillis = publicationIntervalMillis
            )
        }
    }
}

enum class MicroscopeRenderCadence {
    CONTINUOUS,
    ON_DEMAND
}

data class MicroscopePresentationPlan(
    val cadence: MicroscopeRenderCadence,
    val requestInitialFrame: Boolean,
    val frameIntervalMillis: Long?,
    val statusPollIntervalMillis: Long?
)
