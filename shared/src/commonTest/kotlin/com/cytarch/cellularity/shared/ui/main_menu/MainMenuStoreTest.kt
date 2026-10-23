package com.cytarch.cellularity.shared.ui.main_menu

import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class MainMenuStoreTest {
    @Test
    fun pageSelectionIsAPlatformNeutralStateTransition() {
        val transition = MainMenuReducer.reduce(
            MainMenuState(),
            MainMenuAction.PageSelected(MainMenuPage.SETTINGS)
        )

        assertEquals(MainMenuPage.SETTINGS, transition.state.page)
        assertEquals(MainMenuPage.GENE_BANK, MainMenuPage.fromIndex(1))
        assertEquals(MainMenuPage.EXPERIMENTS, MainMenuPage.fromIndex(Int.MAX_VALUE))
        assertTrue(transition.effects.isEmpty())
    }

    @Test
    fun importProgressAcceptsOnlyItsMatchingCompletionOrCancellation() {
        val running = MainMenuReducer.reduce(
            MainMenuState(),
            MainMenuAction.ImportStarted("active")
        ).state
        val staleCompletion = MainMenuReducer.reduce(
            running,
            MainMenuAction.ImportFinished(
                commandId = "stale",
                outcome = MainMenuImportOutcome.Success(genome("ignored"))
            )
        )
        val staleCancellation = MainMenuReducer.reduce(
            running,
            MainMenuAction.ImportCancelled("stale")
        )
        val cancelled = MainMenuReducer.reduce(
            running,
            MainMenuAction.ImportCancelled("active")
        )

        assertTrue(staleCompletion.state.importInProgress)
        assertTrue(staleCompletion.effects.isEmpty())
        assertTrue(staleCancellation.state.importInProgress)
        assertFalse(cancelled.state.importInProgress)
    }

    @Test
    fun successfulSubstrateImportEmitsMessageThenTypedOpenEffectOnce() = runBlocking {
        val store = MainMenuStore(MainMenuState(page = MainMenuPage.ABOUT))
        store.dispatch(MainMenuAction.ImportStarted("substrate"))

        store.dispatch(
            MainMenuAction.ImportFinished(
                commandId = "substrate",
                outcome = MainMenuImportOutcome.Success(substrate("sample"))
            )
        )

        assertFalse(store.state is MutableStateFlow<*>)
        assertEquals(MainMenuPage.EXPERIMENTS, store.state.value.page)
        assertFalse(store.state.value.importInProgress)
        assertEquals(1L, store.state.value.collectionRevisions.substrates)
        assertEquals(0L, store.state.value.collectionRevisions.genomes)
        assertEquals(MainMenuEffect.ImportSucceeded, store.effects.first())
        assertEquals(
            "sample",
            assertIs<MainMenuEffect.OpenSubstrate>(store.effects.first()).substrateId.value
        )
        assertNull(withTimeoutOrNull(25) { store.effects.first() })
    }

    @Test
    fun successfulGenomeImportSelectsGeneBankWithoutAPlatformLaunch() = runBlocking {
        val store = MainMenuStore()
        store.dispatch(MainMenuAction.ImportStarted("genome"))

        store.dispatch(
            MainMenuAction.ImportFinished(
                commandId = "genome",
                outcome = MainMenuImportOutcome.Success(genome("organism"))
            )
        )

        assertEquals(MainMenuPage.GENE_BANK, store.state.value.page)
        assertEquals(0L, store.state.value.collectionRevisions.substrates)
        assertEquals(1L, store.state.value.collectionRevisions.genomes)
        assertEquals(MainMenuEffect.ImportSucceeded, store.effects.first())
        assertNull(withTimeoutOrNull(25) { store.effects.first() })
    }

    @Test
    fun duplicateNameNavigatesToTheMatchingCollection() {
        val initial = MainMenuState(page = MainMenuPage.ABOUT)
        val genomeFailure = finishFailure(
            initial = initial,
            commandId = "genome-duplicate",
            errorCode = StorageErrorCode.NAME_EXISTS,
            file = genome("duplicate")
        )
        val substrateFailure = finishFailure(
            initial = initial,
            commandId = "substrate-duplicate",
            errorCode = StorageErrorCode.NAME_EXISTS,
            file = substrate("duplicate")
        )

        assertEquals(MainMenuPage.GENE_BANK, genomeFailure.state.page)
        assertEquals(MainMenuPage.EXPERIMENTS, substrateFailure.state.page)
        assertEquals(1L, genomeFailure.state.collectionRevisions.genomes)
        assertEquals(0L, genomeFailure.state.collectionRevisions.substrates)
        assertEquals(0L, substrateFailure.state.collectionRevisions.genomes)
        assertEquals(1L, substrateFailure.state.collectionRevisions.substrates)
        assertEquals(
            MainMenuImportedFileKind.GENOME,
            assertIs<MainMenuEffect.ImportFailed>(genomeFailure.effects.single()).fileKind
        )
        assertEquals(
            MainMenuImportedFileKind.SUBSTRATE,
            assertIs<MainMenuEffect.ImportFailed>(substrateFailure.effects.single()).fileKind
        )
    }

    @Test
    fun ordinaryImportFailureClearsProgressWithoutChangingThePage() {
        val transition = finishFailure(
            initial = MainMenuState(page = MainMenuPage.SETTINGS),
            commandId = "broken",
            errorCode = StorageErrorCode.CORRUPTED,
            file = genome("broken")
        )

        assertEquals(MainMenuPage.SETTINGS, transition.state.page)
        assertFalse(transition.state.importInProgress)
        assertEquals(MainMenuCollectionRevisions(), transition.state.collectionRevisions)
        assertEquals(
            StorageErrorCode.CORRUPTED,
            assertIs<MainMenuEffect.ImportFailed>(transition.effects.single()).error.code
        )
    }

    private fun finishFailure(
        initial: MainMenuState,
        commandId: String,
        errorCode: StorageErrorCode,
        file: MainMenuImportedFile?
    ): MainMenuTransition {
        val running = MainMenuReducer.reduce(
            initial,
            MainMenuAction.ImportStarted(commandId)
        ).state
        return MainMenuReducer.reduce(
            running,
            MainMenuAction.ImportFinished(
                commandId = commandId,
                outcome = MainMenuImportOutcome.Failure(
                    error = StorageError(errorCode, "test"),
                    file = file
                )
            )
        )
    }

    private fun substrate(name: String) = MainMenuImportedFile(
        displayName = name,
        kind = MainMenuImportedFileKind.SUBSTRATE
    )

    private fun genome(name: String) = MainMenuImportedFile(
        displayName = name,
        kind = MainMenuImportedFileKind.GENOME
    )
}
