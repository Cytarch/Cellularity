package com.cytarch.cellularity.shared.files

import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerAction
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerItem
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerItemDetails
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerStore
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankAction
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankItemDetails
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankStore
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class FileRepositoryStateTest {
    @Test
    fun freezerPublishesOneImmutableSnapshotAndKeepsPerFileFailuresTyped() = runBlocking {
        val repository = FakeCellularityFileRepository().apply {
            listSubstrateIdsHandler = {
                StorageResult.Success(
                    listOf("alpha", "future", "broken").map(::StoredSubstrateId)
                )
            }
            readSubstrateInfoHandler = { id ->
                when (id.value) {
                    "alpha" -> StorageResult.Success(
                        SubstrateFileInfo(time = 1.25, cellCount = 42)
                    )
                    "future" -> StorageResult.Failure(
                        StorageError(StorageErrorCode.NEWER_VERSION, "future-format")
                    )
                    else -> StorageResult.Failure(
                        StorageError(StorageErrorCode.CORRUPTED, "bad-payload")
                    )
                }
            }
        }
        val store = FreezerStore(repository)

        store.dispatch(FreezerAction.Refresh)

        assertFalse(store.state is MutableStateFlow<*>)
        val items = store.state.value.items
        assertIs<FreezerItem.NewPlate>(items[0])
        val readable = assertIs<FreezerItem.Stored>(items[1])
        assertEquals(StoredSubstrateId("alpha"), readable.id)
        assertEquals(
            FreezerItemDetails.Readable(cellCount = 42, unfrozenAgeHours = 1.25),
            readable.details
        )
        val newer = assertIs<FreezerItemDetails.NewerVersion>(
            assertIs<FreezerItem.Stored>(items[2]).details
        )
        assertEquals("future-format", newer.error.detail)
        val unreadable = assertIs<FreezerItemDetails.Unreadable>(
            assertIs<FreezerItem.Stored>(items[3]).details
        )
        assertEquals(StorageErrorCode.CORRUPTED, unreadable.error.code)
    }

    @Test
    fun cancelledFreezerRefreshDoesNotPublishAPartialList() = runBlocking {
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<StorageResult<List<StoredSubstrateId>>>()
        val repository = FakeCellularityFileRepository().apply {
            listSubstrateIdsHandler = {
                StorageResult.Success(listOf(StoredSubstrateId("stable")))
            }
            readSubstrateInfoHandler = { _ ->
                StorageResult.Success(SubstrateFileInfo())
            }
        }
        val store = FreezerStore(repository)
        store.dispatch(FreezerAction.Refresh)
        val stable = store.state.value.items

        repository.listSubstrateIdsHandler = {
            entered.complete(Unit)
            release.await()
        }
        val refresh = launch { store.dispatch(FreezerAction.Refresh) }
        entered.await()
        refresh.cancelAndJoin()

        assertTrue(store.state.value.items === stable)
    }

    @Test
    fun geneBankPreservesStorageIdsWhilePublishingTypedReadFailures() = runBlocking {
        val repository = FakeCellularityFileRepository().apply {
            listGenomeIdsHandler = {
                StorageResult.Success(
                    listOf("valid.genome", "future.genome", "broken.genome")
                        .map(::StoredGenomeId)
                )
            }
            readGenomeInfoHandler = { id ->
                when (id.fileName) {
                    "valid.genome" -> StorageResult.Success(GenomeFileInfo("Visible title"))
                    "future.genome" -> StorageResult.Failure(
                        StorageError(StorageErrorCode.NEWER_VERSION)
                    )
                    else -> StorageResult.Failure(
                        StorageError(StorageErrorCode.CORRUPTED)
                    )
                }
            }
        }
        val store = GeneBankStore(repository)

        store.dispatch(GeneBankAction.Refresh)

        assertFalse(store.state is MutableStateFlow<*>)
        assertEquals(
            listOf("valid.genome", "future.genome", "broken.genome"),
            store.state.value.items.map { it.id.fileName }
        )
        assertEquals("Visible title", store.state.value.items[0].displayName)
        assertIs<GeneBankItemDetails.Readable>(store.state.value.items[0].details)
        assertEquals(
            StorageErrorCode.NEWER_VERSION,
            assertIs<GeneBankItemDetails.NewerVersion>(
                store.state.value.items[1].details
            ).error.code
        )
        assertEquals(
            StorageErrorCode.CORRUPTED,
            assertIs<GeneBankItemDetails.Unreadable>(
                store.state.value.items[2].details
            ).error.code
        )
    }
}
