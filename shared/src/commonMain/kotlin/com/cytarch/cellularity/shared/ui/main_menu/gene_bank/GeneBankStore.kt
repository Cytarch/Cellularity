package com.cytarch.cellularity.shared.ui.main_menu.gene_bank

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.GenomeFileInfo
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredFileShareOutcome
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.model.immutableListCopy
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionFeedback
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionKind
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionNotice
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionOperation
import com.cytarch.cellularity.shared.ui.main_menu.storageNotice
import com.cytarch.cellularity.shared.ui.state.Store
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

data class GeneBankItem(
    val id: StoredGenomeId,
    val displayName: String,
    val details: GeneBankItemDetails,
    val geneCount: Int? = null,
    val initialGeneIndex: Int? = null,
    val uniqueGeneCount: Int? = null,
    val cellTypeMask: Long? = null,
    val previewAvailable: Boolean = false
) {
    val cellTypeCount: Int?
        get() = cellTypeMask?.countOneBits()
}

sealed interface GeneBankItemDetails {
    data object Readable : GeneBankItemDetails
    data class NewerVersion(val error: StorageError) : GeneBankItemDetails
    data class Unreadable(val error: StorageError) : GeneBankItemDetails
}

data class GeneBankState(
    val items: List<GeneBankItem> = emptyList()
)

sealed interface GeneBankAction {
    data object Refresh : GeneBankAction
    data class DeleteItem(val id: StoredGenomeId) : GeneBankAction
    data class RenameItem(
        val id: StoredGenomeId,
        val newDisplayName: String
    ) : GeneBankAction
    data class ShareItem(val id: StoredGenomeId) : GeneBankAction
    data class ShareFinished(val outcome: StoredFileShareOutcome) : GeneBankAction
}

sealed interface GeneBankEffect {
    data class ShareGenome(val id: StoredGenomeId) : GeneBankEffect
    data class ShowNotice(val notice: FileCollectionNotice) : GeneBankEffect
}

internal data class GeneBankTransition(
    val state: GeneBankState,
    val effect: GeneBankEffect? = null
)

/** Pure state and effect transitions; repository orchestration belongs to [GeneBankStore]. */
internal object GeneBankReducer {
    fun reduce(state: GeneBankState, action: GeneBankAction): GeneBankTransition = when (action) {
        is GeneBankAction.ShareItem -> GeneBankTransition(
            state = state,
            effect = GeneBankEffect.ShareGenome(action.id)
        )
        is GeneBankAction.ShareFinished -> shareFinished(state, action.outcome)
        GeneBankAction.Refresh,
        is GeneBankAction.DeleteItem,
        is GeneBankAction.RenameItem -> GeneBankTransition(state)
    }

    fun replaceItems(
        state: GeneBankState,
        items: Iterable<GeneBankItem>
    ): GeneBankState = state.copy(items = immutableListCopy(items))

    fun storedItem(
        id: StoredGenomeId,
        result: StorageResult<GenomeFileInfo>
    ): GeneBankItem = when (result) {
        is StorageResult.Success -> GeneBankItem(
            id = id,
            displayName = result.value.displayName,
            details = if (
                result.value.compatibility == FileLoadCompatibility.NEWER_VERSION
            ) {
                GeneBankItemDetails.NewerVersion(
                    StorageError(StorageErrorCode.NEWER_VERSION)
                )
            } else {
                GeneBankItemDetails.Readable
            },
            geneCount = result.value.geneCount,
            initialGeneIndex = result.value.initialGeneIndex,
            uniqueGeneCount = result.value.uniqueGeneCount,
            cellTypeMask = result.value.cellTypeMask,
            previewAvailable = result.value.previewAvailable
        )
        is StorageResult.Failure -> GeneBankItem(
            id = id,
            displayName = id.defaultDisplayName,
            details = if (result.error.code == StorageErrorCode.NEWER_VERSION) {
                GeneBankItemDetails.NewerVersion(result.error)
            } else {
                GeneBankItemDetails.Unreadable(result.error)
            }
        )
    }

    private fun shareFinished(
        state: GeneBankState,
        outcome: StoredFileShareOutcome
    ): GeneBankTransition = when (outcome) {
        StoredFileShareOutcome.Started -> GeneBankTransition(state)
        StoredFileShareOutcome.NoCompatibleTarget -> GeneBankTransition(
            state = state,
            effect = GeneBankEffect.ShowNotice(
                FileCollectionNotice(
                    operation = FileCollectionOperation.SHARE,
                    feedback = FileCollectionFeedback.NO_SHARE_TARGET
                )
            )
        )
        is StoredFileShareOutcome.Failure -> GeneBankTransition(
            state = state,
            effect = GeneBankEffect.ShowNotice(
                storageNotice(
                    kind = FileCollectionKind.GENOMES,
                    operation = FileCollectionOperation.SHARE,
                    error = outcome.error
                )
            )
        )
    }
}

class GeneBankStore(
    private val fileRepository: CellularityFileRepository
) : Store<GeneBankState, GeneBankAction, GeneBankEffect> {
    private val mutableState = MutableStateFlow(GeneBankState())
    private val effectChannel = Channel<GeneBankEffect>(Channel.UNLIMITED)
    private val dispatchMutex = Mutex()

    override val state: StateFlow<GeneBankState> = mutableState.asStateFlow()
    override val effects: Flow<GeneBankEffect> = effectChannel.receiveAsFlow()

    override suspend fun dispatch(action: GeneBankAction) {
        dispatchMutex.withLock {
            val transition = GeneBankReducer.reduce(mutableState.value, action)
            mutableState.value = transition.state
            transition.effect?.let { effectChannel.send(it) }

            when (action) {
                GeneBankAction.Refresh -> refreshLocked()
                is GeneBankAction.DeleteItem -> deleteLocked(action.id)
                is GeneBankAction.RenameItem -> renameLocked(
                    action.id,
                    action.newDisplayName
                )
                is GeneBankAction.ShareItem,
                is GeneBankAction.ShareFinished -> Unit
            }
        }
    }

    private suspend fun refreshLocked() {
        when (val listed = fileRepository.listGenomeIds()) {
            is StorageResult.Success -> {
                val nextItems = listed.value.map { id ->
                    GeneBankReducer.storedItem(
                        id = id,
                        result = fileRepository.readGenomeInfo(id)
                    )
                }
                mutableState.value = GeneBankReducer.replaceItems(
                    mutableState.value,
                    nextItems
                )
            }
            is StorageResult.Failure -> emitFailure(
                FileCollectionOperation.REFRESH,
                listed.error
            )
        }
    }

    private suspend fun deleteLocked(id: StoredGenomeId) {
        when (val result = fileRepository.deleteGenome(id)) {
            is StorageResult.Success -> refreshLocked()
            is StorageResult.Failure -> emitFailure(
                FileCollectionOperation.DELETE,
                result.error
            )
        }
    }

    private suspend fun renameLocked(id: StoredGenomeId, requestedName: String) {
        val newDisplayName = requestedName.trim()
        if (!CellularityFileNames.isValidDisplayName(newDisplayName)) {
            emitFailure(
                FileCollectionOperation.RENAME,
                StorageError(StorageErrorCode.BAD_NAME)
            )
            return
        }
        when (val result = fileRepository.renameGenome(id, newDisplayName)) {
            is StorageResult.Success -> refreshLocked()
            is StorageResult.Failure -> emitFailure(
                FileCollectionOperation.RENAME,
                result.error
            )
        }
    }

    private suspend fun emitFailure(
        operation: FileCollectionOperation,
        error: StorageError
    ) {
        effectChannel.send(
            GeneBankEffect.ShowNotice(
                storageNotice(FileCollectionKind.GENOMES, operation, error)
            )
        )
    }
}
