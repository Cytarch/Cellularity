/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.CellId
import com.cytarch.cellularity.shared.simulation.EnvironmentBatchCodec
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.SaveResult
import com.cytarch.cellularity.shared.simulation.SimulationCommand
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import com.cytarch.cellularity.shared.simulation.SimulationResult
import com.cytarch.cellularity.shared.simulation.SimulationSessionState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/**
 * The shared simulation boundary, backed by the native renderer's session.
 *
 * The renderer already owns the world, so this does not open one; it presents the world
 * that is already running through the interface the shared screens were written against,
 * which is what lets the substrate panel edit the live simulation rather than a copy.
 *
 * Two things about the reads are worth knowing before reading the rest. Reading the
 * environment or the stats is not a field access: each is a request posted to the thread
 * that owns the world and answered when that thread reaches it, so a read costs a round
 * trip and a frame boundary. They are therefore never polled per frame, and never made
 * from the interface thread.
 *
 * There is also no environment value until a read succeeds. The panel already tells
 * "nothing to show" from "show this" by the snapshot being null, so the absence is
 * represented honestly rather than by a placeholder document the simulation never wrote.
 */
class DesktopSimulationGateway(
    private val sessionHandle: Long,
    private val renderer: DesktopNativeRenderer = DesktopNativeRenderer,
    private val ioDispatcher: kotlinx.coroutines.CoroutineDispatcher = Dispatchers.IO
) : SimulationGateway {

    private val mutableState = MutableStateFlow<SimulationSessionState>(
        SimulationSessionState.Opening
    )
    override val state: StateFlow<SimulationSessionState> = mutableState.asStateFlow()

    private val mutableContentState = MutableStateFlow(SimulationContentState.newWorld())
    override val contentState: StateFlow<SimulationContentState> =
        mutableContentState.asStateFlow()

    /**
     * The last confirmed environment, or null before the first successful read.
     *
     * The shared interface requires a value at all times, because on a phone the world is
     * opened by the same object that reports it and there is never a moment without one.
     * Here the world already exists and this only reads it, so there is a real interval
     * with nothing to report. The panel tells the two apart by this being null, which is
     * why the absence is tracked separately rather than by handing the interface a
     * document the simulation never wrote.
     */
    /** Null until the first successful read. */
    private val mutableEnvironment = MutableStateFlow<EnvironmentSnapshot?>(null)

    val confirmedEnvironment: StateFlow<EnvironmentSnapshot?> = mutableEnvironment.asStateFlow()

    /**
     * The value the shared interface sees.
     *
     * It starts as a zeroed document of the right shape rather than an arbitrary one,
     * because the panel maps fields by index: a gene pool of the wrong length would be an
     * out-of-range read rather than a value that merely looks wrong.
     */
    private val mutableConfirmed = MutableStateFlow(absentEnvironment())

    override val environment: StateFlow<EnvironmentSnapshot> = mutableConfirmed.asStateFlow()

    private val mutableStats = MutableStateFlow(SimulationStats())
    override val stats: StateFlow<SimulationStats> = mutableStats.asStateFlow()

    private val applyMutex = Mutex()

    /** Reads the environment and the stats once. */
    suspend fun readOnce() {
        val snapshot = withContext(ioDispatcher) {
            runCatching { renderer.nativeReadEnvironment(sessionHandle) }
                .getOrNull()
                ?.let { bytes ->
                    runCatching { EnvironmentBatchCodec.decodeSnapshot(bytes) }.getOrNull()
                }
        }
        if (snapshot != null) {
            mutableEnvironment.value = snapshot
            mutableConfirmed.value = snapshot
        }

        val statsValues = withContext(ioDispatcher) {
            runCatching { renderer.nativeReadStats(sessionHandle) }.getOrNull()
        }
        if (statsValues != null && statsValues.size >= STATS_FIELD_COUNT) {
            mutableStats.value = SimulationStats(
                cellCount = statsValues[3].toInt(),
                // The native snapshot carries a mass, not a count of particles, so it is
                // reported as a mass. Calling it a count would be a different number.
                foodMass = statsValues[4].toDouble(),
                time = statsValues[5].toDouble() / 1000.0,
                updateCount = statsValues[2]
            )
        }
    }

    /**
     * Reads once and returns whether the world answered.
     *
     * The distinction matters because a read that fails leaves the confirmed environment null,
     * and the panel shows its loading state for as long as that lasts. A caller that cannot tell
     * "read and found nothing yet" from "read and was answered" has no way to try again.
     */
    private suspend fun readOnceOrNull(): EnvironmentSnapshot? {
        readOnce()
        return mutableEnvironment.value
    }

    /**
     * Records that the session is live, and takes the first reading -- retrying until it lands.
     *
     * It used to read exactly once and then declare the session ready whether or not anything
     * came back. A single read can genuinely fail on a world that has only just been created, and
     * when it did, the state said Ready while the confirmed environment stayed null -- so the
     * substrate panel showed its loading message for the rest of the run and nothing ever tried
     * again. That is exactly what a panel stuck at three fifths of its height is: not an empty
     * world, but a read that was never repeated.
     *
     * So the read is repeated until it succeeds. The retries are bounded, and when the bound is
     * reached the session is deliberately left in the opening state rather than being called
     * ready: there is no failure state in the shared model, and claiming readiness for a world
     * that has never answered is what produced the fault in the first place. Reporting less than
     * is known is the safer of the two mistakes.
     */
    suspend fun start() {
        var attempt = 0
        while (true) {
            if (readOnceOrNull() != null) {
                if (mutableState.value == SimulationSessionState.Opening) {
                    mutableState.value = SimulationSessionState.Ready
                }
                return
            }
            attempt++
            if (attempt >= MAXIMUM_OPENING_READ_ATTEMPTS) {
                return
            }
            delay(OPENING_READ_RETRY_MILLIS)
        }
    }

    override suspend fun execute(command: SimulationCommand): SimulationResult = when (command) {
        SimulationCommand.Reset -> {
            // A reset is queued rather than applied, so acceptance means the request was
            // taken, not that the world is already empty. The consequence appears in the
            // next read, which is why one is taken.
            val status = withContext(ioDispatcher) {
                runCatching { renderer.nativeResetWorld(sessionHandle) }.getOrDefault(FAILED)
            }
            if (status == 0) {
                mutableContentState.value = SimulationContentState(
                    revision = mutableContentState.value.revision + 1,
                    persistedRevision = null
                )
                readOnce()
                SimulationResult.Queued
            } else {
                SimulationResult.Failure(
                    CellularityError(
                        CellularityErrorCode.INVALID_VALUE,
                        "The simulation did not accept the reset."
                    )
                )
            }
        }

        is SimulationCommand.Reload -> SimulationResult.Failure(
            CellularityError(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "Reloading a stored substrate is not available in the desktop build yet."
            )
        )
    }

    override suspend fun applyEnvironment(patch: EnvironmentPatch): ApplyResult =
        applyMutex.withLock {
            val bytes = try {
                EnvironmentBatchCodec.encodePatch(patch)
            } catch (failure: CancellationException) {
                throw failure
            } catch (failure: RuntimeException) {
                // A patch that cannot even be encoded never reached the simulation, and
                // saying so as a rejection is more useful than throwing at the caller.
                return failed("The environment change could not be encoded.")
            }

            val resultBytes = try {
                withContext(ioDispatcher) {
                    renderer.nativeApplyEnvironmentPatch(sessionHandle, bytes)
                }
            } catch (failure: CancellationException) {
                throw failure
            } catch (failure: RuntimeException) {
                null
            } ?: return failed("The simulation did not answer the environment change.")

            val result = try {
                EnvironmentBatchCodec.decodeApplyResult(resultBytes, patch)
            } catch (failure: CancellationException) {
                throw failure
            } catch (failure: RuntimeException) {
                return failed("The simulation sent an unreadable environment result.")
            }

            // The patch moved the world on, so what the panel shows is now stale by exactly
            // one revision. Reading back is what keeps a control from settling on a value
            // the simulation never had.
            readOnce()
            result
        }

    override suspend fun inspectCell(id: CellId): CellSnapshot? {
        // The native side answers with a versioned cell document whose decoder still lives
        // in the Android module. Returning null says "no snapshot" rather than inventing
        // one, so the diagnosis tool shows nothing selected, which is the truthful reading
        // of a feature that is not wired up here yet.
        return null
    }

    override suspend fun save(target: StoredSubstrateId?): SaveResult = SaveResult.Failure(
        CellularityError(
            CellularityErrorCode.UNSUPPORTED_FEATURE,
            "Saving a substrate is not available in the desktop build yet."
        )
    )

    override suspend fun close() {
        if (mutableState.value != SimulationSessionState.Closed) {
            mutableState.value = SimulationSessionState.Closed
        }
    }

    /**
     * A failure that never reached the world.
     *
     * It is a failure rather than a rejection on purpose: a rejection means the simulation
     * refused specific changes and must therefore name at least one, and there is no
     * change to name when the patch could not be encoded or the call did not complete.
     */
    private fun failed(message: String): ApplyResult = ApplyResult.Failure(
        CellularityError(CellularityErrorCode.INVALID_VALUE, message)
    )

    private companion object {
        const val FAILED = 1

        /** revision, worldEpoch, updates, cells, food, time, temperature, tool, cell, failed */
        const val STATS_FIELD_COUNT = 10

    /**
     * How many times the opening read is tried before the session is left as opening.
     *
     * Twenty attempts at a quarter of a second is five seconds. A world that has not answered in
     * five seconds is not slow, it is not there -- and a person watching a panel that says it is
     * loading deserves the difference between "not yet" and "never", which is a distinction the
     * state now keeps instead of erasing.
     */
    const val MAXIMUM_OPENING_READ_ATTEMPTS = 20

    /** How long to wait between opening reads. */
    const val OPENING_READ_RETRY_MILLIS = 250L
    }
}

/** A scalar field with no contribution, used only where a field must be named. */
internal fun emptyScalarField(type: ScalarFieldType = ScalarFieldType.CONSTANT) =
    ScalarFieldSnapshot(
        type = type,
        base = 0.0,
        strength = 0.0,
        angle = 0.0,
        offset = 0.0,
        softness = 0.0
    )

/** A vector field with no contribution, used only where a field must be named. */
internal fun emptyVectorField(type: VectorFieldType = VectorFieldType.LINEAR_DIRECTIONAL) =
    VectorFieldSnapshot(type = type, strength = 0.0, angle = 0.0)

/**
 * A zeroed document of the right shape, for the interval before the first read.
 *
 * Its length is taken from the contract rather than guessed, because the panel indexes
 * into the gene pool and the mutation weights: a short list would read out of range, and
 * the resulting crash would look like a rendering problem rather than a missing read.
 */
private fun absentEnvironment() = EnvironmentSnapshot(
    revision = 0,
    addRandom = false,
    dieEdge = false,
    onlyTweakMutations = false,
    mutationOnlyAfterSplit = false,
    aging = false,
    mobileFood = false,
    gravityField = emptyVectorField(),
    densityField = emptyScalarField(),
    salinityField = emptyScalarField(),
    foodLipidLayerField = emptyScalarField(),
    radiation = 0.0,
    testMulticellularCoef = 0.0,
    testMutationCoef = 0.0,
    testFoodDecay = 0.0,
    testIdleMetabolismScale = 0.0,
    testFlagellocyteCostScale = 0.0,
    testPhagocyteEatSpeedScale = 0.0,
    viscosity = 0.0,
    frictionDynamic = 0.0,
    frictionStatic = 0.0,
    foodSpeed = 0.0,
    foodSize = 0.0,
    foodLumpiness = 0.0,
    foodLumpSize = 0.0,
    foodLipidLayer = 0.0,
    salinity = 0.0,
    lightIntensity = 0.0,
    lightRange = 0.0,
    lightSpeed = 0.0,
    gravity = 0.0,
    density = 0.0,
    densityGradient = 0.0,
    nitrates = 0.0,
    genePool = List(EnvironmentContract.GENE_POOL_SIZE) { false },
    mutationWeights = List(EnvironmentContract.MUTATION_WEIGHT_COUNT) { 0.0 }
)
