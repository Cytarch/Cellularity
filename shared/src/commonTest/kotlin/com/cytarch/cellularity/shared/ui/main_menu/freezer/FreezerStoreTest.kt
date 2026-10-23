package com.cytarch.cellularity.shared.ui.main_menu.freezer

import com.cytarch.cellularity.shared.files.FakeCellularityFileRepository
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredFileShareOutcome
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.SubstrateFileInfo
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionFeedback
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionOperation
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class FreezerStoreTest {
    @Test
    fun reducerCreatesTypedOpenEffectAndSuppressesDuplicateLaunch() {
        val id = StoredSubstrateId("saved")

        val first = FreezerReducer.reduce(FreezerState(), FreezerAction.OpenStored(id))
        val duplicate = FreezerReducer.reduce(first.state, FreezerAction.OpenStored(id))
        val resumed = FreezerReducer.reduce(
            duplicate.state,
            FreezerAction.ReturnedToForeground
        )

        assertTrue(first.state.launchPending)
        assertEquals(
            FreezerOpenRequest(storedSubstrateId = id),
            assertIs<FreezerEffect.OpenPlate>(first.effect).request
        )
        assertNull(duplicate.effect)
        assertFalse(resumed.state.launchPending)

        val future = FreezerReducer.storedItem(
            id,
            StorageResult.Success(
                SubstrateFileInfo(
                    time = 2.0,
                    cellCount = 3,
                    previewAvailable = true,
                    compatibility = FileLoadCompatibility.NEWER_VERSION
                )
            )
        )
        assertIs<FreezerItemDetails.NewerVersion>(future.details)
        assertTrue(future.previewAvailable)

        val low = FreezerReducer.storedItem(
            StoredSubstrateId("alpha"),
            StorageResult.Success(
                SubstrateFileInfo(
                    time = 1.0,
                    cellCount = 1,
                    maximumCellCount = 10,
                    foodCount = 2,
                    maximumFoodCount = 20,
                    foodMass = 3.0,
                    dishDiameterMillimeters = 4.0,
                    createdAtUnixMillis = 5L
                )
            )
        )
        val high = FreezerReducer.storedItem(
            StoredSubstrateId("zeta"),
            StorageResult.Success(
                SubstrateFileInfo(
                    time = 11.0,
                    cellCount = 12,
                    maximumCellCount = 130,
                    foodCount = 14,
                    maximumFoodCount = 150,
                    foodMass = 16.0,
                    dishDiameterMillimeters = 17.0,
                    createdAtUnixMillis = 18L
                )
            )
        )
        assertEquals(
            FreezerItemDetails.Readable(
                cellCount = 12,
                unfrozenAgeHours = 11.0,
                maximumCellCount = 130,
                foodCount = 14,
                maximumFoodCount = 150,
                foodMass = 16.0,
                dishDiameterMillimeters = 17.0,
                createdAtUnixMillis = 18L
            ),
            high.details
        )
        FreezerSortField.entries
            .filterNot { it == FreezerSortField.NAME }
            .forEach { field ->
                val source = listOf(future, low, FreezerItem.NewPlate, high)
                val descending = filterAndSortFreezerItems(
                    source,
                    searchQuery = "",
                    sortField = field,
                    sortDirection = FreezerSortDirection.DESCENDING
                )
                val ascending = filterAndSortFreezerItems(
                    source,
                    searchQuery = "",
                    sortField = field,
                    sortDirection = FreezerSortDirection.ASCENDING
                )
                assertEquals(
                    listOf(FreezerItem.NewPlate, high, low, future),
                    descending
                )
                assertEquals(
                    listOf(FreezerItem.NewPlate, low, high, future),
                    ascending
                )
            }
        assertEquals(
            listOf(FreezerItem.NewPlate, high, future, low),
            filterAndSortFreezerItems(
                listOf(future, low, FreezerItem.NewPlate, high),
                searchQuery = "",
                sortField = FreezerSortField.NAME,
                sortDirection = FreezerSortDirection.DESCENDING
            )
        )
    }

    @Test
    fun reducerMapsAdvancedConfigurationWithoutPlatformObjects() {
        val transition = FreezerReducer.reduce(
            FreezerState(),
            FreezerAction.StartAdvanced(cells = 12, food = 34, radius = 5.5)
        )

        assertEquals(
            FreezerAdvancedConfiguration(12, 34, 5.5),
            assertIs<FreezerEffect.OpenPlate>(transition.effect)
                .request
                .advancedConfiguration
        )
    }

    @Test
    fun refreshFailureKeepsFallbackAndRetainsPermissionCauseOnce() = runBlocking {
        val repository = FakeCellularityFileRepository().apply {
            listSubstrateIdsHandler = {
                StorageResult.Failure(
                    StorageError(StorageErrorCode.PERMISSION_REVOKED, "test")
                )
            }
        }
        val store = FreezerStore(repository)

        store.dispatch(FreezerAction.Refresh)

        assertEquals(listOf(FreezerItem.NewPlate), store.state.value.items)
        val effect = assertIs<FreezerEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionOperation.REFRESH, effect.notice.operation)
        assertEquals(FileCollectionFeedback.FILE_UNAVAILABLE, effect.notice.feedback)
        assertEquals(StorageErrorCode.PERMISSION_REVOKED, effect.notice.storageError?.code)
        assertNull(withTimeoutOrNull(50) { store.effects.first() })
    }

    @Test
    fun successfulDeleteRefreshesSnapshotInsideStore() = runBlocking {
        var ids = listOf(StoredSubstrateId("saved"))
        val repository = FakeCellularityFileRepository().apply {
            listSubstrateIdsHandler = { StorageResult.Success(ids) }
            readSubstrateInfoHandler = { _ ->
                StorageResult.Success(SubstrateFileInfo(time = 2.0, cellCount = 3))
            }
            deleteSubstrateHandler = { id ->
                ids = ids - id
                StorageResult.Success(Unit)
            }
        }
        val store = FreezerStore(repository)
        store.dispatch(FreezerAction.Refresh)

        store.dispatch(FreezerAction.DeleteItem(StoredSubstrateId("saved")))

        assertEquals(listOf(FreezerItem.NewPlate), store.state.value.items)
    }

    @Test
    fun renameFailureCarriesProductMeaningAndExactRepositoryErrorOnce() = runBlocking {
        val duplicate = StorageError(StorageErrorCode.NAME_EXISTS, "duplicate")
        val repository = FakeCellularityFileRepository().apply {
            renameSubstrateHandler = { _, _ -> StorageResult.Failure(duplicate) }
        }
        val store = FreezerStore(repository)

        store.dispatch(
            FreezerAction.RenameItem(StoredSubstrateId("old"), "new")
        )

        val effect = assertIs<FreezerEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionOperation.RENAME, effect.notice.operation)
        assertEquals(FileCollectionFeedback.SUBSTRATE_NAME_EXISTS, effect.notice.feedback)
        assertEquals(duplicate, effect.notice.storageError)
        assertNull(withTimeoutOrNull(50) { store.effects.first() })
    }

    @Test
    fun badRenameIsRejectedBeforeRepositoryCall() = runBlocking {
        var renameCalls = 0
        val repository = FakeCellularityFileRepository().apply {
            renameSubstrateHandler = { _, _ ->
                renameCalls += 1
                StorageResult.Success(Unit)
            }
        }
        val store = FreezerStore(repository)

        store.dispatch(
            FreezerAction.RenameItem(StoredSubstrateId("old"), "folder/new")
        )

        assertEquals(0, renameCalls)
        val effect = assertIs<FreezerEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionFeedback.BAD_NAME, effect.notice.feedback)
        assertEquals(StorageErrorCode.BAD_NAME, effect.notice.storageError?.code)
    }

    @Test
    fun shareUsesTypedIdAndMapsPlatformCompletionBackIntoOneShotEffects() = runBlocking {
        val id = StoredSubstrateId("sample")
        val store = FreezerStore(FakeCellularityFileRepository())

        store.dispatch(FreezerAction.ShareItem(id))
        assertEquals(id, assertIs<FreezerEffect.ShareSubstrate>(store.effects.first()).id)

        store.dispatch(
            FreezerAction.ShareFinished(StoredFileShareOutcome.NoCompatibleTarget)
        )
        val noTarget = assertIs<FreezerEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionFeedback.NO_SHARE_TARGET, noTarget.notice.feedback)

        val permissionError = StorageError(StorageErrorCode.PERMISSION_REVOKED, "denied")
        store.dispatch(
            FreezerAction.ShareFinished(StoredFileShareOutcome.Failure(permissionError))
        )
        val failed = assertIs<FreezerEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionOperation.SHARE, failed.notice.operation)
        assertEquals(permissionError, failed.notice.storageError)
    }
}
