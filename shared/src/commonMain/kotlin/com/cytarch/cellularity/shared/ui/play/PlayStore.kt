package com.cytarch.cellularity.shared.ui.play

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.ui.state.Store
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

enum class PlayPhase {
    NOT_STARTED,
    OPENING,
    READY,
    FAILED
}

enum class PlayTab {
    SUBSTRATE,
    MICROSCOPE,
    GENOME
}

enum class PlayDialog {
    NONE,
    PLATE_ACTIONS,
    SAVE_PLATE,
    CONFIRM_RELOAD,
    CONFIRM_RESET,
    CONFIRM_LEAVE
}

enum class PlayPlateAction {
    SAVE,
    RELOAD,
    STERILIZE
}

enum class PlayOperation {
    NONE,
    SAVING,
    RELOADING,
    RESETTING
}

enum class PlayPendingContentCommand {
    RELOAD,
    RESET
}

data class PendingPlayContentChange(
    val command: PlayPendingContentCommand,
    val issuedAtRevision: Long
)

data class PlayRestoredUiState(
    val selectedTab: PlayTab = PlayTab.SUBSTRATE,
    val dialog: PlayDialog = PlayDialog.NONE,
    val saveName: String = ""
)

data class PlayPersistenceState(
    val source: PlayLaunchSource,
    val ui: PlayRestoredUiState
)

data class PlayState(
    val source: PlayLaunchSource,
    val phase: PlayPhase,
    val selectedTab: PlayTab,
    val dialog: PlayDialog,
    val saveName: String,
    val operation: PlayOperation,
    val contentState: SimulationContentState,
    val pendingContentChange: PendingPlayContentChange? = null
) {
    val substrateId: StoredSubstrateId?
        get() = (source as? PlayLaunchSource.Stored)?.id

    val substrateName: String?
        get() = substrateId?.value

    val isDirty: Boolean
        get() = pendingContentChange != null || contentState.isDirty

    val isBusy: Boolean
        get() = operation != PlayOperation.NONE

    fun persistenceState(): PlayPersistenceState = PlayPersistenceState(
        source = source,
        ui = PlayRestoredUiState(
            selectedTab = selectedTab,
            dialog = dialog,
            saveName = saveName
        )
    )

    companion object {
        fun initial(
            source: PlayLaunchSource,
            restoredUi: PlayRestoredUiState = PlayRestoredUiState()
        ): PlayState {
            val targetName = (source as? PlayLaunchSource.Stored)?.id?.value.orEmpty()
            val restoredDialog = when {
                restoredUi.dialog == PlayDialog.CONFIRM_RELOAD && targetName.isEmpty() ->
                    PlayDialog.NONE
                else -> restoredUi.dialog
            }
            return PlayState(
                source = source,
                phase = PlayPhase.NOT_STARTED,
                selectedTab = restoredUi.selectedTab,
                dialog = restoredDialog,
                saveName = restoredUi.saveName.ifEmpty { targetName },
                operation = PlayOperation.NONE,
                contentState = when (source) {
                    is PlayLaunchSource.New -> SimulationContentState.newWorld()
                    is PlayLaunchSource.Stored -> SimulationContentState.storedWorld()
                }
            )
        }
    }
}

enum class PlayFailureOperation {
    OPEN,
    SAVE,
    RELOAD,
    RESET
}

enum class PlayFeedback {
    BAD_NAME,
    NAME_EXISTS,
    FILE_TOO_LARGE,
    NEWER_VERSION,
    LOAD_FAILED,
    SAVE_FAILED,
    RELOAD_FAILED,
    RESET_FAILED,
    SAVED
}

sealed interface PlayFailureCause {
    data class Storage(val error: StorageError) : PlayFailureCause

    data class Simulation(val error: CellularityError) : PlayFailureCause
}

data class PlayNotice(
    val operation: PlayFailureOperation,
    val feedback: PlayFeedback,
    val cause: PlayFailureCause? = null
)

enum class PlayFinishReason {
    USER,
    OPEN_FAILED,
    NEWER_VERSION
}

sealed interface PlayEffect {
    data class ShowNotice(val notice: PlayNotice) : PlayEffect

    data class Finish(val reason: PlayFinishReason) : PlayEffect
}

sealed interface PlayAction {
    data object Start : PlayAction
    data class SelectTab(val tab: PlayTab) : PlayAction
    data object RequestPlateActions : PlayAction
    data class SelectPlateAction(
        val action: PlayPlateAction,
        val confirmationRequired: Boolean
    ) : PlayAction
    data object DismissDialog : PlayAction
    data object BackRequested : PlayAction
    data object ConfirmLeave : PlayAction
    data class SaveNameChanged(val value: String) : PlayAction
    data object ConfirmSave : PlayAction
    data object ConfirmReload : PlayAction
    data object ConfirmReset : PlayAction
    data class ContentStateObserved(val value: SimulationContentState) : PlayAction
    data class SessionFailed(val error: CellularityError) : PlayAction

    data class SessionOpened(val content: SimulationContentState) : PlayAction
    data class SessionOpenFailed(val cause: PlayFailureCause) : PlayAction
    data class SaveSucceeded(
        val target: StoredSubstrateId,
        val content: SimulationContentState
    ) : PlayAction
    data object SaveTargetExists : PlayAction
    data class OperationFailed(
        val operation: PlayFailureOperation,
        val cause: PlayFailureCause
    ) : PlayAction
    data class ReloadAccepted(val content: SimulationContentState) : PlayAction
    data class ResetAccepted(val content: SimulationContentState) : PlayAction
}

sealed interface PlayOpenResult {
    data class Opened(val content: SimulationContentState) : PlayOpenResult

    data class Failure(val cause: PlayFailureCause) : PlayOpenResult
}

sealed interface PlaySaveResult {
    data class Saved(
        val target: StoredSubstrateId,
        val content: SimulationContentState
    ) : PlaySaveResult

    data class Failure(val cause: PlayFailureCause) : PlaySaveResult
}

sealed interface PlaySessionCommandResult {
    data class Accepted(val content: SimulationContentState) : PlaySessionCommandResult

    data class Failure(val cause: PlayFailureCause) : PlaySessionCommandResult
}

/** Platform session and file boundary used by the common Play store. */
interface PlaySessionOperations {
    suspend fun open(source: PlayLaunchSource): PlayOpenResult

    suspend fun substrateExists(name: String): StorageResult<Boolean>

    suspend fun save(target: StoredSubstrateId): PlaySaveResult

    suspend fun reload(source: StoredSubstrateId): PlaySessionCommandResult

    suspend fun reset(): PlaySessionCommandResult
}

internal sealed interface PlayCommand {
    data class Open(val source: PlayLaunchSource) : PlayCommand
    data class Save(val target: StoredSubstrateId) : PlayCommand
    data class Reload(val source: StoredSubstrateId) : PlayCommand
    data object Reset : PlayCommand
}

internal data class PlayTransition(
    val state: PlayState,
    val command: PlayCommand? = null,
    val effects: List<PlayEffect> = emptyList()
)

/** Pure Play navigation, dialog, operation, and dirty-state transitions. */
internal object PlayReducer {
    fun reduce(state: PlayState, action: PlayAction): PlayTransition = when (action) {
        PlayAction.Start -> if (state.phase == PlayPhase.NOT_STARTED) {
            PlayTransition(
                state = state.copy(phase = PlayPhase.OPENING),
                command = PlayCommand.Open(state.source)
            )
        } else {
            PlayTransition(state)
        }

        is PlayAction.SessionOpened -> if (state.phase == PlayPhase.OPENING) {
            PlayTransition(
                state.copy(
                    phase = PlayPhase.READY,
                    contentState = action.content
                )
            )
        } else {
            PlayTransition(state)
        }

        is PlayAction.SessionOpenFailed -> terminalFailure(
            state = state,
            operation = PlayFailureOperation.OPEN,
            cause = action.cause
        )

        is PlayAction.SelectTab -> readyTransition(state) {
            PlayTransition(it.copy(selectedTab = action.tab))
        }

        PlayAction.RequestPlateActions -> readyIdleTransition(state) {
            PlayTransition(it.copy(dialog = PlayDialog.PLATE_ACTIONS))
        }

        is PlayAction.SelectPlateAction -> selectPlateAction(state, action)

        PlayAction.DismissDialog -> if (state.isBusy) {
            PlayTransition(state)
        } else {
            PlayTransition(state.copy(dialog = PlayDialog.NONE))
        }

        PlayAction.BackRequested -> requestBack(state)

        PlayAction.ConfirmLeave -> if (
            state.dialog == PlayDialog.CONFIRM_LEAVE && !state.isBusy
        ) {
            PlayTransition(
                state = state.copy(dialog = PlayDialog.NONE),
                effects = listOf(PlayEffect.Finish(PlayFinishReason.USER))
            )
        } else {
            PlayTransition(state)
        }

        is PlayAction.SaveNameChanged -> if (
            state.dialog == PlayDialog.SAVE_PLATE && !state.isBusy
        ) {
            PlayTransition(state.copy(saveName = action.value))
        } else {
            PlayTransition(state)
        }

        PlayAction.ConfirmSave -> confirmSave(state)
        PlayAction.ConfirmReload -> confirmReload(state)
        PlayAction.ConfirmReset -> confirmReset(state)

        is PlayAction.ContentStateObserved -> observeContentState(state, action.value)

        is PlayAction.SessionFailed -> if (state.phase == PlayPhase.FAILED) {
            PlayTransition(state)
        } else {
            terminalFailure(
                state = state,
                operation = state.operation.failureOperationOrOpen(),
                cause = PlayFailureCause.Simulation(action.error)
            )
        }

        is PlayAction.SaveSucceeded -> if (state.operation == PlayOperation.SAVING) {
            PlayTransition(
                state = state.copy(
                    source = PlayLaunchSource.Stored(action.target),
                    dialog = PlayDialog.NONE,
                    saveName = action.target.value,
                    operation = PlayOperation.NONE,
                    contentState = action.content,
                    pendingContentChange = null
                ),
                effects = listOf(
                    PlayEffect.ShowNotice(
                        PlayNotice(
                            operation = PlayFailureOperation.SAVE,
                            feedback = PlayFeedback.SAVED
                        )
                    )
                )
            )
        } else {
            PlayTransition(state)
        }

        PlayAction.SaveTargetExists -> operationFailure(
            state = state,
            operation = PlayFailureOperation.SAVE,
            cause = PlayFailureCause.Storage(StorageError(StorageErrorCode.NAME_EXISTS))
        )

        is PlayAction.OperationFailed -> operationFailure(
            state,
            action.operation,
            action.cause
        )

        is PlayAction.ReloadAccepted -> if (state.operation == PlayOperation.RELOADING) {
            PlayTransition(
                state.copy(
                    dialog = PlayDialog.NONE,
                    operation = PlayOperation.NONE,
                    contentState = action.content,
                    pendingContentChange = PendingPlayContentChange(
                        PlayPendingContentCommand.RELOAD,
                        action.content.revision
                    )
                )
            )
        } else {
            PlayTransition(state)
        }

        is PlayAction.ResetAccepted -> if (state.operation == PlayOperation.RESETTING) {
            PlayTransition(
                state.copy(
                    dialog = PlayDialog.NONE,
                    operation = PlayOperation.NONE,
                    contentState = action.content,
                    pendingContentChange = PendingPlayContentChange(
                        PlayPendingContentCommand.RESET,
                        action.content.revision
                    )
                )
            )
        } else {
            PlayTransition(state)
        }
    }

    private fun selectPlateAction(
        state: PlayState,
        action: PlayAction.SelectPlateAction
    ): PlayTransition = readyIdleTransition(state) { ready ->
        if (ready.dialog != PlayDialog.PLATE_ACTIONS) return@readyIdleTransition PlayTransition(ready)
        when (action.action) {
            PlayPlateAction.SAVE -> PlayTransition(
                ready.copy(
                    dialog = PlayDialog.SAVE_PLATE,
                    saveName = ready.substrateName.orEmpty()
                )
            )
            PlayPlateAction.RELOAD -> {
                val target = ready.substrateId ?: return@readyIdleTransition PlayTransition(ready)
                if (action.confirmationRequired) {
                    PlayTransition(ready.copy(dialog = PlayDialog.CONFIRM_RELOAD))
                } else {
                    startReload(ready, target)
                }
            }
            PlayPlateAction.STERILIZE -> if (action.confirmationRequired) {
                PlayTransition(ready.copy(dialog = PlayDialog.CONFIRM_RESET))
            } else {
                startReset(ready)
            }
        }
    }

    private fun requestBack(state: PlayState): PlayTransition = readyIdleTransition(state) {
        if (it.isDirty) {
            PlayTransition(it.copy(dialog = PlayDialog.CONFIRM_LEAVE))
        } else {
            PlayTransition(
                state = it,
                effects = listOf(PlayEffect.Finish(PlayFinishReason.USER))
            )
        }
    }

    private fun confirmSave(state: PlayState): PlayTransition {
        if (state.phase != PlayPhase.READY || state.dialog != PlayDialog.SAVE_PLATE || state.isBusy) {
            return PlayTransition(state)
        }
        val normalizedName = state.saveName.trim()
        if (!CellularityFileNames.isValidDisplayName(normalizedName)) {
            return PlayTransition(
                state = state,
                effects = listOf(
                    PlayEffect.ShowNotice(
                        PlayNotice(
                            operation = PlayFailureOperation.SAVE,
                            feedback = PlayFeedback.BAD_NAME,
                            cause = PlayFailureCause.Storage(
                                StorageError(StorageErrorCode.BAD_NAME)
                            )
                        )
                    )
                )
            )
        }
        val target = StoredSubstrateId(normalizedName)
        return PlayTransition(
            state = state.copy(
                saveName = normalizedName,
                operation = PlayOperation.SAVING
            ),
            command = PlayCommand.Save(target)
        )
    }

    private fun confirmReload(state: PlayState): PlayTransition {
        if (state.dialog != PlayDialog.CONFIRM_RELOAD || state.isBusy) {
            return PlayTransition(state)
        }
        val target = state.substrateId ?: return PlayTransition(
            state.copy(dialog = PlayDialog.NONE)
        )
        return startReload(state, target)
    }

    private fun confirmReset(state: PlayState): PlayTransition {
        if (state.dialog != PlayDialog.CONFIRM_RESET || state.isBusy) {
            return PlayTransition(state)
        }
        return startReset(state)
    }

    private fun startReload(
        state: PlayState,
        target: StoredSubstrateId
    ): PlayTransition = PlayTransition(
        state = state.copy(operation = PlayOperation.RELOADING),
        command = PlayCommand.Reload(target)
    )

    private fun startReset(state: PlayState): PlayTransition = PlayTransition(
        state = state.copy(operation = PlayOperation.RESETTING),
        command = PlayCommand.Reset
    )

    private fun observeContentState(
        state: PlayState,
        observed: SimulationContentState
    ): PlayTransition {
        if (state.phase != PlayPhase.READY || observed.revision < state.contentState.revision) {
            return PlayTransition(state)
        }
        val pending = state.pendingContentChange
        val pendingCompleted = when (pending?.command) {
            PlayPendingContentCommand.RESET -> observed.revision > pending.issuedAtRevision
            PlayPendingContentCommand.RELOAD ->
                observed.persistedRevision?.let { it > pending.issuedAtRevision } == true
            null -> false
        }
        return PlayTransition(
            state.copy(
                contentState = observed,
                pendingContentChange = if (pendingCompleted) null else pending
            )
        )
    }

    private fun operationFailure(
        state: PlayState,
        operation: PlayFailureOperation,
        cause: PlayFailureCause
    ): PlayTransition {
        if (cause.isTerminalSimulationFailure()) {
            return terminalFailure(state, operation, cause)
        }
        return PlayTransition(
            state = state.copy(operation = PlayOperation.NONE),
            effects = listOf(
                PlayEffect.ShowNotice(
                    PlayNotice(operation, feedbackFor(operation, cause), cause)
                )
            )
        )
    }

    private fun terminalFailure(
        state: PlayState,
        operation: PlayFailureOperation,
        cause: PlayFailureCause
    ): PlayTransition {
        val feedback = feedbackFor(operation, cause)
        val finishReason = if (feedback == PlayFeedback.NEWER_VERSION) {
            PlayFinishReason.NEWER_VERSION
        } else {
            PlayFinishReason.OPEN_FAILED
        }
        return PlayTransition(
            state = state.copy(
                phase = PlayPhase.FAILED,
                dialog = PlayDialog.NONE,
                operation = PlayOperation.NONE
            ),
            effects = listOf(
                PlayEffect.ShowNotice(PlayNotice(operation, feedback, cause)),
                PlayEffect.Finish(finishReason)
            )
        )
    }

    private fun feedbackFor(
        operation: PlayFailureOperation,
        cause: PlayFailureCause
    ): PlayFeedback = when (cause) {
        is PlayFailureCause.Storage -> when (cause.error.code) {
            StorageErrorCode.BAD_NAME -> PlayFeedback.BAD_NAME
            StorageErrorCode.NAME_EXISTS -> PlayFeedback.NAME_EXISTS
            StorageErrorCode.TOO_LARGE -> PlayFeedback.FILE_TOO_LARGE
            StorageErrorCode.NEWER_VERSION -> PlayFeedback.NEWER_VERSION
            StorageErrorCode.NOT_FOUND,
            StorageErrorCode.UNSUPPORTED_FORMAT,
            StorageErrorCode.CORRUPTED,
            StorageErrorCode.PERMISSION_REVOKED,
            StorageErrorCode.IO_FAILURE -> operation.genericFailureFeedback()
        }
        is PlayFailureCause.Simulation -> when (cause.error.code) {
            CellularityErrorCode.INVALID_VALUE -> if (operation == PlayFailureOperation.SAVE) {
                PlayFeedback.BAD_NAME
            } else {
                operation.genericFailureFeedback()
            }
            CellularityErrorCode.OUT_OF_MEMORY -> PlayFeedback.FILE_TOO_LARGE
            else -> operation.genericFailureFeedback()
        }
    }

    private fun PlayFailureOperation.genericFailureFeedback(): PlayFeedback = when (this) {
        PlayFailureOperation.OPEN -> PlayFeedback.LOAD_FAILED
        PlayFailureOperation.SAVE -> PlayFeedback.SAVE_FAILED
        PlayFailureOperation.RELOAD -> PlayFeedback.RELOAD_FAILED
        PlayFailureOperation.RESET -> PlayFeedback.RESET_FAILED
    }

    private fun PlayFailureCause.isTerminalSimulationFailure(): Boolean =
        this is PlayFailureCause.Simulation && when (error.code) {
            CellularityErrorCode.INTERNAL_FAILURE,
            CellularityErrorCode.OUT_OF_MEMORY,
            CellularityErrorCode.SESSION_CLOSED -> true
            else -> false
        }

    private fun PlayOperation.failureOperationOrOpen(): PlayFailureOperation = when (this) {
        PlayOperation.NONE -> PlayFailureOperation.OPEN
        PlayOperation.SAVING -> PlayFailureOperation.SAVE
        PlayOperation.RELOADING -> PlayFailureOperation.RELOAD
        PlayOperation.RESETTING -> PlayFailureOperation.RESET
    }

    private inline fun readyTransition(
        state: PlayState,
        transition: (PlayState) -> PlayTransition
    ): PlayTransition = if (state.phase == PlayPhase.READY) {
        transition(state)
    } else {
        PlayTransition(state)
    }

    private inline fun readyIdleTransition(
        state: PlayState,
        transition: (PlayState) -> PlayTransition
    ): PlayTransition = if (
        state.phase == PlayPhase.READY && !state.isBusy && state.pendingContentChange == null
    ) {
        transition(state)
    } else {
        PlayTransition(state)
    }
}

class PlayStore(
    source: PlayLaunchSource,
    private val operations: PlaySessionOperations,
    restoredUi: PlayRestoredUiState = PlayRestoredUiState()
) : Store<PlayState, PlayAction, PlayEffect> {
    private val mutableState = MutableStateFlow(PlayState.initial(source, restoredUi))
    private val effectChannel = Channel<PlayEffect>(Channel.UNLIMITED)
    private val dispatchMutex = Mutex()

    override val state: StateFlow<PlayState> = mutableState.asStateFlow()
    override val effects: Flow<PlayEffect> = effectChannel.receiveAsFlow()

    override suspend fun dispatch(action: PlayAction) {
        dispatchMutex.withLock {
            executeTransition(PlayReducer.reduce(mutableState.value, action))
        }
    }

    private suspend fun executeTransition(initial: PlayTransition) {
        var transition = initial
        while (true) {
            mutableState.value = transition.state
            transition.effects.forEach { effectChannel.send(it) }
            val command = transition.command ?: return
            val completion = try {
                execute(command)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (failure: Exception) {
                PlayAction.OperationFailed(
                    operation = command.failureOperation(),
                    cause = PlayFailureCause.Simulation(
                        CellularityError(
                            CellularityErrorCode.INTERNAL_FAILURE,
                            failure.message.orEmpty()
                        )
                    )
                )
            }
            transition = PlayReducer.reduce(mutableState.value, completion)
        }
    }

    private suspend fun execute(command: PlayCommand): PlayAction = when (command) {
        is PlayCommand.Open -> when (val result = operations.open(command.source)) {
            is PlayOpenResult.Opened -> PlayAction.SessionOpened(result.content)
            is PlayOpenResult.Failure -> PlayAction.SessionOpenFailed(result.cause)
        }
        is PlayCommand.Save -> executeSave(command.target)
        is PlayCommand.Reload -> when (val result = operations.reload(command.source)) {
            is PlaySessionCommandResult.Accepted -> PlayAction.ReloadAccepted(result.content)
            is PlaySessionCommandResult.Failure -> PlayAction.OperationFailed(
                PlayFailureOperation.RELOAD,
                result.cause
            )
        }
        PlayCommand.Reset -> when (val result = operations.reset()) {
            is PlaySessionCommandResult.Accepted -> PlayAction.ResetAccepted(result.content)
            is PlaySessionCommandResult.Failure -> PlayAction.OperationFailed(
                PlayFailureOperation.RESET,
                result.cause
            )
        }
    }

    private suspend fun executeSave(target: StoredSubstrateId): PlayAction {
        when (val exists = operations.substrateExists(target.value)) {
            is StorageResult.Failure -> return PlayAction.OperationFailed(
                PlayFailureOperation.SAVE,
                PlayFailureCause.Storage(exists.error)
            )
            is StorageResult.Success -> if (
                exists.value && mutableState.value.substrateId != target
            ) {
                return PlayAction.SaveTargetExists
            }
        }
        return when (val result = operations.save(target)) {
            is PlaySaveResult.Saved -> PlayAction.SaveSucceeded(
                result.target,
                result.content
            )
            is PlaySaveResult.Failure -> PlayAction.OperationFailed(
                PlayFailureOperation.SAVE,
                result.cause
            )
        }
    }

    private fun PlayCommand.failureOperation(): PlayFailureOperation = when (this) {
        is PlayCommand.Open -> PlayFailureOperation.OPEN
        is PlayCommand.Save -> PlayFailureOperation.SAVE
        is PlayCommand.Reload -> PlayFailureOperation.RELOAD
        PlayCommand.Reset -> PlayFailureOperation.RESET
    }
}
