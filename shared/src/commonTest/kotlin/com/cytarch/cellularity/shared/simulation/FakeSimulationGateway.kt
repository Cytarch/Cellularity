package com.cytarch.cellularity.shared.simulation

import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.SimulationStats
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Controllable common fake for feature tests; never packaged in the application. */
class FakeSimulationGateway(
    initialEnvironment: EnvironmentSnapshot,
    initialStats: SimulationStats = SimulationStats(),
    initialContentState: SimulationContentState = SimulationContentState.newWorld()
) : SimulationGateway {
    private val mutableState = MutableStateFlow<SimulationSessionState>(SimulationSessionState.Ready)
    override val state: StateFlow<SimulationSessionState> = mutableState.asStateFlow()

    private val mutableEnvironment = MutableStateFlow(initialEnvironment)
    override val environment: StateFlow<EnvironmentSnapshot> = mutableEnvironment.asStateFlow()

    private val mutableStats = MutableStateFlow(initialStats)
    override val stats: StateFlow<SimulationStats> = mutableStats.asStateFlow()

    private val mutableContentState = MutableStateFlow(initialContentState)
    override val contentState: StateFlow<SimulationContentState> =
        mutableContentState.asStateFlow()

    val commands = mutableListOf<SimulationCommand>()
    val environmentPatches = mutableListOf<EnvironmentPatch>()
    val queuedApplyResults = mutableListOf<ApplyResult>()
    val inspectedCells = mutableListOf<CellId>()
    val saveTargets = mutableListOf<StoredSubstrateId?>()
    val cells = mutableMapOf<CellId, CellSnapshot>()
    var beforeEnvironmentApply: suspend (EnvironmentPatch) -> Unit = {}
    var closeCount = 0

    override suspend fun execute(command: SimulationCommand): SimulationResult {
        rejection()?.let { return SimulationResult.Failure(it) }
        commands += command
        when (command) {
            SimulationCommand.Reset -> markChanged()
            is SimulationCommand.Reload -> markReplacementPersisted()
        }
        return SimulationResult.Queued
    }

    override suspend fun applyEnvironment(patch: EnvironmentPatch): ApplyResult {
        rejection()?.let { return ApplyResult.Failure(it) }
        environmentPatches += patch
        beforeEnvironmentApply(patch)
        return if (queuedApplyResults.isNotEmpty()) {
            queuedApplyResults.removeAt(0)
        } else {
            ApplyResult.Applied(patch.baseRevision + 1)
        }.also { if (it is ApplyResult.Applied) markChanged() }
    }

    override suspend fun inspectCell(id: CellId): CellSnapshot? {
        if (rejection() != null) return null
        inspectedCells += id
        return cells[id]
    }

    override suspend fun save(target: StoredSubstrateId?): SaveResult {
        rejection()?.let { return SaveResult.Failure(it) }
        saveTargets += target
        val restingState = mutableState.value
        mutableState.value = SimulationSessionState.Saving
        mutableState.value = restingState
        return if (target != null) {
            markPersisted()
            SaveResult.Saved(target)
        } else {
            SaveResult.Failure(
                com.cytarch.cellularity.shared.model.CellularityError(
                    com.cytarch.cellularity.shared.model.CellularityErrorCode.INVALID_VALUE,
                    "Missing fake save target"
                )
            )
        }
    }

    override suspend fun close() {
        if (mutableState.value == SimulationSessionState.Closing ||
            mutableState.value == SimulationSessionState.Closed
        ) {
            return
        }
        mutableState.value = SimulationSessionState.Closing
        closeCount += 1
        mutableState.value = SimulationSessionState.Closed
    }

    fun emitEnvironment(value: EnvironmentSnapshot) {
        if (mutableEnvironment.value != value) markChanged()
        mutableEnvironment.value = value
    }

    fun emitStats(value: SimulationStats) {
        if (mutableStats.value != value) markChanged()
        mutableStats.value = value
    }

    fun pause() {
        if (mutableState.value == SimulationSessionState.Ready) {
            mutableState.value = SimulationSessionState.Paused
        }
    }

    fun resume() {
        if (mutableState.value == SimulationSessionState.Paused) {
            mutableState.value = SimulationSessionState.Ready
        }
    }

    fun fail(error: CellularityError) {
        mutableState.value = SimulationSessionState.Failed(error)
    }

    private fun markChanged() {
        val current = mutableContentState.value
        if (!current.isDirty) {
            mutableContentState.value = current.copy(revision = current.revision + 1)
        }
    }

    private fun markPersisted() {
        val current = mutableContentState.value
        mutableContentState.value = current.copy(persistedRevision = current.revision)
    }

    private fun markReplacementPersisted() {
        val next = mutableContentState.value.revision + 1
        mutableContentState.value = SimulationContentState(next, next)
    }

    private fun rejection(): CellularityError? = when (val current = mutableState.value) {
        SimulationSessionState.Ready,
        SimulationSessionState.Paused,
        SimulationSessionState.Saving -> null
        SimulationSessionState.New,
        SimulationSessionState.Opening -> CellularityError(
            CellularityErrorCode.SESSION_NOT_READY,
            "Fake simulation session is not ready"
        )
        SimulationSessionState.Closing,
        SimulationSessionState.Closed -> CellularityError(
            CellularityErrorCode.SESSION_CLOSED,
            "Fake simulation session is closed"
        )
        is SimulationSessionState.Failed -> current.error
    }
}
