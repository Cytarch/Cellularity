package com.cytarch.cellularity.shared.files

import com.cytarch.cellularity.shared.model.GenomeDraft

internal class FakeCellularityFileRepository : CellularityFileRepository {
    var listSubstrateIdsHandler: suspend () -> StorageResult<List<StoredSubstrateId>> = {
        StorageResult.Success(emptyList())
    }
    var readSubstrateInfoHandler:
        suspend (StoredSubstrateId) -> StorageResult<SubstrateFileInfo> =
        { id -> StorageResult.Failure(StorageError(StorageErrorCode.NOT_FOUND, id.value)) }
    var substrateExistsHandler: suspend (String?) -> StorageResult<Boolean> = {
        StorageResult.Success(false)
    }
    var deleteSubstrateHandler: suspend (StoredSubstrateId) -> StorageResult<Unit> = {
        StorageResult.Success(Unit)
    }
    var renameSubstrateHandler:
        suspend (StoredSubstrateId, String) -> StorageResult<Unit> = { _, _ ->
            StorageResult.Success(Unit)
        }
    var listGenomeIdsHandler: suspend () -> StorageResult<List<StoredGenomeId>> = {
        StorageResult.Success(emptyList())
    }
    var listGenomeNamesHandler: suspend () -> StorageResult<List<String>> = {
        StorageResult.Success(emptyList())
    }
    var readGenomeInfoHandler: suspend (StoredGenomeId) -> StorageResult<GenomeFileInfo> =
        { id -> StorageResult.Failure(StorageError(StorageErrorCode.NOT_FOUND, id.fileName)) }
    var readGenomeDraftHandler: suspend (String?) -> StorageResult<GenomeDraft> = {
        StorageResult.Failure(StorageError(StorageErrorCode.NOT_FOUND))
    }
    var writeGenomeDraftHandler: suspend (String?, GenomeDraft) -> StorageResult<Unit> =
        { _, _ -> StorageResult.Success(Unit) }
    var deleteGenomeHandler: suspend (StoredGenomeId) -> StorageResult<Unit> = {
        StorageResult.Success(Unit)
    }
    var renameGenomeHandler: suspend (StoredGenomeId, String) -> StorageResult<Unit> = { _, _ ->
        StorageResult.Success(Unit)
    }

    override suspend fun listSubstrateIds(): StorageResult<List<StoredSubstrateId>> =
        listSubstrateIdsHandler()

    override suspend fun readSubstrateInfo(
        id: StoredSubstrateId
    ): StorageResult<SubstrateFileInfo> = readSubstrateInfoHandler(id)

    override suspend fun substrateExists(name: String?): StorageResult<Boolean> =
        substrateExistsHandler(name)

    override suspend fun deleteSubstrate(id: StoredSubstrateId): StorageResult<Unit> =
        deleteSubstrateHandler(id)

    override suspend fun renameSubstrate(
        id: StoredSubstrateId,
        newDisplayName: String
    ): StorageResult<Unit> = renameSubstrateHandler(id, newDisplayName)

    override suspend fun listGenomeIds(): StorageResult<List<StoredGenomeId>> =
        listGenomeIdsHandler()

    override suspend fun listGenomeNames(): StorageResult<List<String>> =
        listGenomeNamesHandler()

    override suspend fun readGenomeInfo(
        id: StoredGenomeId
    ): StorageResult<GenomeFileInfo> = readGenomeInfoHandler(id)

    override suspend fun readGenomeDraft(
        nameOrFilename: String?
    ): StorageResult<GenomeDraft> = readGenomeDraftHandler(nameOrFilename)

    override suspend fun writeGenomeDraft(
        nameOrFilename: String?,
        genome: GenomeDraft
    ): StorageResult<Unit> = writeGenomeDraftHandler(nameOrFilename, genome)

    override suspend fun deleteGenome(id: StoredGenomeId): StorageResult<Unit> =
        deleteGenomeHandler(id)

    override suspend fun renameGenome(
        id: StoredGenomeId,
        newDisplayName: String
    ): StorageResult<Unit> = renameGenomeHandler(id, newDisplayName)
}
