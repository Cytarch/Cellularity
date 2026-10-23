package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.SimulationStats

/** Non-render microscope access owned by the simulation session. */
internal interface MicroscopeSimulationBackend {
    val runtimePublicationSource: MicroscopeRuntimePublicationSource

    fun setLifecyclePaused(paused: Boolean)

    fun selectedGenome(): GenomeDraft?
}

data class MicroscopeFrameSnapshot(
    val rendererInvalidated: Boolean,
    val stats: SimulationStats,
    val diagnosisRequested: Boolean,
    val inspection: CellInspectionSnapshot?,
    val renderPreparationFailed: Boolean = false,
    val runtimeTelemetry: MicroscopeRuntimeTelemetry = MicroscopeRuntimeTelemetry()
)
