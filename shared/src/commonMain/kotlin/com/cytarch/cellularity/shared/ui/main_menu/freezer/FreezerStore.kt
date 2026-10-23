package com.cytarch.cellularity.shared.ui.main_menu.freezer

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredFileShareOutcome
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.SubstrateFileInfo
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

sealed interface FreezerItem {
    data object NewPlate : FreezerItem

    data class Stored(
        val id: StoredSubstrateId,
        val displayName: String,
        val details: FreezerItemDetails,
        val previewAvailable: Boolean = false
    ) : FreezerItem
}

sealed interface FreezerItemDetails {
    data class Readable(
        val cellCount: Int,
        val unfrozenAgeHours: Double,
        val maximumCellCount: Int? = null,
        val foodCount: Int? = null,
        val maximumFoodCount: Int? = null,
        val foodMass: Double? = null,
        val dishDiameterMillimeters: Double? = null,
        val createdAtUnixMillis: Long? = null
    ) : FreezerItemDetails

    data class NewerVersion(val error: StorageError) : FreezerItemDetails
    data class Unreadable(val error: StorageError) : FreezerItemDetails
}

data class FreezerState(
    val items: List<FreezerItem> = listOf(FreezerItem.NewPlate),
    val launchPending: Boolean = false
)

data class FreezerAdvancedConfiguration(
    val maximumCells: Int,
    val maximumFood: Int,
    val radius: Double,
    val innerRadius: Double = 0.0
)

data class FreezerOpenRequest(
    val storedSubstrateId: StoredSubstrateId? = null,
    val advancedConfiguration: FreezerAdvancedConfiguration? = null
)

sealed interface FreezerAction {
    data object Refresh : FreezerAction
    data object OpenNewPlate : FreezerAction
    data class OpenStored(val id: StoredSubstrateId) : FreezerAction
    data class DeleteItem(val id: StoredSubstrateId) : FreezerAction
    data class RenameItem(
        val id: StoredSubstrateId,
        val newDisplayName: String
    ) : FreezerAction
    data class ShareItem(val id: StoredSubstrateId) : FreezerAction
    data class ShareFinished(val outcome: StoredFileShareOutcome) : FreezerAction
    data class StartAdvanced(
        val cells: Int,
        val food: Int,
        val radius: Double,
        val innerRadius: Double = 0.0
    ) : FreezerAction

    data object ReturnedToForeground : FreezerAction
}

sealed interface FreezerEffect {
    data class OpenPlate(val request: FreezerOpenRequest) : FreezerEffect
    data class ShareSubstrate(val id: StoredSubstrateId) : FreezerEffect
    data class ShowNotice(val notice: FileCollectionNotice) : FreezerEffect
}

internal data class FreezerTransition(
    val state: FreezerState,
    val effect: FreezerEffect? = null
)

/** Pure state and effect transitions; repository orchestration belongs to [FreezerStore]. */
internal object FreezerReducer {
    fun reduce(state: FreezerState, action: FreezerAction): FreezerTransition = when (action) {
        FreezerAction.OpenNewPlate -> requestOpen(state, FreezerOpenRequest())
        is FreezerAction.OpenStored -> requestOpen(
            state,
            FreezerOpenRequest(storedSubstrateId = action.id)
        )
        is FreezerAction.StartAdvanced -> requestOpen(
            state,
            FreezerOpenRequest(
                advancedConfiguration = FreezerAdvancedConfiguration(
                    maximumCells = action.cells,
                    maximumFood = action.food,
                    radius = action.radius,
                    innerRadius = action.innerRadius
                )
            )
        )
        is FreezerAction.ShareItem -> FreezerTransition(
            state = state,
            effect = FreezerEffect.ShareSubstrate(action.id)
        )
        is FreezerAction.ShareFinished -> shareFinished(state, action.outcome)
        FreezerAction.ReturnedToForeground -> FreezerTransition(
            state.copy(launchPending = false)
        )
        FreezerAction.Refresh,
        is FreezerAction.DeleteItem,
        is FreezerAction.RenameItem -> FreezerTransition(state)
    }

    fun replaceStoredItems(
        state: FreezerState,
        storedItems: Iterable<FreezerItem.Stored>
    ): FreezerState = state.copy(
        items = immutableListCopy(listOf(FreezerItem.NewPlate) + storedItems)
    )

    fun storedItem(
        id: StoredSubstrateId,
        result: StorageResult<SubstrateFileInfo>
    ): FreezerItem.Stored {
        val details = when (result) {
            is StorageResult.Success -> if (
                result.value.compatibility == FileLoadCompatibility.NEWER_VERSION
            ) {
                FreezerItemDetails.NewerVersion(
                    StorageError(StorageErrorCode.NEWER_VERSION)
                )
            } else {
                FreezerItemDetails.Readable(
                    cellCount = result.value.cellCount,
                    maximumCellCount = result.value.maximumCellCount,
                    foodCount = result.value.foodCount,
                    maximumFoodCount = result.value.maximumFoodCount,
                    foodMass = result.value.foodMass,
                    dishDiameterMillimeters = result.value.dishDiameterMillimeters,
                    unfrozenAgeHours = result.value.time,
                    createdAtUnixMillis = result.value.createdAtUnixMillis
                )
            }
            is StorageResult.Failure -> if (
                result.error.code == StorageErrorCode.NEWER_VERSION
            ) {
                FreezerItemDetails.NewerVersion(result.error)
            } else {
                FreezerItemDetails.Unreadable(result.error)
            }
        }
        return FreezerItem.Stored(
            id = id,
            displayName = id.value,
            details = details,
            previewAvailable = result is StorageResult.Success &&
                result.value.previewAvailable
        )
    }

    private fun requestOpen(
        state: FreezerState,
        request: FreezerOpenRequest
    ): FreezerTransition {
        if (state.launchPending) return FreezerTransition(state)
        return FreezerTransition(
            state = state.copy(launchPending = true),
            effect = FreezerEffect.OpenPlate(request)
        )
    }

    private fun shareFinished(
        state: FreezerState,
        outcome: StoredFileShareOutcome
    ): FreezerTransition = when (outcome) {
        StoredFileShareOutcome.Started -> FreezerTransition(state)
        StoredFileShareOutcome.NoCompatibleTarget -> FreezerTransition(
            state = state,
            effect = FreezerEffect.ShowNotice(
                FileCollectionNotice(
                    operation = FileCollectionOperation.SHARE,
                    feedback = FileCollectionFeedback.NO_SHARE_TARGET
                )
            )
        )
        is StoredFileShareOutcome.Failure -> FreezerTransition(
            state = state,
            effect = FreezerEffect.ShowNotice(
                storageNotice(
                    kind = FileCollectionKind.SUBSTRATES,
                    operation = FileCollectionOperation.SHARE,
                    error = outcome.error
                )
            )
        )
    }
}

class FreezerStore(
    private val fileRepository: CellularityFileRepository
) : Store<FreezerState, FreezerAction, FreezerEffect> {
    private val mutableState = MutableStateFlow(FreezerState())
    private val effectChannel = Channel<FreezerEffect>(Channel.UNLIMITED)
    private val dispatchMutex = Mutex()

    override val state: StateFlow<FreezerState> = mutableState.asStateFlow()
    override val effects: Flow<FreezerEffect> = effectChannel.receiveAsFlow()

    override suspend fun dispatch(action: FreezerAction) {
        dispatchMutex.withLock {
            val transition = FreezerReducer.reduce(mutableState.value, action)
            mutableState.value = transition.state
            transition.effect?.let { effectChannel.send(it) }

            when (action) {
                FreezerAction.Refresh -> refreshLocked()
                is FreezerAction.DeleteItem -> deleteLocked(action.id)
                is FreezerAction.RenameItem -> renameLocked(
                    action.id,
                    action.newDisplayName
                )
                FreezerAction.OpenNewPlate,
                is FreezerAction.OpenStored,
                is FreezerAction.ShareItem,
                is FreezerAction.ShareFinished,
                is FreezerAction.StartAdvanced,
                FreezerAction.ReturnedToForeground -> Unit
            }
        }
    }

    private suspend fun refreshLocked() {
        when (val listed = fileRepository.listSubstrateIds()) {
            is StorageResult.Success -> {
                val nextItems = listed.value.map { id ->
                    FreezerReducer.storedItem(
                        id = id,
                        result = fileRepository.readSubstrateInfo(id)
                    )
                }
                mutableState.value = FreezerReducer.replaceStoredItems(
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

    private suspend fun deleteLocked(id: StoredSubstrateId) {
        when (val result = fileRepository.deleteSubstrate(id)) {
            is StorageResult.Success -> refreshLocked()
            is StorageResult.Failure -> emitFailure(
                FileCollectionOperation.DELETE,
                result.error
            )
        }
    }

    private suspend fun renameLocked(id: StoredSubstrateId, requestedName: String) {
        val newDisplayName = requestedName.trim()
        if (!CellularityFileNames.isValidDisplayName(newDisplayName)) {
            emitFailure(
                FileCollectionOperation.RENAME,
                StorageError(StorageErrorCode.BAD_NAME)
            )
            return
        }
        when (val result = fileRepository.renameSubstrate(id, newDisplayName)) {
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
            FreezerEffect.ShowNotice(
                storageNotice(FileCollectionKind.SUBSTRATES, operation, error)
            )
        )
    }
}
