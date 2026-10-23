package com.cytarch.cellularity.shared.ui.main_menu

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.ui.state.Store
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

enum class MainMenuPage(val index: Int) {
    EXPERIMENTS(0),
    GENE_BANK(1),
    SETTINGS(2),
    ABOUT(3);

    companion object {
        fun fromIndex(index: Int): MainMenuPage =
            entries.firstOrNull { it.index == index } ?: EXPERIMENTS
    }
}

sealed interface MainMenuImportProgress {
    data object Idle : MainMenuImportProgress

    data class Running(val commandId: String) : MainMenuImportProgress {
        init {
            require(commandId.isNotBlank()) { "Import command ID must not be blank" }
        }
    }
}

data class MainMenuCollectionRevisions(
    val substrates: Long = 0L,
    val genomes: Long = 0L
) {
    fun forPage(page: MainMenuPage): Long = when (page) {
        MainMenuPage.EXPERIMENTS -> substrates
        MainMenuPage.GENE_BANK -> genomes
        MainMenuPage.SETTINGS,
        MainMenuPage.ABOUT -> 0L
    }

    internal fun advancedFor(kind: MainMenuImportedFileKind): MainMenuCollectionRevisions =
        when (kind) {
            MainMenuImportedFileKind.SUBSTRATE -> copy(substrates = substrates + 1L)
            MainMenuImportedFileKind.GENOME -> copy(genomes = genomes + 1L)
        }
}

data class MainMenuState(
    val page: MainMenuPage = MainMenuPage.EXPERIMENTS,
    val importProgress: MainMenuImportProgress = MainMenuImportProgress.Idle,
    val collectionRevisions: MainMenuCollectionRevisions = MainMenuCollectionRevisions()
) {
    val importInProgress: Boolean
        get() = importProgress is MainMenuImportProgress.Running

    fun collectionRevision(page: MainMenuPage): Long = collectionRevisions.forPage(page)
}

enum class MainMenuImportedFileKind {
    SUBSTRATE,
    GENOME
}

data class MainMenuImportedFile(
    val displayName: String,
    val kind: MainMenuImportedFileKind
) {
    init {
        require(CellularityFileNames.isValidDisplayName(displayName)) {
            "Imported file must have a valid display name"
        }
    }
}

sealed interface MainMenuImportOutcome {
    data class Success(val file: MainMenuImportedFile) : MainMenuImportOutcome

    data class Failure(
        val error: StorageError,
        val file: MainMenuImportedFile? = null
    ) : MainMenuImportOutcome
}

sealed interface MainMenuAction {
    data class PageSelected(val page: MainMenuPage) : MainMenuAction

    data class ImportStarted(val commandId: String) : MainMenuAction

    data class ImportFinished(
        val commandId: String,
        val outcome: MainMenuImportOutcome
    ) : MainMenuAction

    data class ImportCancelled(val commandId: String) : MainMenuAction
}

sealed interface MainMenuEffect {
    data object ImportSucceeded : MainMenuEffect
    data class ImportFailed(
        val error: StorageError,
        val fileKind: MainMenuImportedFileKind?
    ) : MainMenuEffect
    data class OpenSubstrate(val substrateId: StoredSubstrateId) : MainMenuEffect
}

internal data class MainMenuTransition(
    val state: MainMenuState,
    val effects: List<MainMenuEffect> = emptyList()
)

/** Pure product and navigation rules. Android import transport lives outside this reducer. */
internal object MainMenuReducer {
    fun reduce(state: MainMenuState, action: MainMenuAction): MainMenuTransition {
        return when (action) {
            is MainMenuAction.PageSelected -> MainMenuTransition(
                state = state.copy(page = action.page)
            )
            is MainMenuAction.ImportStarted -> {
                if (state.importInProgress) {
                    MainMenuTransition(state)
                } else {
                    MainMenuTransition(
                        state.copy(
                            importProgress = MainMenuImportProgress.Running(action.commandId)
                        )
                    )
                }
            }
            is MainMenuAction.ImportFinished -> finishImport(state, action)
            is MainMenuAction.ImportCancelled -> {
                if (state.runningCommandId() == action.commandId) {
                    MainMenuTransition(
                        state.copy(importProgress = MainMenuImportProgress.Idle)
                    )
                } else {
                    MainMenuTransition(state)
                }
            }
        }
    }

    private fun finishImport(
        state: MainMenuState,
        action: MainMenuAction.ImportFinished
    ): MainMenuTransition {
        if (state.runningCommandId() != action.commandId) {
            return MainMenuTransition(state)
        }

        val idleState = state.copy(importProgress = MainMenuImportProgress.Idle)
        return when (val outcome = action.outcome) {
            is MainMenuImportOutcome.Success -> {
                val destination = outcome.file.destinationPage()
                val effects = buildList {
                    add(MainMenuEffect.ImportSucceeded)
                    if (outcome.file.kind == MainMenuImportedFileKind.SUBSTRATE) {
                        add(
                            MainMenuEffect.OpenSubstrate(
                                StoredSubstrateId(outcome.file.displayName)
                            )
                        )
                    }
                }
                MainMenuTransition(
                    state = idleState
                        .advanceCollectionRevision(outcome.file.kind)
                        .copy(page = destination),
                    effects = effects
                )
            }
            is MainMenuImportOutcome.Failure -> MainMenuTransition(
                state = if (
                    outcome.error.code == StorageErrorCode.NAME_EXISTS && outcome.file != null
                ) {
                    idleState
                        .advanceCollectionRevision(outcome.file.kind)
                        .copy(page = outcome.file.destinationPage())
                } else {
                    idleState
                },
                effects = listOf(
                    MainMenuEffect.ImportFailed(
                        error = outcome.error,
                        fileKind = outcome.file?.kind
                    )
                )
            )
        }
    }

    private fun MainMenuState.runningCommandId(): String? =
        (importProgress as? MainMenuImportProgress.Running)?.commandId

    private fun MainMenuState.advanceCollectionRevision(
        kind: MainMenuImportedFileKind
    ): MainMenuState = copy(
        collectionRevisions = collectionRevisions.advancedFor(kind)
    )

    private fun MainMenuImportedFile.destinationPage(): MainMenuPage = when (kind) {
        MainMenuImportedFileKind.SUBSTRATE -> MainMenuPage.EXPERIMENTS
        MainMenuImportedFileKind.GENOME -> MainMenuPage.GENE_BANK
    }
}

class MainMenuStore(
    initialState: MainMenuState = MainMenuState()
) : Store<MainMenuState, MainMenuAction, MainMenuEffect> {
    private val mutableState = MutableStateFlow(initialState)
    private val effectChannel = Channel<MainMenuEffect>(Channel.UNLIMITED)
    private val dispatchMutex = Mutex()

    override val state: StateFlow<MainMenuState> = mutableState.asStateFlow()
    override val effects: Flow<MainMenuEffect> = effectChannel.receiveAsFlow()

    override suspend fun dispatch(action: MainMenuAction) {
        dispatchMutex.withLock {
            val transition = MainMenuReducer.reduce(mutableState.value, action)
            mutableState.value = transition.state
            transition.effects.forEach { effectChannel.send(it) }
        }
    }
}
