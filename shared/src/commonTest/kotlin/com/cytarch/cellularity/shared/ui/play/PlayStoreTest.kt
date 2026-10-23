package com.cytarch.cellularity.shared.ui.play

import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class PlayStoreTest {
    @Test
    fun startOpensStoredSourceOnceAndPublishesReadyCleanState() = runBlocking {
        val source = PlayLaunchSource.Stored(StoredSubstrateId("plate"))
        val operations = FakePlaySessionOperations()
        val store = PlayStore(source, operations)

        store.dispatch(PlayAction.Start)
        store.dispatch(PlayAction.Start)

        assertEquals(listOf<PlayLaunchSource>(source), operations.openedSources)
        assertEquals(PlayPhase.READY, store.state.value.phase)
        assertEquals("plate", store.state.value.substrateName)
        assertFalse(store.state.value.isDirty)
    }

    @Test
    fun genomeTabKeepsMicroscopeVisibleUntilPreviewContentIsCurrent() {
        assertEquals(
            PlayContentVisibility(
                microscope = true,
                substrate = false,
                genome = false
            ),
            playContentVisibility(PlayTab.GENOME, genomeContentReady = false)
        )
        assertEquals(
            PlayContentVisibility(
                microscope = false,
                substrate = false,
                genome = true
            ),
            playContentVisibility(PlayTab.GENOME, genomeContentReady = true)
        )
    }

    @Test
    fun tabAndDialogStateHaveSerializablePersistenceSnapshot() = runBlocking {
        val source = PlayLaunchSource.New(SimulationContract.defaultConfiguration)
        val store = PlayStore(source, FakePlaySessionOperations())
        store.dispatch(PlayAction.Start)

        store.dispatch(PlayAction.SelectTab(PlayTab.GENOME))
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(
                PlayPlateAction.SAVE,
                confirmationRequired = false
            )
        )
        store.dispatch(PlayAction.SaveNameChanged("draft"))

        assertEquals(
            PlayPersistenceState(
                source,
                PlayRestoredUiState(
                    selectedTab = PlayTab.GENOME,
                    dialog = PlayDialog.SAVE_PLATE,
                    saveName = "draft"
                )
            ),
            store.state.value.persistenceState()
        )
    }

    @Test
    fun cleanBackFinishesImmediatelyButDirtyBackRequiresConfirmation() = runBlocking {
        val cleanStore = PlayStore(
            PlayLaunchSource.Stored(StoredSubstrateId("clean")),
            FakePlaySessionOperations()
        )
        cleanStore.dispatch(PlayAction.Start)
        cleanStore.dispatch(PlayAction.BackRequested)
        assertEquals(
            PlayFinishReason.USER,
            assertIs<PlayEffect.Finish>(cleanStore.effects.first()).reason
        )

        val dirtyStore = PlayStore(
            PlayLaunchSource.Stored(StoredSubstrateId("dirty")),
            FakePlaySessionOperations()
        )
        dirtyStore.dispatch(PlayAction.Start)
        dirtyStore.dispatch(
            PlayAction.ContentStateObserved(SimulationContentState(1, 0))
        )
        dirtyStore.dispatch(PlayAction.BackRequested)

        assertEquals(PlayDialog.CONFIRM_LEAVE, dirtyStore.state.value.dialog)
        assertNull(withTimeoutOrNull(50) { dirtyStore.effects.first() })
        dirtyStore.dispatch(PlayAction.ConfirmLeave)
        assertIs<PlayEffect.Finish>(dirtyStore.effects.first())
        Unit
    }

    @Test
    fun aNewUnsavedWorldIsDirtyBeforeItsFirstMutation() = runBlocking {
        val store = PlayStore(
            PlayLaunchSource.New(SimulationContract.defaultConfiguration),
            FakePlaySessionOperations()
        )
        store.dispatch(PlayAction.Start)

        assertTrue(store.state.value.isDirty)
        store.dispatch(PlayAction.BackRequested)
        assertEquals(PlayDialog.CONFIRM_LEAVE, store.state.value.dialog)
    }

    @Test
    fun invalidSaveNameIsRejectedBeforeAnyStorageCall() = runBlocking {
        val operations = FakePlaySessionOperations()
        val store = readySaveDialog(operations)

        store.dispatch(PlayAction.SaveNameChanged("../bad"))
        store.dispatch(PlayAction.ConfirmSave)

        assertTrue(operations.existenceChecks.isEmpty())
        val notice = assertIs<PlayEffect.ShowNotice>(store.effects.first()).notice
        assertEquals(PlayFeedback.BAD_NAME, notice.feedback)
        assertEquals(StorageErrorCode.BAD_NAME, notice.storageErrorCode())
        assertEquals(PlayDialog.SAVE_PLATE, store.state.value.dialog)
    }

    @Test
    fun duplicateTargetAndSelfOverwriteHaveDifferentSemantics() = runBlocking {
        val duplicateOperations = FakePlaySessionOperations().apply {
            existsResult = StorageResult.Success(true)
        }
        val duplicateStore = readySaveDialog(
            duplicateOperations,
            source = PlayLaunchSource.Stored(StoredSubstrateId("old"))
        )
        duplicateStore.dispatch(PlayAction.SaveNameChanged("other"))
        duplicateStore.dispatch(PlayAction.ConfirmSave)

        assertTrue(duplicateOperations.savedTargets.isEmpty())
        assertEquals(
            PlayFeedback.NAME_EXISTS,
            assertIs<PlayEffect.ShowNotice>(duplicateStore.effects.first()).notice.feedback
        )

        val overwriteOperations = FakePlaySessionOperations().apply {
            existsResult = StorageResult.Success(true)
        }
        val overwriteStore = readySaveDialog(
            overwriteOperations,
            source = PlayLaunchSource.Stored(StoredSubstrateId("same"))
        )
        overwriteStore.dispatch(PlayAction.ConfirmSave)

        assertEquals(listOf(StoredSubstrateId("same")), overwriteOperations.savedTargets)
        assertEquals(PlayFeedback.SAVED, assertIs<PlayEffect.ShowNotice>(
            overwriteStore.effects.first()
        ).notice.feedback)
    }

    @Test
    fun firstSuccessfulSaveChangesRecoverySourceAndClearsDirtyAtSavedRevision() = runBlocking {
        val operations = FakePlaySessionOperations().apply {
            saveResult = PlaySaveResult.Saved(
                StoredSubstrateId("saved"),
                SimulationContentState(4, 4)
            )
        }
        val store = readySaveDialog(operations)
        store.dispatch(PlayAction.SaveNameChanged("  saved  "))

        store.dispatch(PlayAction.ConfirmSave)

        assertEquals(listOf(StoredSubstrateId("saved")), operations.savedTargets)
        assertEquals(
            PlayLaunchSource.Stored(StoredSubstrateId("saved")),
            store.state.value.source
        )
        assertFalse(store.state.value.isDirty)
        assertEquals(PlayDialog.NONE, store.state.value.dialog)
    }

    @Test
    fun reloadStaysConservativelyDirtyUntilPersistedReplacementIsObserved() = runBlocking {
        val operations = FakePlaySessionOperations().apply {
            reloadResult = PlaySessionCommandResult.Accepted(
                SimulationContentState(1, 0)
            )
        }
        val store = PlayStore(
            PlayLaunchSource.Stored(StoredSubstrateId("plate")),
            operations
        )
        store.dispatch(PlayAction.Start)
        store.dispatch(PlayAction.ContentStateObserved(SimulationContentState(1, 0)))
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(
                PlayPlateAction.RELOAD,
                confirmationRequired = true
            )
        )
        store.dispatch(PlayAction.ConfirmReload)
        store.dispatch(PlayAction.ConfirmReload)

        assertEquals(listOf(StoredSubstrateId("plate")), operations.reloadedSources)
        assertTrue(store.state.value.isDirty)
        assertEquals(PlayPendingContentCommand.RELOAD, store.state.value.pendingContentChange?.command)

        store.dispatch(PlayAction.ContentStateObserved(SimulationContentState(2, 2)))
        assertFalse(store.state.value.isDirty)
        assertNull(store.state.value.pendingContentChange)
    }

    @Test
    fun reloadCompletionSurvivesConflationWithTheNextWorldMutation() = runBlocking {
        val operations = FakePlaySessionOperations().apply {
            reloadResult = PlaySessionCommandResult.Accepted(
                SimulationContentState(1, 0)
            )
        }
        val store = PlayStore(
            PlayLaunchSource.Stored(StoredSubstrateId("plate")),
            operations
        )
        store.dispatch(PlayAction.Start)
        store.dispatch(PlayAction.ContentStateObserved(SimulationContentState(1, 0)))
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(
                PlayPlateAction.RELOAD,
                confirmationRequired = false
            )
        )

        store.dispatch(PlayAction.ContentStateObserved(SimulationContentState(3, 2)))

        assertNull(store.state.value.pendingContentChange)
        assertTrue(store.state.value.isDirty)
    }

    @Test
    fun acceptedResetIsDirtyImmediatelyAndDuplicateCommandIsSuppressed() = runBlocking {
        val operations = FakePlaySessionOperations()
        val store = PlayStore(
            PlayLaunchSource.Stored(StoredSubstrateId("plate")),
            operations
        )
        store.dispatch(PlayAction.Start)
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(
                PlayPlateAction.STERILIZE,
                confirmationRequired = false
            )
        )
        store.dispatch(PlayAction.ConfirmReset)
        store.dispatch(PlayAction.ConfirmReset)

        assertEquals(1, operations.resetCount)
        assertTrue(store.state.value.isDirty)
        assertEquals(PlayPendingContentCommand.RESET, store.state.value.pendingContentChange?.command)

        store.dispatch(PlayAction.ContentStateObserved(SimulationContentState(1, 0)))
        assertNull(store.state.value.pendingContentChange)
    }

    @Test
    fun saveRemainsSingleAcrossConcurrentConfirmationAndCallerCancellation() = runBlocking {
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val operations = FakePlaySessionOperations().apply {
            beforeSave = {
                entered.complete(Unit)
                release.await()
            }
        }
        val store = readySaveDialog(operations)
        store.dispatch(PlayAction.SaveNameChanged("once"))

        val first = launch { store.dispatch(PlayAction.ConfirmSave) }
        entered.await()
        val duplicate = async { store.dispatch(PlayAction.ConfirmSave) }
        release.complete(Unit)
        first.join()
        duplicate.await()

        assertEquals(listOf(StoredSubstrateId("once")), operations.savedTargets)
    }

    @Test
    fun backQueuedDuringSaveRunsOnceAgainstTheSavedState() = runBlocking {
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val operations = FakePlaySessionOperations().apply {
            beforeSave = {
                entered.complete(Unit)
                release.await()
            }
        }
        val store = readySaveDialog(
            operations,
            source = PlayLaunchSource.Stored(StoredSubstrateId("plate"))
        )

        val saving = launch { store.dispatch(PlayAction.ConfirmSave) }
        entered.await()
        val backing = launch { store.dispatch(PlayAction.BackRequested) }
        release.complete(Unit)
        saving.join()
        backing.join()

        assertEquals(listOf(StoredSubstrateId("plate")), operations.savedTargets)
        assertEquals(
            PlayFeedback.SAVED,
            assertIs<PlayEffect.ShowNotice>(store.effects.first()).notice.feedback
        )
        assertEquals(
            PlayFinishReason.USER,
            assertIs<PlayEffect.Finish>(store.effects.first()).reason
        )
    }

    @Test
    fun newerOpenFailurePreservesCauseAndEmitsNoticeBeforeFinishOnce() = runBlocking {
        val error = StorageError(StorageErrorCode.NEWER_VERSION, "future")
        val operations = FakePlaySessionOperations().apply {
            openResult = PlayOpenResult.Failure(PlayFailureCause.Storage(error))
        }
        val store = PlayStore(
            PlayLaunchSource.Stored(StoredSubstrateId("future")),
            operations
        )

        store.dispatch(PlayAction.Start)

        assertEquals(PlayPhase.FAILED, store.state.value.phase)
        val notice = assertIs<PlayEffect.ShowNotice>(store.effects.first()).notice
        assertEquals(PlayFeedback.NEWER_VERSION, notice.feedback)
        assertEquals(error, assertIs<PlayFailureCause.Storage>(notice.cause).error)
        assertEquals(
            PlayFinishReason.NEWER_VERSION,
            assertIs<PlayEffect.Finish>(store.effects.first()).reason
        )
        assertNull(withTimeoutOrNull(50) { store.effects.first() })
    }

    @Test
    fun terminalSessionFailureCannotProduceDuplicateFinishEffects() = runBlocking {
        val store = PlayStore(
            PlayLaunchSource.New(SimulationContract.defaultConfiguration),
            FakePlaySessionOperations()
        )
        store.dispatch(PlayAction.Start)
        val failure = CellularityError(CellularityErrorCode.INTERNAL_FAILURE, "fatal")

        store.dispatch(PlayAction.SessionFailed(failure))
        store.dispatch(PlayAction.SessionFailed(failure))

        assertIs<PlayEffect.ShowNotice>(store.effects.first())
        assertIs<PlayEffect.Finish>(store.effects.first())
        assertNull(withTimeoutOrNull(50) { store.effects.first() })
    }

    private suspend fun readySaveDialog(
        operations: FakePlaySessionOperations,
        source: PlayLaunchSource = PlayLaunchSource.New(
            SimulationContract.defaultConfiguration
        )
    ): PlayStore {
        val store = PlayStore(source, operations)
        store.dispatch(PlayAction.Start)
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(
                PlayPlateAction.SAVE,
                confirmationRequired = false
            )
        )
        return store
    }

    private fun PlayNotice.storageErrorCode(): StorageErrorCode? =
        (cause as? PlayFailureCause.Storage)?.error?.code
}

private class FakePlaySessionOperations : PlaySessionOperations {
    val openedSources = mutableListOf<PlayLaunchSource>()
    val existenceChecks = mutableListOf<String>()
    val savedTargets = mutableListOf<StoredSubstrateId>()
    val reloadedSources = mutableListOf<StoredSubstrateId>()
    var resetCount = 0

    var openResult: PlayOpenResult? = null
    var existsResult: StorageResult<Boolean> = StorageResult.Success(false)
    var saveResult: PlaySaveResult? = null
    var reloadResult: PlaySessionCommandResult = PlaySessionCommandResult.Accepted(
        SimulationContentState.storedWorld()
    )
    var resetResult: PlaySessionCommandResult = PlaySessionCommandResult.Accepted(
        SimulationContentState.storedWorld()
    )
    var beforeSave: suspend () -> Unit = {}

    override suspend fun open(source: PlayLaunchSource): PlayOpenResult {
        openedSources += source
        return openResult ?: PlayOpenResult.Opened(
            when (source) {
                is PlayLaunchSource.New -> SimulationContentState.newWorld()
                is PlayLaunchSource.Stored -> SimulationContentState.storedWorld()
            }
        )
    }

    override suspend fun substrateExists(name: String): StorageResult<Boolean> {
        existenceChecks += name
        return existsResult
    }

    override suspend fun save(target: StoredSubstrateId): PlaySaveResult {
        savedTargets += target
        beforeSave()
        return saveResult ?: PlaySaveResult.Saved(
            target,
            SimulationContentState.storedWorld()
        )
    }

    override suspend fun reload(source: StoredSubstrateId): PlaySessionCommandResult {
        reloadedSources += source
        return reloadResult
    }

    override suspend fun reset(): PlaySessionCommandResult {
        resetCount += 1
        return resetResult
    }
}
