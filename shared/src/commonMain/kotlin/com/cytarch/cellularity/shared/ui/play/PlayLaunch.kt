package com.cytarch.cellularity.shared.ui.play

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract

/** Serializable-by-value description of the world a Play session must open. */
sealed interface PlayLaunchSource {
    data class New(val configuration: SimulationConfiguration) : PlayLaunchSource

    data class Stored(val id: StoredSubstrateId) : PlayLaunchSource
}

enum class PlayLaunchFailureReason {
    BAD_STORED_ID,
    UNSUPPORTED_CONFIGURATION
}

sealed interface PlayLaunchDecodeResult {
    data class Valid(val source: PlayLaunchSource) : PlayLaunchDecodeResult

    data class Invalid(val reason: PlayLaunchFailureReason) : PlayLaunchDecodeResult
}

/** Pure decoder shared by Android Intent and saved-state adapters. */
object PlayLaunchContract {
    fun decode(
        stored: Boolean,
        name: String?,
        maxCells: Int,
        maxFoods: Int,
        radius: Double,
        innerRadius: Double = 0.0
    ): PlayLaunchDecodeResult {
        if (stored) {
            if (!CellularityFileNames.isValidDisplayName(name)) {
                return PlayLaunchDecodeResult.Invalid(PlayLaunchFailureReason.BAD_STORED_ID)
            }
            return PlayLaunchDecodeResult.Valid(
                PlayLaunchSource.Stored(StoredSubstrateId(requireNotNull(name)))
            )
        }

        val configuration = SimulationConfiguration(
            maxCellCount = maxCells,
            maxFoodCount = maxFoods,
            radius = radius,
            innerRadius = innerRadius
        )
        return if (SimulationContract.isConfigurationValid(configuration)) {
            PlayLaunchDecodeResult.Valid(PlayLaunchSource.New(configuration))
        } else {
            PlayLaunchDecodeResult.Invalid(
                PlayLaunchFailureReason.UNSUPPORTED_CONFIGURATION
            )
        }
    }
}
