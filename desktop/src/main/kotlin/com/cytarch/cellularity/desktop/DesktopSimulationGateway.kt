/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.files.DesktopSubstratePreviewEncoder
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.desktop.nativebridge.SubstratePreviewCapture
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
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
    /**
     * Where a saved plate goes, or null for a session that has nowhere to save into.
     *
     * Null is not a defect: the probes and the genome preview open sessions that belong to no
     * collection, and a save from one of those has to say so rather than write a file somewhere
     * nobody will look.
     */
    private val fileRepository: DesktopCellularityFileRepository? = null,
    /**
     * The plate this session was opened from, when it was opened from one.
     *
     * It is what makes a second save of the same run replace its own file instead of being refused
     * as a name that already exists, which is the phone's rule: a plate may overwrite itself and
     * nothing else.
     */
    private val openedPlate: StoredSubstrateId? = null,
    /**
     * Where the plate's preview picture is taken, when something is drawing this session.
     *
     * The picture is renderer work and only the surface's thread may ask for it, so the save reaches
     * the renderer through this. Null means no picture, which is a plate without one rather than a
     * failed save.
     */
    private val previewCapture: SubstratePreviewCapture? = null,
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

    /**
     * Reads the environment and the stats once, reporting what the environment read did.
     *
     * The read is now retried in place before it is given up on. There are two things that can
     * fail and they are different: the renderer can refuse to answer at all, which usually means
     * the world is not ready, and the bytes it returns can fail to decode, which means the two
     * sides disagree about the document. Retrying helps the first and cannot help the second, so
     * the two are counted apart and the count is kept for whoever asks -- a panel that says only
     * "loading" cannot tell a person which of the two they are looking at.
     *
     * The retry used to be outside, in start(), which meant it repeated the whole read including
     * a stats read that had already succeeded, and it gave up after a fixed number of attempts
     * that were measured in seconds rather than in what actually went wrong. Neither is a reason
     * for the read itself not to try again.
     */
    suspend fun readOnce() {
        var attempt = 0
        while (true) {
            val outcome = withContext(ioDispatcher) {
                val bytes = runCatching { renderer.nativeReadEnvironment(sessionHandle) }.getOrNull()
                if (bytes == null) {
                    EnvironmentReadOutcome.NoAnswer
                } else {
                    runCatching { EnvironmentBatchCodec.decodeSnapshot(bytes) }
                        .fold(
                            onSuccess = { EnvironmentReadOutcome.Decoded(it) },
                            onFailure = { EnvironmentReadOutcome.Undecodable(it.toString()) }
                        )
                }
            }

            when (outcome) {
                is EnvironmentReadOutcome.Decoded -> {
                    mutableEnvironment.value = outcome.snapshot
                    mutableConfirmed.value = outcome.snapshot
                    environmentReadFailure = null
                    break
                }

                EnvironmentReadOutcome.NoAnswer -> {
                    attempt++
                    environmentReadFailure = "the simulation did not answer"
                    if (attempt >= MAXIMUM_READ_ATTEMPTS) break
                    delay(READ_RETRY_MILLIS)
                }

                is EnvironmentReadOutcome.Undecodable -> {
                    // Retried once, because a half-written document is possible, and then left:
                    // decoding the same bytes again will keep producing the same answer, and
                    // repeating it forever would hide the disagreement behind a spinner.
                    attempt++
                    environmentReadFailure = "the environment could not be decoded: ${outcome.reason}"
                    if (attempt >= MAXIMUM_READ_ATTEMPTS) break
                    delay(READ_RETRY_MILLIS)
                }
            }
        }

        readStatsOnce()
    }

    /**
     * Reads the counters, and only the counters.
     *
     * The snapshot carries the cell count, the food mass, the elapsed simulation time and the
     * revision, as one consistent set: the runtime publishes them together, so a status line
     * built from the result cannot show a cell count from before an update beside a revision
     * from after it. It is the cheap read -- a lock-free consume of the runtime's latest
     * publication -- as opposed to the environment read, which asks the world's owner and
     * decodes a whole document.
     */
    suspend fun readStatsOnce() {
        val statsValues = withContext(ioDispatcher) {
            runCatching { renderer.nativeReadStats(sessionHandle) }.getOrNull()
        }
        if (statsValues != null && statsValues.size >= STATS_FIELD_COUNT) {
            mutableStats.value = SimulationStats(
                cellCount = statsValues[3].toInt(),
                // The native snapshot carries a mass, not a count of particles, so it is
                // reported as a mass. Calling it a count would be a different number.
                //
                // These two slots carry the bits of a double rather than a rounded number:
                // a whole-unit figure reports a plate that has food on it as empty, and the
                // phone reads the same fields the same way.
                foodMass = Double.fromBits(statsValues[4]),
                time = Double.fromBits(statsValues[5]),
                updateCount = statsValues[2]
            )
        }
    }

    /**
     * Keeps the counters current for as long as this call runs.
     *
     * The counters were read once, when the session was created, and never again: the substrate
     * panel then said "0,0 h, 0 cells, 0,0 nutrients" for ever while the world behind it filled up.
     * That is not a stale label, it is a screen reporting a moment that passed, and no amount of
     * reading the panel would have shown it -- the desktop had no loop at all, where the phone
     * publishes on every frame it is handed and throttles that to about seven times a second.
     *
     * Suspending rather than starting a thread of its own: the caller runs this in the effect that
     * owns the session, so it stops when the session does, and there is nothing to cancel, join or
     * leak. The interval is slower than the phone's because a document-less read of four numbers
     * does not need to be faster, and because a panel that is not on screen is not reading them
     * either -- the StateFlow simply holds the latest value.
     */
    suspend fun pollStats(intervalMillis: Long = STATS_POLL_INTERVAL_MILLIS) {
        while (true) {
            readStatsOnce()
            delay(intervalMillis)
        }
    }

    /**
     * Why the last environment read did not produce a document, or null if it did.
     *
     * Kept because "the panel is loading" and "the panel will never load" are different things to
     * a person and identical to the code. Whatever the reason, it is worth saying rather than
     * showing a message that implies something is still coming.
     */
    var environmentReadFailure: String? = null
        private set

    private sealed interface EnvironmentReadOutcome {
        data class Decoded(val snapshot: EnvironmentSnapshot) : EnvironmentReadOutcome

        data object NoAnswer : EnvironmentReadOutcome

        data class Undecodable(val reason: String) : EnvironmentReadOutcome
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
        readOnce()
        if (mutableEnvironment.value != null) {
            if (mutableState.value == SimulationSessionState.Opening) {
                mutableState.value = SimulationSessionState.Ready
            }
        }
        // Deliberately left as opening when nothing came back. The shared model has no failure
        // state, and claiming readiness for a world that has never answered is the fault this
        // replaced -- the panel kept saying it was loading while the session said it was ready.
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

    override suspend fun save(target: StoredSubstrateId?): SaveResult {
        val id = target ?: openedPlate ?: return SaveResult.Failure(
            CellularityError(
                CellularityErrorCode.INVALID_VALUE,
                "The plate needs a name before it can be saved."
            )
        )
        return when (val result = savePlate(id, replaceExisting = id == openedPlate)) {
            is StorageResult.Success -> SaveResult.Saved(id)
            is StorageResult.Failure -> SaveResult.Failure(
                CellularityError(
                    code = result.error.code.toCellularityErrorCode(),
                    message = result.error.detail.ifBlank { result.error.code.name }
                )
            )
        }
    }

    /**
     * Saves the running plate as [id], answering with the storage outcome itself.
     *
     * This is the form the play screen's own operations use, and it is the same call as the shared
     * [save]: a notice about a save is chosen by the storage code it failed with -- "that name is
     * taken" reads differently from "the disk refused it" -- and the shared error type has no room
     * for that distinction, so the richer answer is kept for the caller that shows it.
     *
     * [replaceExisting] is true only when [id] is the plate this session is already showing.
     * Overwriting anything else is refused by the repository, which is what keeps a name from
     * silently destroying a run the person is not looking at.
     */
    suspend fun savePlate(
        id: StoredSubstrateId,
        replaceExisting: Boolean
    ): StorageResult<Unit> {
        val repository = fileRepository ?: return StorageResult.Failure(
            StorageError(
                StorageErrorCode.IO_FAILURE,
                "This session was opened without a collection to save into."
            )
        )
        val result = repository.saveSubstrate(id = id, replaceExisting = replaceExisting) {
            temporary ->
            writeSubstrateInto(temporary)
        }
        if (result is StorageResult.Success) {
            // The world has not changed, so the revision has not either -- what changed is that the
            // file now describes it, which is the whole meaning of "saved".
            val current = mutableContentState.value
            mutableContentState.value = current.copy(persistedRevision = current.revision)
        }
        return result
    }

    /**
     * Writes the running world into [temporary], with its picture.
     *
     * The write itself is the phone's own call and is made where the world is, through the owner
     * queue -- which is why it is not made from the interface thread. What the world cannot know is
     * the picture: the preview frame is prepared by the save, but drawing it is renderer work and
     * only the surface's own thread may ask for it, so the pixels are taken through [previewCapture]
     * and encoded here.
     *
     * A plate without a picture is a plate, so every step after the write is best effort: a refused
     * frame, an encoder that answered nothing, or an attachment the format would not take all leave
     * the plate written and the save successful. The one thing that must not happen is a preview
     * frame left in the renderer's slot, which is why the token is always given back.
     *
     * A write that failed is a different matter, and it is reported with the writer's own words
     * rather than as a code: "IO_FAILURE" tells a person nothing about the plate that is still only
     * in memory.
     */
    private suspend fun writeSubstrateInto(temporary: java.nio.file.Path) {
        val path = temporary.toString()
        val tokens = LongArray(1)
        val problem = withContext(ioDispatcher) {
            renderer.nativeSaveSubstrate(sessionHandle, path, tokens)
        }
        if (problem != null) {
            throw java.io.IOException(problem)
        }
        val token = tokens[0]
        if (token == 0L) {
            return
        }
        try {
            val pixels = previewCapture?.capture(token) ?: return
            val side = renderer.nativeSubstratePreviewPixels()
            val webp = DesktopSubstratePreviewEncoder.encodeWebpLossy(
                rgbaBytes = pixels,
                pixelWidth = side,
                pixelHeight = side,
                rowStrideBytes = side * BYTES_PER_PIXEL
            ) ?: return
            withContext(ioDispatcher) {
                renderer.nativeAttachSubstratePreview(path, webp)
            }
        } finally {
            // Given back on every path, including the ones that decided against a picture. The
            // capture consumes the frame, so this is a no-op after one; before one it is the only
            // thing that releases the world the frame was holding.
            withContext(ioDispatcher) {
                renderer.nativeDiscardSubstratePreview(sessionHandle, token)
            }
        }
    }

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

    /**
     * The shared error code for a storage outcome.
     *
     * The two vocabularies are not the same size: storage knows about a name being taken, a file
     * being too large and a profile revoking access, and the simulation's own codes predate the
     * collection. What matters is that a caller using the shared interface is told the kind of
     * thing that went wrong rather than being told nothing, and that the ones the shared codes do
     * cover -- too large, unsupported, a newer file -- are named exactly.
     */
    private fun StorageErrorCode.toCellularityErrorCode(): CellularityErrorCode = when (this) {
        StorageErrorCode.TOO_LARGE -> CellularityErrorCode.LIMIT_EXCEEDED
        StorageErrorCode.UNSUPPORTED_FORMAT -> CellularityErrorCode.UNSUPPORTED_FEATURE
        StorageErrorCode.NEWER_VERSION -> CellularityErrorCode.NEWER_SNAPSHOT_VERSION
        StorageErrorCode.CORRUPTED -> CellularityErrorCode.TRUNCATED_INPUT
        StorageErrorCode.BAD_NAME,
        StorageErrorCode.NAME_EXISTS -> CellularityErrorCode.INVALID_VALUE
        StorageErrorCode.NOT_FOUND,
        StorageErrorCode.PERMISSION_REVOKED,
        StorageErrorCode.IO_FAILURE -> CellularityErrorCode.IO_FAILURE
    }

    private companion object {
        const val FAILED = 1

        /** A preview pixel is four bytes wide, which is how the capture lays them out. */
        const val BYTES_PER_PIXEL = 4

        /**
         * revision, worldEpoch, updates, cells, food bits, time bits, temperature, tool, cell,
         * failed, diagnosis requested.
         *
         * Two of the eleven are the bit pattern of a double rather than a number, which is why
         * this is a floor rather than an equality: a longer array from a later bridge is read the
         * same way, and the two fractional fields never move.
         */
        const val STATS_FIELD_COUNT = 11

        /**
         * How often the counters are re-read while a session is open.
         *
         * A quarter of a second. The phone publishes on every frame it is handed and throttles
         * that to about seven a second; a status line does not need more than four, and the read
         * is a consume of a value the runtime has already published rather than a request for it.
         */
        const val STATS_POLL_INTERVAL_MILLIS = 250L

    /**
     * How many times the opening read is tried before the session is left as opening.
     *
     * Twenty attempts at a quarter of a second is five seconds. A world that has not answered in
     * five seconds is not slow, it is not there -- and a person watching a panel that says it is
     * loading deserves the difference between "not yet" and "never", which is a distinction the
     * state now keeps instead of erasing.
     */
    /**
     * How many times one read is tried before it reports what went wrong.
     *
     * Ten attempts at a fifth of a second is two seconds. Beyond that the reason is kept and
     * shown, because a world that has not answered in two seconds is not late, it is absent --
     * and a message that implies something is still coming is worse than one that says what
     * happened.
     */
    const val MAXIMUM_READ_ATTEMPTS = 10

    /** How long to wait before trying a read again. */
    const val READ_RETRY_MILLIS = 200L
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
