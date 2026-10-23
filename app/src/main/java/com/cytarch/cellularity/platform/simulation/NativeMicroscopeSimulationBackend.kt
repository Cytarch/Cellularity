package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.NativeSimulationFacade
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.SimulationStats

internal class NativeMicroscopeSimulationBackend(
    private val simulation: NativeSimulationFacade,
    private val publishFrame: (SimulationStats, EnvironmentSnapshot?) -> Unit,
    private val publicationExecutor: MicroscopeRuntimePublicationExecutor,
    override val publicationIntervalMillis: Long =
        simulation.microscopeRuntimePublicationIntervalMillis()
) : MicroscopeSimulationBackend, MicroscopeRuntimePublicationSource {
    init {
        require(publicationIntervalMillis > 0L) {
            "Microscope publication interval must be positive"
        }
    }

    private var lastPreparationFailure: List<Long>? = null
    private val publicationCursor = MicroscopeRuntimePublicationCursor(
        simulation.microscopePublicationSourceGeneration()
    )

    override val runtimePublicationSource: MicroscopeRuntimePublicationSource
        get() = this

    private val pollOperation: () -> MicroscopeFrameSnapshot? =
        ::pollRuntimePublication

    override fun tryStartPoll(
        viewGeneration: Long,
        completion: (Long, MicroscopeRuntimePublicationEnvelope) -> Unit
    ): Boolean = publicationExecutor.tryStartPublicationPoll(
        viewGeneration,
        pollOperation,
        { generation, frame ->
            completion(generation, MicroscopeRuntimePublicationEnvelope(frame))
        }
    )

    private fun pollRuntimePublication(): MicroscopeFrameSnapshot? {
        if (
            publicationCursor.resetIfSourceChanged(
                simulation.microscopePublicationSourceGeneration()
            )
        ) {
            lastPreparationFailure = null
        }
        val publication = simulation.pollMicroscopeRuntimePublication(
            publicationCursor.runtimeRevision,
            publicationCursor.worldEpoch,
            publicationCursor.environmentRevision
        )
            ?: return null
        val status = publication.status
        if (status.size < RUNTIME_STATUS_FIELD_COUNT) return null
        val runtimeTelemetry = NativeMicroscopeRuntimeTelemetryCodec.decode(
            publication.rateTelemetry
        ) ?: return null
        val revision = status[STATUS_REVISION]
        val worldEpoch = status[STATUS_WORLD_EPOCH]
        val environmentRevision = status[STATUS_ENVIRONMENT_REVISION]
        val environmentChanged = worldEpoch != publicationCursor.worldEpoch ||
            environmentRevision != publicationCursor.environmentRevision
        val environment = if (environmentChanged) {
            publication.environmentState?.let(EnvironmentBatchCodec::decodeSnapshot)
        } else null
        val stats = SimulationStats(
            cellCount = status[STATUS_CELL_COUNT].toInt(),
            foodMass = Double.fromBits(status[STATUS_FOOD_MASS_BITS]),
            time = Double.fromBits(status[STATUS_TIME_BITS]),
            updateCount = status[STATUS_UPDATE_COUNT]
        )
        MicroscopeRuntimeDiagnostics.update(runtimeTelemetry, stats)
        publishFrame(stats, environment)

        val diagnosisRequested = status[STATUS_DIAGNOSIS_REQUESTED] != 0L
        val selectedCellId = status[STATUS_SELECTED_CELL_ID].toInt()
        val inspection = if (diagnosisRequested && selectedCellId >= 0) {
            NativeWireCodec.decodeCellSnapshot(
                publication.selectedCellState
            )?.let { cell ->
                CellInspectionSnapshot(
                    cell = cell,
                    genome = GenomeDraftWireCodec.decode(
                        requireNotNull(
                            publication.selectedGenomeState
                        ) {
                            "Selected cell genome is unavailable"
                        }
                    )
                )
            }
        } else {
            null
        }
        val preparationFailure = status
            .slice(STATUS_PREPARATION_FAILURE_KIND..STATUS_PREPARATION_SOURCE_SEQUENCE)
            .takeIf { it.firstOrNull() != 0L }
        if (preparationFailure != null && preparationFailure != lastPreparationFailure) {
            AppLog.error(
                "Microscope render preparation failed: " +
                    "kind=${preparationFailure[0]} stage=${preparationFailure[1]} " +
                    "epoch=${preparationFailure[2]} sourceSequence=${preparationFailure[3]}"
            )
        }
        lastPreparationFailure = preparationFailure
        publicationCursor.runtimeRevision = revision
        if (!environmentChanged || environment != null) {
            publicationCursor.worldEpoch = worldEpoch
            publicationCursor.environmentRevision = environmentRevision
        }
        return MicroscopeFrameSnapshot(
            rendererInvalidated = environmentChanged,
            stats = stats,
            diagnosisRequested = diagnosisRequested,
            inspection = inspection,
            renderPreparationFailed = preparationFailure != null ||
                status[STATUS_RUNTIME_FAILED] != 0L,
            runtimeTelemetry = runtimeTelemetry
        )
    }

    override fun setLifecyclePaused(paused: Boolean) =
        simulation.setMicroscopeLifecyclePaused(paused)

    override fun selectedGenome(): GenomeDraft? =
        simulation.selectedGenomeDraftBytes()?.let(GenomeDraftWireCodec::decode)

    private companion object {
        const val STATUS_REVISION = 0
        const val STATUS_WORLD_EPOCH = 1
        const val STATUS_UPDATE_COUNT = 2
        const val STATUS_ENVIRONMENT_REVISION = 3
        const val STATUS_CELL_COUNT = 4
        const val STATUS_FOOD_MASS_BITS = 5
        const val STATUS_TIME_BITS = 6
        const val STATUS_SELECTED_CELL_ID = 9
        const val STATUS_DIAGNOSIS_REQUESTED = 10
        const val STATUS_RUNTIME_FAILED = 11
        const val STATUS_PREPARATION_FAILURE_KIND = 12
        const val STATUS_PREPARATION_SOURCE_SEQUENCE = 15
        const val RUNTIME_STATUS_FIELD_COUNT = 18
    }
}

internal class MicroscopeRuntimePublicationCursor(
    initialSourceGeneration: Long
) {
    var sourceGeneration: Long = requirePositiveGeneration(initialSourceGeneration)
        private set
    var runtimeRevision: Long = 0L
    var worldEpoch: Long = 0L
    var environmentRevision: Long = Long.MIN_VALUE

    fun resetIfSourceChanged(currentSourceGeneration: Long): Boolean {
        val current = requirePositiveGeneration(currentSourceGeneration)
        if (current == sourceGeneration) return false
        sourceGeneration = current
        runtimeRevision = 0L
        worldEpoch = 0L
        environmentRevision = Long.MIN_VALUE
        return true
    }

    private fun requirePositiveGeneration(generation: Long): Long {
        require(generation > 0L) {
            "Microscope publication source generation must be positive"
        }
        return generation
    }
}
