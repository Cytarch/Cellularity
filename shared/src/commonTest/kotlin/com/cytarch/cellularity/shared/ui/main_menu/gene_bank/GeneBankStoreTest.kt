package com.cytarch.cellularity.shared.ui.main_menu.gene_bank

import com.cytarch.cellularity.shared.files.FakeCellularityFileRepository
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.GenomeFileInfo
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredFileShareOutcome
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.settings.GeneBankSortField
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionFeedback
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionOperation
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.MutableStateFlow
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

class GeneBankStoreTest {
    @Test
    fun shareActionEmitsTheStorageIdRatherThanTheDisplayName() {
        val id = StoredGenomeId("stable-file.genome")
        val state = GeneBankState(
            items = listOf(
                GeneBankItem(id, "A different visible title", GeneBankItemDetails.Readable)
            )
        )

        val transition = GeneBankReducer.reduce(state, GeneBankAction.ShareItem(id))

        assertEquals(id, assertIs<GeneBankEffect.ShareGenome>(transition.effect).id)
        assertTrue(transition.state === state)

        val future = GeneBankReducer.storedItem(
            id,
            StorageResult.Success(
                GenomeFileInfo(
                    displayName = "Future",
                    geneCount = 12,
                    initialGeneIndex = 3,
                    uniqueGeneCount = 8,
                    cellTypeMask = 0b1011L,
                    previewAvailable = true,
                    compatibility = FileLoadCompatibility.NEWER_VERSION
                )
            )
        )
        assertIs<GeneBankItemDetails.NewerVersion>(future.details)
        assertEquals(12, future.geneCount)
        assertEquals(3, future.initialGeneIndex)
        assertEquals(8, future.uniqueGeneCount)
        assertEquals(3, future.cellTypeCount)
        assertTrue(future.previewAvailable)
    }

    @Test
    fun collectionFilteringAndEveryMetadataSortKeepUnknownValuesLast() {
        val low = GeneBankItem(
            id = StoredGenomeId("alpha.genome"),
            displayName = "Alpha",
            details = GeneBankItemDetails.Readable,
            geneCount = 2,
            initialGeneIndex = 0,
            uniqueGeneCount = 1,
            cellTypeMask = 0b1L
        )
        val high = GeneBankItem(
            id = StoredGenomeId("zeta.genome"),
            displayName = "Zeta",
            details = GeneBankItemDetails.Readable,
            geneCount = 20,
            initialGeneIndex = 10,
            uniqueGeneCount = 15,
            cellTypeMask = 0b1111L
        )
        val unknown = GeneBankItem(
            id = StoredGenomeId("unknown.genome"),
            displayName = "Unknown",
            details = GeneBankItemDetails.Unreadable(
                StorageError(StorageErrorCode.CORRUPTED)
            )
        )

        GeneBankSortField.entries
            .filterNot { it == GeneBankSortField.NAME }
            .forEach { field ->
                assertEquals(
                    listOf(high, low, unknown),
                    filterAndSortGeneBankItems(
                        listOf(unknown, low, high),
                        searchQuery = "",
                        sortField = field,
                        sortDirection = GeneBankSortDirection.DESCENDING
                    )
                )
                assertEquals(
                    listOf(low, high, unknown),
                    filterAndSortGeneBankItems(
                        listOf(unknown, high, low),
                        searchQuery = "",
                        sortField = field,
                        sortDirection = GeneBankSortDirection.ASCENDING
                    )
                )
            }

        assertEquals(
            listOf(high),
            filterAndSortGeneBankItems(
                listOf(low, high, unknown),
                searchQuery = "zet",
                sortField = GeneBankSortField.NAME,
                sortDirection = GeneBankSortDirection.ASCENDING
            )
        )
    }

    @Test
    fun refreshFailurePreservesSnapshotAndRetainsPermissionErrorOnce() = runBlocking {
        val id = StoredGenomeId("stable.genome")
        val repository = FakeCellularityFileRepository().apply {
            listGenomeIdsHandler = { StorageResult.Success(listOf(id)) }
            readGenomeInfoHandler = { _ ->
                StorageResult.Success(GenomeFileInfo("Stable title"))
            }
        }
        val store = GeneBankStore(repository)
        store.dispatch(GeneBankAction.Refresh)
        val stable = store.state.value.items

        repository.listGenomeIdsHandler = {
            StorageResult.Failure(
                StorageError(StorageErrorCode.PERMISSION_REVOKED, "revoked")
            )
        }
        store.dispatch(GeneBankAction.Refresh)

        assertFalse(store.state is MutableStateFlow<*>)
        assertTrue(store.state.value.items === stable)
        val effect = assertIs<GeneBankEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionOperation.REFRESH, effect.notice.operation)
        assertEquals(FileCollectionFeedback.FILE_UNAVAILABLE, effect.notice.feedback)
        assertEquals(StorageErrorCode.PERMISSION_REVOKED, effect.notice.storageError?.code)
        assertNull(withTimeoutOrNull(50) { store.effects.first() })
    }

    @Test
    fun successfulDeleteRefreshesAndUsesExactGenomeId() = runBlocking {
        val first = StoredGenomeId("visible.genome")
        val second = StoredGenomeId("keep.genome")
        var ids = listOf(first, second)
        var deleted: StoredGenomeId? = null
        val repository = FakeCellularityFileRepository().apply {
            listGenomeIdsHandler = { StorageResult.Success(ids) }
            readGenomeInfoHandler = { id ->
                StorageResult.Success(GenomeFileInfo("title:${id.defaultDisplayName}"))
            }
            deleteGenomeHandler = { id ->
                deleted = id
                ids = ids - id
                StorageResult.Success(Unit)
            }
        }
        val store = GeneBankStore(repository)
        store.dispatch(GeneBankAction.Refresh)

        store.dispatch(GeneBankAction.DeleteItem(first))

        assertEquals(first, deleted)
        assertEquals(listOf(second), store.state.value.items.map { it.id })
    }

    @Test
    fun renameKeepsFilenameAndNewDisplayNameInSeparateArguments() = runBlocking {
        val oldId = StoredGenomeId("storage-id.genome")
        val renamedId = StoredGenomeId("New visible name.genome")
        var ids = listOf(oldId)
        var repositoryArguments: Pair<StoredGenomeId, String>? = null
        val repository = FakeCellularityFileRepository().apply {
            listGenomeIdsHandler = { StorageResult.Success(ids) }
            readGenomeInfoHandler = { id ->
                StorageResult.Success(GenomeFileInfo(id.defaultDisplayName))
            }
            renameGenomeHandler = { id, newDisplayName ->
                repositoryArguments = id to newDisplayName
                ids = listOf(renamedId)
                StorageResult.Success(Unit)
            }
        }
        val store = GeneBankStore(repository)

        store.dispatch(GeneBankAction.RenameItem(oldId, "  New visible name  "))

        assertEquals(oldId to "New visible name", repositoryArguments)
        assertEquals(listOf(renamedId), store.state.value.items.map { it.id })
    }

    @Test
    fun duplicateAndBadNamesHaveDistinctTypedFeedback() = runBlocking {
        var renameCalls = 0
        val duplicate = StorageError(StorageErrorCode.NAME_EXISTS, "duplicate")
        val repository = FakeCellularityFileRepository().apply {
            renameGenomeHandler = { _, _ ->
                renameCalls += 1
                StorageResult.Failure(duplicate)
            }
        }
        val store = GeneBankStore(repository)
        val id = StoredGenomeId("old.genome")

        store.dispatch(GeneBankAction.RenameItem(id, "new"))
        val duplicateEffect = assertIs<GeneBankEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionFeedback.GENOME_NAME_EXISTS, duplicateEffect.notice.feedback)
        assertEquals(duplicate, duplicateEffect.notice.storageError)

        store.dispatch(GeneBankAction.RenameItem(id, "../bad"))
        val badNameEffect = assertIs<GeneBankEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionFeedback.BAD_NAME, badNameEffect.notice.feedback)
        assertEquals(1, renameCalls)
    }

    @Test
    fun cancelledRefreshDoesNotPublishPartialGenomeList() = runBlocking {
        val stableId = StoredGenomeId("stable.genome")
        val repository = FakeCellularityFileRepository().apply {
            listGenomeIdsHandler = { StorageResult.Success(listOf(stableId)) }
            readGenomeInfoHandler = { _ ->
                StorageResult.Success(GenomeFileInfo("stable"))
            }
        }
        val store = GeneBankStore(repository)
        store.dispatch(GeneBankAction.Refresh)
        val stable = store.state.value.items

        val first = StoredGenomeId("first.genome")
        val waiting = StoredGenomeId("waiting.genome")
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<StorageResult<GenomeFileInfo>>()
        repository.listGenomeIdsHandler = { StorageResult.Success(listOf(first, waiting)) }
        repository.readGenomeInfoHandler = { id ->
            if (id == waiting) {
                entered.complete(Unit)
                release.await()
            } else {
                StorageResult.Success(GenomeFileInfo("first"))
            }
        }

        val refresh = launch { store.dispatch(GeneBankAction.Refresh) }
        entered.await()
        refresh.cancelAndJoin()

        assertTrue(store.state.value.items === stable)
    }

    @Test
    fun shareCompletionReportsNoTargetAndExactStorageFailure() = runBlocking {
        val store = GeneBankStore(FakeCellularityFileRepository())

        store.dispatch(GeneBankAction.ShareFinished(StoredFileShareOutcome.NoCompatibleTarget))
        val unavailable = assertIs<GeneBankEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionFeedback.NO_SHARE_TARGET, unavailable.notice.feedback)

        val failure = StorageError(StorageErrorCode.PERMISSION_REVOKED, "denied")
        store.dispatch(GeneBankAction.ShareFinished(StoredFileShareOutcome.Failure(failure)))
        val failed = assertIs<GeneBankEffect.ShowNotice>(store.effects.first())
        assertEquals(FileCollectionOperation.SHARE, failed.notice.operation)
        assertEquals(failure, failed.notice.storageError)
    }
}
