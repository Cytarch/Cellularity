package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputBatchLayout
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressSnapshot
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressState
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressStatus
import com.cytarch.cellularity.platform.render.microscope.MicroscopeRenderEndpoint
import com.cytarch.cellularity.platform.render.microscope.NativeMicroscopeRenderAccess
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.CellularityResult
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.CellId
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.SaveResult
import com.cytarch.cellularity.shared.simulation.SimulationCommand
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import com.cytarch.cellularity.shared.simulation.SimulationResult
import com.cytarch.cellularity.shared.simulation.SimulationSessionState
import java.lang.ref.WeakReference
import java.util.concurrent.locks.ReentrantLock
import java.util.concurrent.locks.ReentrantReadWriteLock
import java.util.concurrent.atomic.AtomicBoolean
import java.nio.ByteBuffer
import kotlin.concurrent.withLock
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

internal interface AndroidSimulationRuntime {
    /** Opens the initial source exactly once. The runtime owns and closes that source. */
    fun open()

    fun environment(): EnvironmentSnapshot

    fun stats(): SimulationStats

    fun execute(command: SimulationCommand): SimulationResult

    fun prepareReplacement(command: SimulationCommand.Reload): PreparedRuntimeReplacement

    fun commitReplacement(
        prepared: PreparedRuntimeReplacement,
        expectedGeneration: Long
    ): RuntimeReplacementCommit

    fun applyEnvironment(patch: EnvironmentPatch): ApplyResult

    fun inspectCell(id: CellId): CellSnapshot?

    fun save(target: StoredSubstrateId): SaveResult

    fun detachForClose(): RetiredRuntime
}

internal interface PreparedRuntimeReplacement : AutoCloseable {
    val target: StoredSubstrateId
}

internal interface RetiredRuntime : AutoCloseable

internal data class RuntimeReplacementCommit(
    val retired: RetiredRuntime
)

/**
 * Single owner of one native simulation world.
 *
 * Lifecycle:
 *
 *     New -> Opening -> Ready <-> Paused
 *                         \----> Saving --/
 *       any non-terminal state -> Closing -> Closed
 *       native failure --------------------> Failed -> Closing -> Closed
 *
 * Gateway commands enter [commandQueue] in acceptance order. A surface token
 * owns only the render attachment; rendering never enters the runtime worker
 * lock, and destroying or replacing a surface never replaces or pauses the
 * native world.
 */
@OptIn(ExperimentalCoroutinesApi::class)
internal class AndroidSimulationGateway(
    private val runtime: AndroidSimulationRuntime,
    initialTarget: StoredSubstrateId?,
    commandDispatcher: CoroutineDispatcher = Dispatchers.IO.limitedParallelism(1),
    publicationDispatcher: CoroutineDispatcher = Dispatchers.IO.limitedParallelism(1),
    private val nanoTime: () -> Long = System::nanoTime
) : SimulationGateway, MicroscopeRuntimePublicationExecutor {
    private val lifecycleLock = Any()
    private val runtimeAccessLock = ReentrantLock(true)
    private val renderAttachmentLock = ReentrantReadWriteLock(true)
    private val renderReadLock = renderAttachmentLock.readLock()
    private val renderWriteLock = renderAttachmentLock.writeLock()
    private val renderAttachmentChanged = renderWriteLock.newCondition()
    private val commandQueue = Channel<QueuedSessionAction>(COMMAND_QUEUE_CAPACITY)
    private val commandJob = SupervisorJob()
    private val commandScope = CoroutineScope(
        commandJob + commandDispatcher + CoroutineName("CellularitySimulationSession")
    )
    private val publicationJob = SupervisorJob()
    private val publicationScope = CoroutineScope(
        publicationJob + publicationDispatcher +
            CoroutineName("CellularityRuntimePublication")
    )
    private val publicationAccepting = AtomicBoolean(true)
    private val publicationPollInFlight = AtomicBoolean(false)
    private val openCompletion = CompletableDeferred<CellularityResult<Unit>>()
    private val closeCompletion = CompletableDeferred<Unit>()
    private val initialEnvironment = runtime.environment()
    private val initialStats = runtime.stats()

    private var currentTarget = initialTarget
    private var closeQueued = false
    private var runtimeClosed = false
    private var restingState: SimulationSessionState = SimulationSessionState.Ready
    private var activeRenderAttachment: ActiveRenderAttachment? = null
    private var boundVulkanWorker: BoundVulkanWorker? = null
    private var completedVulkanWorker: CompletedVulkanWorker? = null
    private var nextSurfaceToken = 1L
    private val inputEpochLock = Any()
    private var nextInputEpoch = 1L
    private var lastStatsPublishNanos = 0L
    private var lastContentStats = initialStats
    private var pendingReloadTarget: StoredSubstrateId? = null
    private var pendingReset = false
    private var runtimeGeneration = 1L

    private val mutableState = MutableStateFlow<SimulationSessionState>(SimulationSessionState.New)
    override val state: StateFlow<SimulationSessionState> = mutableState.asStateFlow()

    private val mutableContentState = MutableStateFlow(
        if (initialTarget == null) {
            SimulationContentState.newWorld()
        } else {
            SimulationContentState.storedWorld()
        }
    )
    override val contentState: StateFlow<SimulationContentState> =
        mutableContentState.asStateFlow()

    private val mutableEnvironment = MutableStateFlow(initialEnvironment)
    override val environment: StateFlow<EnvironmentSnapshot> = mutableEnvironment.asStateFlow()

    private val mutableStats = MutableStateFlow(initialStats)
    override val stats: StateFlow<SimulationStats> = mutableStats.asStateFlow()

    init {
        commandScope.launch {
            for (action in commandQueue) {
                action.run()
            }
        }
    }

    internal suspend fun open(): CellularityResult<Unit> {
        synchronized(lifecycleLock) {
            when (val current = mutableState.value) {
                SimulationSessionState.New -> {
                    mutableState.value = SimulationSessionState.Opening
                    if (!commandQueue.trySend(QueuedSessionAction(::performOpen)).isSuccess) {
                        val error = closedError()
                        mutableState.value = SimulationSessionState.Failed(error)
                        openCompletion.complete(CellularityResult.Failure(error))
                    }
                }
                SimulationSessionState.Opening -> Unit
                SimulationSessionState.Ready,
                SimulationSessionState.Paused,
                SimulationSessionState.Saving -> {
                    openCompletion.complete(CellularityResult.Success(Unit))
                }
                is SimulationSessionState.Failed -> {
                    openCompletion.complete(CellularityResult.Failure(current.error))
                }
                SimulationSessionState.Closing,
                SimulationSessionState.Closed -> {
                    openCompletion.complete(CellularityResult.Failure(closedError()))
                }
            }
        }
        return openCompletion.await()
    }

    override suspend fun execute(command: SimulationCommand): SimulationResult = when (command) {
        is SimulationCommand.Reload -> enqueueReplacement(command)
        SimulationCommand.Reset -> enqueueCommand(SimulationResult::Failure) {
            runtime.execute(command).also { result ->
                if (result is SimulationResult.Queued) pendingReset = true
                if (result is SimulationResult.Failure) observeRuntimeFailure(result.error)
            }
        }
    }

    override suspend fun applyEnvironment(patch: EnvironmentPatch): ApplyResult =
        enqueueCommand(ApplyResult::Failure) {
            runtime.applyEnvironment(patch).also { result ->
                when (result) {
                    is ApplyResult.Applied -> publishConfirmedEnvironment(
                        expectedRevision = result.revision,
                        contentChanged = true
                    )
                    is ApplyResult.Rejected -> publishConfirmedEnvironment(
                        expectedRevision = result.currentRevision,
                        contentChanged = false
                    )
                    is ApplyResult.Failure -> Unit
                }
                if (result is ApplyResult.Failure) observeRuntimeFailure(result.error)
            }
        }

    override suspend fun inspectCell(id: CellId): CellSnapshot? =
        enqueueCommand({ null }) { runtime.inspectCell(id) }

    override suspend fun save(target: StoredSubstrateId?): SaveResult =
        enqueueCommand(SaveResult::Failure) { saveOnWorker(target) }

    override suspend fun close() {
        requestClose()
        closeCompletion.await()
    }

    /** Atomically stops accepting work and appends one close barrier to the queue. */
    internal fun requestClose() {
        var runFallback = false
        var stopPublications = false
        synchronized(lifecycleLock) {
            if (mutableState.value == SimulationSessionState.Closed || closeQueued) return
            mutableState.value = SimulationSessionState.Closing
            closeQueued = true
            stopPublications = publicationAccepting.getAndSet(false)
            runFallback = !commandQueue.trySend(
                QueuedSessionAction(::performClose)
            ).isSuccess
        }
        if (stopPublications) publicationJob.cancel()
        if (runFallback) performClose()
    }

    internal fun createRenderAttachment(
        simulation: MicroscopeSimulationBackend,
        render: NativeMicroscopeRenderAccess,
        backendKind: RenderBackendKind = RenderBackendKind.OPEN_GL_ES
    ): MicroscopeRenderEndpoint {
        val token = synchronized(lifecycleLock) {
            check(nextSurfaceToken > 0L) {
                "Render surface token generation is exhausted"
            }
            nextSurfaceToken.also { issued ->
                nextSurfaceToken = if (issued == Long.MAX_VALUE) 0L else issued + 1L
            }
        }
        return SessionMicroscopeRenderEndpoint(
            this, token, backendKind, simulation, render
        )
    }

    internal fun beginVulkanWorkerBinding(
        token: Long,
        owner: Any,
        operation: () -> Unit
    ): Boolean = renderWriteLock.withLock {
        if (!prepareRenderHandoffLocked(token)) return@withLock false
        if (!awaitVulkanBindingLocked(token, owner)) return@withLock false
        val current = boundVulkanWorker
        if (current?.token == token && current.owner === owner) return@withLock true
        try {
            operation()
            boundVulkanWorker = BoundVulkanWorker(token, owner)
            renderAttachmentChanged.signalAll()
            true
        } catch (failure: Throwable) {
            transitionToFailed(failure)
            false
        }
    }

    internal fun completeVulkanWorkerUnbind(
        token: Long,
        owner: Any
    ): Boolean = renderWriteLock.withLock {
        val current = boundVulkanWorker
        if (current?.token != token || current.owner !== owner) {
            // A retry may finish the same retired owner, never the replacement.
            val completed = completedVulkanWorker
            return@withLock completed?.token == token && completed.owner.get() === owner
        }
        if (activeRenderAttachment?.token == token && !detachActiveRenderAttachmentLocked()) {
            return@withLock false
        }
        boundVulkanWorker = null
        completedVulkanWorker = CompletedVulkanWorker(token, WeakReference(owner))
        renderAttachmentChanged.signalAll()
        true
    }

    internal fun attachSurface(
        token: Long,
        backendKind: RenderBackendKind,
        attachOperation: () -> Long?,
        detachOperation: () -> Unit,
        requestInputResync: () -> Boolean,
        completeInputResync: (Boolean) -> Long,
        activePointerCount: () -> Int
    ): Boolean = renderWriteLock.withLock {
        if (!prepareRenderHandoffLocked(token)) return@withLock false
        if (!awaitCompatibleBackendBindingLocked(token, backendKind)) return@withLock false
        if (synchronized(lifecycleLock) { commandRejectionLocked() } != null) {
            return@withLock false
        }
        var attachAttempted = false
        try {
            attachAttempted = true
            val leaseInputEpoch = attachOperation()
            if (leaseInputEpoch == null) {
                attachAttempted = false
                return@withLock false
            }
            if (synchronized(lifecycleLock) { commandRejectionLocked() } != null) {
                runCatching(detachOperation)
                return@withLock false
            }
            val hasActivePointers = activePointerCount() != 0
            val initialEpoch = if (hasActivePointers) {
                0L
            } else {
                leaseInputEpoch.takeIf { it != 0L } ?: allocateInputEpoch()
            }
            val initialInputState = when {
                hasActivePointers -> MicroscopeInputIngressState.RESYNCING
                initialEpoch == 0L -> MicroscopeInputIngressState.CLOSING
                else -> MicroscopeInputIngressState.ACCEPTING
            }
            val attachment = ActiveRenderAttachment(
                token = token,
                detach = detachOperation,
                requestInputResync = requestInputResync,
                completeInputResync = completeInputResync,
                activePointerCount = activePointerCount,
                inputEpoch = initialEpoch,
                inputState = initialInputState
            )
            activeRenderAttachment = attachment
            if (hasActivePointers) {
                beginInputResync(attachment, requestInputResync)
            }
            renderAttachmentChanged.signalAll()
            true
        } catch (failure: Throwable) {
            if (attachAttempted) {
                try {
                    detachOperation()
                } catch (cleanupFailure: Throwable) {
                    if (cleanupFailure !== failure) {
                        failure.addSuppressed(cleanupFailure)
                    }
                }
            }
            transitionToFailed(failure)
            false
        }
    }

    internal fun detachSurface(token: Long) = renderWriteLock.withLock {
        if (activeRenderAttachment?.token == token) {
            detachActiveRenderAttachmentLocked()
        }
    }

    internal fun <T> withRenderAccess(
        token: Long,
        fallback: T,
        operation: () -> T
    ): T = renderReadLock.withLock {
        val allowed = activeRenderAttachment?.token == token &&
            synchronized(lifecycleLock) { commandRejectionLocked() == null }
        if (!allowed) return@withLock fallback
        try {
            operation()
        } catch (failure: Throwable) {
            transitionToFailed(failure)
            fallback
        }
    }

    internal fun <T> withRenderEndpointAccess(
        fallback: T,
        operation: () -> T
    ): T = renderReadLock.withLock {
        if (synchronized(lifecycleLock) { commandRejectionLocked() } != null) {
            return@withLock fallback
        }
        try {
            operation()
        } catch (failure: Throwable) {
            transitionToFailed(failure)
            fallback
        }
    }

    internal fun <T> tryWithRenderAccess(
        token: Long,
        fallback: T,
        operation: () -> T
    ): T {
        if (!renderReadLock.tryLock()) return fallback
        try {
            val allowed = activeRenderAttachment?.token == token &&
                synchronized(lifecycleLock) { commandRejectionLocked() == null }
            if (!allowed) return fallback
            return try {
                operation()
            } catch (failure: Throwable) {
                transitionToFailed(failure)
                fallback
            }
        } finally {
            renderReadLock.unlock()
        }
    }

    internal fun submitMicroscopeInputBatch(
        token: Long,
        expectedInputEpoch: Long,
        batchSequence: Long,
        result: ByteBuffer,
        operation: () -> Unit
    ): MicroscopeInputIngressStatus {
        if (!renderReadLock.tryLock()) {
            MicroscopeInputBatchLayout.writeIngressResult(
                result,
                batchSequence,
                0L,
                MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE
            )
            return MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE
        }
        try {
            val attachment = activeRenderAttachment
            if (attachment?.token != token) {
                MicroscopeInputBatchLayout.writeIngressResult(
                    result,
                    batchSequence,
                    0L,
                    MicroscopeInputIngressStatus.INACTIVE_SURFACE
                )
                return MicroscopeInputIngressStatus.INACTIVE_SURFACE
            }
            if (synchronized(lifecycleLock) { commandRejectionLocked() } != null) {
                MicroscopeInputBatchLayout.writeIngressResult(
                    result,
                    batchSequence,
                    0L,
                    MicroscopeInputIngressStatus.CLOSING
                )
                synchronized(attachment.inputLock) {
                    attachment.inputState = MicroscopeInputIngressState.CLOSING
                }
                return MicroscopeInputIngressStatus.CLOSING
            }
            var dispatchFailure: Throwable? = null
            val dispatchStatus = synchronized(attachment.inputLock) {
                val rejection = when {
                    attachment.inputState == MicroscopeInputIngressState.RESYNCING ->
                        MicroscopeInputIngressStatus.RESYNCING
                    attachment.inputState == MicroscopeInputIngressState.CLOSING ->
                        MicroscopeInputIngressStatus.CLOSING
                    attachment.inputState != MicroscopeInputIngressState.ACCEPTING ->
                        MicroscopeInputIngressStatus.INACTIVE_SURFACE
                    expectedInputEpoch != attachment.inputEpoch ->
                        MicroscopeInputIngressStatus.STALE_INPUT_EPOCH
                    else -> null
                }
                if (rejection != null) {
                    MicroscopeInputBatchLayout.writeIngressResult(
                        result,
                        batchSequence,
                        attachment.inputEpoch,
                        rejection
                    )
                    return@synchronized rejection
                }
                try {
                    // Keep epoch validation and the short synchronous JNI
                    // dispatch atomic with respect to resync/detach state.
                    operation()
                    MicroscopeInputIngressStatus.APPLIED
                } catch (failure: Throwable) {
                    attachment.inputState = MicroscopeInputIngressState.CLOSING
                    dispatchFailure = failure
                    MicroscopeInputBatchLayout.writeIngressResult(
                        result,
                        batchSequence,
                        attachment.inputEpoch,
                        MicroscopeInputIngressStatus.INTERNAL_FAILURE
                    )
                    MicroscopeInputIngressStatus.INTERNAL_FAILURE
                }
            }
            dispatchFailure?.let(::transitionToFailed)
            return dispatchStatus
        } finally {
            renderReadLock.unlock()
        }
    }

    internal fun readMicroscopePresentationMotionSnapshot(
        token: Long,
        result: ByteBuffer,
        operation: () -> Boolean
    ): Boolean {
        if (!renderReadLock.tryLock()) return false
        try {
            val allowed = activeRenderAttachment?.token == token &&
                synchronized(lifecycleLock) { commandRejectionLocked() == null }
            if (!allowed) return false
            return try {
                operation()
            } catch (failure: Throwable) {
                transitionToFailed(failure)
                false
            }
        } finally {
            renderReadLock.unlock()
        }
    }

    internal fun requestMicroscopeInputResync(token: Long): Boolean {
        if (!renderReadLock.tryLock()) return false
        try {
            val attachment = activeRenderAttachment
            if (attachment?.token != token) return false
            return beginInputResync(attachment, attachment.requestInputResync)
        } finally {
            renderReadLock.unlock()
        }
    }

    internal fun microscopeInputIngressSnapshot(
        token: Long,
        ensureResync: Boolean
    ): MicroscopeInputIngressSnapshot {
        if (!renderReadLock.tryLock()) {
            return MicroscopeInputIngressSnapshot.INACTIVE
        }
        try {
            val attachment = activeRenderAttachment
            if (attachment?.token != token) {
                return if (synchronized(lifecycleLock) {
                        commandRejectionLocked() != null
                    }
                ) {
                    MicroscopeInputIngressSnapshot.CLOSING
                } else {
                    MicroscopeInputIngressSnapshot.INACTIVE
                }
            }
            if (ensureResync) {
                beginInputResync(attachment, attachment.requestInputResync)
            }
            synchronized(attachment.inputLock) {
                if (attachment.inputState == MicroscopeInputIngressState.RESYNCING &&
                    attachment.resyncCommandIssued &&
                    attachment.activePointerCount() == 0
                ) {
                    val leaseEpoch = attachment.completeInputResync(true)
                    val nextEpoch = leaseEpoch.takeIf { it != 0L }
                        ?: allocateInputEpoch()
                    if (nextEpoch == 0L) {
                        attachment.inputState = MicroscopeInputIngressState.CLOSING
                    } else {
                        attachment.inputEpoch = nextEpoch
                        attachment.inputState = MicroscopeInputIngressState.ACCEPTING
                        attachment.resyncCommandIssued = false
                    }
                }
                return MicroscopeInputIngressSnapshot(
                    attachment.inputState,
                    attachment.inputEpoch
                )
            }
        } finally {
            renderReadLock.unlock()
        }
    }

    internal fun frameWithRenderAccess(
        token: Long,
        operation: () -> Long
    ): Long {
        renderReadLock.lock()
        try {
            val allowed = activeRenderAttachment?.token == token &&
                synchronized(lifecycleLock) { commandRejectionLocked() == null }
            if (!allowed) {
                return com.cytarch.cellularity.platform.render.RenderFrameResultCodec
                    .SKIPPED_NOT_READY_WIRE
            }
            return try {
                operation()
            } catch (failure: Throwable) {
                transitionToFailed(failure)
                com.cytarch.cellularity.platform.render.RenderFrameResultCodec.INVALID_WIRE
            }
        } finally {
            renderReadLock.unlock()
        }
    }

    internal fun setLifecyclePaused(
        token: Long,
        paused: Boolean,
        operation: () -> Unit
    ) = renderReadLock.withLock {
        val allowed = activeRenderAttachment?.token == token &&
            synchronized(lifecycleLock) { commandRejectionLocked() == null }
        if (!allowed) return@withLock
        try {
            operation()
            synchronized(lifecycleLock) {
                if (activeRenderAttachment?.token != token || commandRejectionLocked() != null) {
                    return@withLock
                }
                restingState = if (paused) {
                    SimulationSessionState.Paused
                } else {
                    SimulationSessionState.Ready
                }
                if (mutableState.value == SimulationSessionState.Ready ||
                    mutableState.value == SimulationSessionState.Paused
                ) {
                    mutableState.value = restingState
                }
            }
        } catch (failure: Throwable) {
            transitionToFailed(failure)
        }
    }

    internal fun <T> withRuntimeAccess(fallback: T, operation: () -> T): T =
        runtimeAccessLock.withLock {
            if (synchronized(lifecycleLock) { commandRejectionLocked() } != null) {
                return@withLock fallback
            }
            try {
                operation()
            } catch (failure: Throwable) {
                transitionToFailed(failure)
                fallback
            }
        }

    override fun tryStartPublicationPoll(
        viewGeneration: Long,
        poll: () -> MicroscopeFrameSnapshot?,
        completion: (Long, MicroscopeFrameSnapshot?) -> Unit
    ): Boolean {
        if (!publicationAccepting.get() ||
            !publicationPollInFlight.compareAndSet(false, true)
        ) {
            return false
        }
        if (!publicationAccepting.get()) {
            publicationPollInFlight.set(false)
            return false
        }

        val job = publicationScope.launch {
            try {
                var snapshot: MicroscopeFrameSnapshot? = null
                if (publicationAccepting.get() && runtimeAccessLock.tryLock()) {
                    snapshot = try {
                        val allowed = synchronized(lifecycleLock) {
                            commandRejectionLocked() == null
                        }
                        if (allowed) poll() else null
                    } catch (failure: Throwable) {
                        transitionToFailed(failure)
                        null
                    } finally {
                        runtimeAccessLock.unlock()
                    }
                }
                if (publicationAccepting.get()) {
                    runCatching { completion(viewGeneration, snapshot) }
                }
            } finally {
                publicationPollInFlight.set(false)
            }
        }
        job.invokeOnCompletion { publicationPollInFlight.set(false) }
        return true
    }

    internal fun publishFrame(
        frameStats: SimulationStats,
        changedEnvironment: EnvironmentSnapshot?
    ) {
        synchronized(lifecycleLock) {
            if (commandRejectionLocked() != null) return
            val statsChanged = frameStats != lastContentStats
            lastContentStats = frameStats

            if (changedEnvironment != null) {
                mutableEnvironment.value = changedEnvironment
            }

            if (statsChanged || changedEnvironment != null) {
                val reloadTarget = pendingReloadTarget
                val resetApplied = pendingReset
                if (changedEnvironment != null) {
                    pendingReloadTarget = null
                    pendingReset = false
                }

                when {
                    changedEnvironment != null && resetApplied -> markReplacementDirtyLocked()
                    changedEnvironment != null && reloadTarget != null -> {
                        currentTarget = reloadTarget
                        markReplacementPersistedLocked()
                    }
                    else -> markContentChangedLocked()
                }
            }

            val now = nanoTime()
            val previous = mutableStats.value
            if (previous.cellCount != frameStats.cellCount ||
                previous.foodMass != frameStats.foodMass ||
                now - lastStatsPublishNanos >= STATS_PUBLISH_INTERVAL_NANOS
            ) {
                mutableStats.value = frameStats
                lastStatsPublishNanos = now
            }
        }
    }

    private fun performOpen() {
        runtimeAccessLock.withLock {
            val rejection = synchronized(lifecycleLock) {
                if (mutableState.value == SimulationSessionState.Opening) null else commandRejectionLocked()
            }
            if (rejection != null) {
                openCompletion.complete(CellularityResult.Failure(rejection))
                return
            }

            try {
                runtime.open()
                val openedEnvironment = runtime.environment()
                val openedStats = runtime.stats()
                val result = synchronized(lifecycleLock) {
                    if (mutableState.value == SimulationSessionState.Opening) {
                        mutableEnvironment.value = openedEnvironment
                        mutableStats.value = openedStats
                        lastContentStats = openedStats
                        restingState = SimulationSessionState.Ready
                        mutableState.value = SimulationSessionState.Ready
                        CellularityResult.Success(Unit)
                    } else {
                        CellularityResult.Failure(closedError())
                    }
                }
                openCompletion.complete(result)
            } catch (failure: Throwable) {
                val error = transitionToFailed(failure)
                openCompletion.complete(CellularityResult.Failure(error))
            }
        }
    }

    private suspend fun <T> enqueueCommand(
        failureResult: (CellularityError) -> T,
        operation: () -> T
    ): T {
        val completion = CompletableDeferred<T>()
        val action = QueuedSessionAction {
            runtimeAccessLock.withLock {
                val rejection = synchronized(lifecycleLock) { commandRejectionLocked() }
                if (rejection != null) {
                    completion.complete(failureResult(rejection))
                    return@withLock
                }
                try {
                    completion.complete(operation())
                } catch (failure: Throwable) {
                    completion.complete(failureResult(transitionToFailed(failure)))
                }
            }
        }

        val immediateRejection = synchronized(lifecycleLock) {
            commandRejectionLocked() ?: if (commandQueue.trySend(action).isSuccess) {
                null
            } else {
                closedError()
            }
        }
        if (immediateRejection != null) {
            completion.complete(failureResult(immediateRejection))
        }
        return completion.await()
    }

    private suspend fun enqueueReplacement(
        command: SimulationCommand.Reload
    ): SimulationResult {
        val completion = CompletableDeferred<SimulationResult>()
        val action = QueuedSessionAction {
            var prepared: PreparedRuntimeReplacement? = null
            var retired: RetiredRuntime? = null
            var commitAttempted = false
            var postBarrierFailure: Throwable? = null
            var result: SimulationResult = SimulationResult.Failure(
                CellularityError(
                    CellularityErrorCode.INTERNAL_FAILURE,
                    "Reload did not produce a result"
                )
            )
            try {
                val rejection = synchronized(lifecycleLock) {
                    commandRejectionLocked()
                }
                if (rejection != null) {
                    completion.complete(SimulationResult.Failure(rejection))
                    return@QueuedSessionAction
                }

                val expectedGeneration = runtimeGeneration
                prepared = runtime.prepareReplacement(command)

                renderWriteLock.lock()
                try {
                    runtimeAccessLock.lock()
                    try {
                        val commitRejection = synchronized(lifecycleLock) {
                            commandRejectionLocked()
                        }
                        if (commitRejection != null || runtimeGeneration != expectedGeneration) {
                            result = SimulationResult.Failure(
                                commitRejection ?: CellularityError(
                                    CellularityErrorCode.INTERNAL_FAILURE,
                                    "Simulation runtime generation changed during reload"
                                )
                            )
                        } else {
                            val committedGeneration = nextRuntimeGeneration(
                                expectedGeneration
                            )
                            commitAttempted = true
                            val committed = runtime.commitReplacement(
                                prepared!!,
                                expectedGeneration
                            )
                            retired = committed.retired
                            synchronized(lifecycleLock) {
                                runtimeGeneration = committedGeneration
                                pendingReloadTarget = prepared!!.target
                            }
                            result = SimulationResult.Queued
                        }
                    } finally {
                        if (commitAttempted) {
                            try {
                                resynchronizeActiveAttachmentAfterControllerBarrierLocked()
                            } catch (failure: Throwable) {
                                postBarrierFailure = failure
                            }
                        }
                        runtimeAccessLock.unlock()
                    }
                } finally {
                    renderWriteLock.unlock()
                }
                postBarrierFailure?.let(::transitionToFailed)
            } catch (failure: Throwable) {
                postBarrierFailure
                    ?.takeUnless { it === failure }
                    ?.let(failure::addSuppressed)
                val error = failure.toCellularityError()
                observeRuntimeFailure(error)
                result = SimulationResult.Failure(error)
            } finally {
                runCatching { retired?.close() }
                runCatching { prepared?.close() }
            }
            completion.complete(result)
        }

        val immediateRejection = synchronized(lifecycleLock) {
            commandRejectionLocked() ?: if (commandQueue.trySend(action).isSuccess) {
                null
            } else {
                closedError()
            }
        }
        if (immediateRejection != null) {
            completion.complete(SimulationResult.Failure(immediateRejection))
        }
        return completion.await()
    }

    private fun resynchronizeActiveAttachmentAfterControllerBarrierLocked() {
        val attachment = activeRenderAttachment ?: return
        if (!beginInputResync(attachment, attachment.requestInputResync)) return
        synchronized(attachment.inputLock) {
            if (attachment.inputState != MicroscopeInputIngressState.RESYNCING ||
                !attachment.resyncCommandIssued ||
                attachment.activePointerCount() != 0
            ) {
                return
            }
            val leaseEpoch = attachment.completeInputResync(true)
            val nextEpoch = leaseEpoch.takeIf { it != 0L } ?: allocateInputEpoch()
            if (nextEpoch == 0L) {
                attachment.inputState = MicroscopeInputIngressState.CLOSING
            } else {
                attachment.inputEpoch = nextEpoch
                attachment.inputState = MicroscopeInputIngressState.ACCEPTING
                attachment.resyncCommandIssued = false
            }
        }
    }

    private fun nextRuntimeGeneration(current: Long): Long {
        check(current > 0L && current != Long.MAX_VALUE) {
            "Simulation runtime generation is exhausted"
        }
        return current + 1L
    }

    private fun saveOnWorker(target: StoredSubstrateId?): SaveResult {
        val resolvedTarget = target ?: currentTarget ?: return SaveResult.Failure(
            CellularityError(
                CellularityErrorCode.INVALID_VALUE,
                "A substrate target is required for the first save"
            )
        )

        synchronized(lifecycleLock) {
            if (commandRejectionLocked() == null) {
                mutableState.value = SimulationSessionState.Saving
            }
        }
        return try {
            runtime.save(resolvedTarget).also { result ->
                if (result is SaveResult.Saved) {
                    currentTarget = result.target
                    synchronized(lifecycleLock) { markCurrentContentPersistedLocked() }
                }
                if (result is SaveResult.Failure) observeRuntimeFailure(result.error)
            }
        } finally {
            synchronized(lifecycleLock) {
                if (mutableState.value == SimulationSessionState.Saving) {
                    mutableState.value = restingState
                }
            }
        }
    }

    private fun observeRuntimeFailure(error: CellularityError) {
        if (error.code == CellularityErrorCode.INTERNAL_FAILURE ||
            error.code == CellularityErrorCode.OUT_OF_MEMORY
        ) {
            synchronized(lifecycleLock) {
                if (commandRejectionLocked() == null) {
                    mutableState.value = SimulationSessionState.Failed(error)
                }
            }
        }
    }

    private fun publishConfirmedEnvironment(
        expectedRevision: Long,
        contentChanged: Boolean
    ) {
        val confirmed = runtime.environment()
        check(confirmed.revision == expectedRevision) {
            "Environment result revision $expectedRevision does not match snapshot ${confirmed.revision}"
        }
        synchronized(lifecycleLock) {
            if (commandRejectionLocked() == null) {
                mutableEnvironment.value = confirmed
                if (contentChanged) markContentChangedLocked()
            }
        }
    }

    private fun markContentChangedLocked() {
        val current = mutableContentState.value
        if (current.isDirty) return
        mutableContentState.value = current.copy(revision = current.revision + 1)
    }

    private fun markCurrentContentPersistedLocked() {
        val current = mutableContentState.value
        mutableContentState.value = current.copy(persistedRevision = current.revision)
    }

    private fun markReplacementDirtyLocked() {
        val current = mutableContentState.value
        mutableContentState.value = current.copy(revision = current.revision + 1)
    }

    private fun markReplacementPersistedLocked() {
        val nextRevision = mutableContentState.value.revision + 1
        mutableContentState.value = SimulationContentState(
            revision = nextRevision,
            persistedRevision = nextRevision
        )
    }

    private fun transitionToFailed(failure: Throwable): CellularityError {
        val error = failure.toCellularityError()
        synchronized(lifecycleLock) {
            when (mutableState.value) {
                SimulationSessionState.Closing,
                SimulationSessionState.Closed -> return closedError()
                else -> mutableState.value = SimulationSessionState.Failed(error)
            }
        }
        return error
    }

    /**
     * Stops stale callbacks before a replacement backend mutates native render
     * ownership. The write lock first drains every in-flight draw, then the
     * previous detach callback abandons only the backend that is still current.
     */
    private fun prepareRenderHandoffLocked(token: Long): Boolean {
        val current = activeRenderAttachment
        if (current != null && current.token != token) {
            if (!detachActiveRenderAttachmentLocked()) return false
        }
        return synchronized(lifecycleLock) { commandRejectionLocked() == null }
    }

    private fun detachActiveRenderAttachmentLocked(): Boolean {
        val current = activeRenderAttachment ?: return true
        beginInputResync(current, current.requestInputResync)
        synchronized(current.inputLock) {
            current.inputState = MicroscopeInputIngressState.INACTIVE_SURFACE
            current.inputEpoch = 0L
        }
        activeRenderAttachment = null
        return try {
            current.detach()
            renderAttachmentChanged.signalAll()
            true
        } catch (failure: Throwable) {
            renderAttachmentChanged.signalAll()
            transitionToFailed(failure)
            false
        }
    }

    private fun beginInputResync(
        attachment: ActiveRenderAttachment,
        operation: () -> Boolean
    ): Boolean {
        var operationFailure: Throwable? = null
        val accepted = synchronized(attachment.inputLock) {
            when (attachment.inputState) {
                MicroscopeInputIngressState.CLOSING,
                MicroscopeInputIngressState.INACTIVE_SURFACE -> return@synchronized false
                MicroscopeInputIngressState.ACCEPTING -> {
                    attachment.inputState = MicroscopeInputIngressState.RESYNCING
                }
                MicroscopeInputIngressState.RESYNCING -> Unit
            }
            if (attachment.resyncCommandIssued) return@synchronized true
            val issued = try {
                operation()
            } catch (failure: Throwable) {
                operationFailure = failure
                false
            }
            if (!issued) {
                attachment.inputState = MicroscopeInputIngressState.CLOSING
                return@synchronized false
            }
            attachment.resyncCommandIssued = true
            true
        }
        operationFailure?.let(::transitionToFailed)
        return accepted
    }

    private fun allocateInputEpoch(): Long = synchronized(inputEpochLock) {
        val epoch = nextInputEpoch
        if (epoch == 0L) return@synchronized 0L
        nextInputEpoch += 1L
        epoch
    }

    private fun awaitVulkanBindingLocked(token: Long, owner: Any): Boolean =
        awaitRenderHandoffLocked(
            ready = {
                val current = boundVulkanWorker
                current == null || (current.token == token && current.owner === owner)
            },
            timeoutMessage = "The previous Vulkan render worker did not detach in time"
        )

    private fun awaitCompatibleBackendBindingLocked(
        token: Long,
        backendKind: RenderBackendKind
    ): Boolean = when (backendKind) {
        RenderBackendKind.OPEN_GL_ES -> awaitRenderHandoffLocked(
            ready = { boundVulkanWorker == null },
            timeoutMessage = "The Vulkan render worker did not detach before GLES attachment"
        )
        RenderBackendKind.VULKAN -> {
            val current = boundVulkanWorker
            if (current?.token == token) {
                true
            } else {
                transitionToFailed(
                    IllegalStateException(
                        "The Vulkan surface was attached before its native worker was bound"
                    )
                )
                false
            }
        }
    }

    private fun awaitRenderHandoffLocked(
        ready: () -> Boolean,
        timeoutMessage: String
    ): Boolean {
        var remaining = RENDER_HANDOFF_TIMEOUT_NANOS
        while (!ready()) {
            if (synchronized(lifecycleLock) { commandRejectionLocked() } != null) return false
            if (remaining <= 0L) {
                transitionToFailed(IllegalStateException(timeoutMessage))
                return false
            }
            remaining = try {
                renderAttachmentChanged.awaitNanos(remaining)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                transitionToFailed(
                    IllegalStateException("Interrupted during renderer backend handoff", interrupted)
                )
                return false
            }
        }
        return synchronized(lifecycleLock) { commandRejectionLocked() == null }
    }

    private fun performClose() {
        publicationAccepting.set(false)
        publicationJob.cancel()
        var retired: RetiredRuntime? = null
        renderWriteLock.withLock {
            // Closing follows the same input teardown protocol as every other
            // surface detach: enqueue terminal cancels before the native
            // endpoint is detached and can no longer accept them.
            detachActiveRenderAttachmentLocked()
            renderAttachmentChanged.signalAll()
            runtimeAccessLock.withLock {
                if (!runtimeClosed) {
                    retired = runCatching(runtime::detachForClose).getOrNull()
                    runtimeClosed = true
                }
            }
            // The stopped worker's finalizer still owns this record. Closing
            // the runtime must not make its later unbind look like a stale owner.
        }
        runCatching { retired?.close() }
        synchronized(lifecycleLock) {
            mutableState.value = SimulationSessionState.Closed
            if (!openCompletion.isCompleted) {
                openCompletion.complete(CellularityResult.Failure(closedError()))
            }
        }
        closeCompletion.complete(Unit)
        commandQueue.close()
        commandJob.cancel()
    }

    private fun commandRejectionLocked(): CellularityError? = when (val current = mutableState.value) {
        SimulationSessionState.Ready,
        SimulationSessionState.Paused,
        SimulationSessionState.Saving -> null
        SimulationSessionState.New,
        SimulationSessionState.Opening -> CellularityError(
            CellularityErrorCode.SESSION_NOT_READY,
            "Simulation session is not ready"
        )
        SimulationSessionState.Closing,
        SimulationSessionState.Closed -> closedError()
        is SimulationSessionState.Failed -> current.error
    }

    private fun closedError() = CellularityError(
        CellularityErrorCode.SESSION_CLOSED,
        "Simulation session is closing or closed"
    )

    private class QueuedSessionAction(val run: () -> Unit)

    private data class ActiveRenderAttachment(
        val token: Long,
        val detach: () -> Unit,
        val requestInputResync: () -> Boolean,
        val completeInputResync: (Boolean) -> Long,
        val activePointerCount: () -> Int,
        val inputLock: Any = Any(),
        var inputEpoch: Long,
        var inputState: MicroscopeInputIngressState =
            MicroscopeInputIngressState.ACCEPTING,
        var resyncCommandIssued: Boolean = false
    )

    private data class BoundVulkanWorker(
        val token: Long,
        val owner: Any
    )

    private data class CompletedVulkanWorker(
        val token: Long,
        val owner: WeakReference<Any>
    )

    private companion object {
        const val COMMAND_QUEUE_CAPACITY = 64
        const val STATS_PUBLISH_INTERVAL_NANOS = 150_000_000L
        const val RENDER_HANDOFF_TIMEOUT_NANOS = 5_000_000_000L
    }
}
