/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.files

import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeFiles
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.genome.DecodedEmbeddedFilePreview
import com.cytarch.cellularity.shared.files.genome.EncodedGenomePreview
import com.cytarch.cellularity.shared.files.storage.AtomicFileStore
import com.cytarch.cellularity.shared.files.storage.CellularityFileStore
import com.cytarch.cellularity.shared.files.storage.CellularityNativeFiles
import com.cytarch.cellularity.shared.files.storage.ExternalCellularityFileKind
import com.cytarch.cellularity.shared.files.storage.ExternalFileName
import com.cytarch.cellularity.shared.files.storage.NioDurableFileOperations
import com.cytarch.cellularity.shared.files.storage.StorageExecutor
import com.cytarch.cellularity.shared.files.storage.StorageOperationException
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.nativewire.NativeSubstrateInspection
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.runBlocking

/**
 * Desktop file adapter: the shared [CellularityFileStore] -- the same implementation Android
 * uses -- in a directory on disk, plus what only a desktop has: external files as paths.
 */
class DesktopCellularityFileRepository private constructor(
    private val store: CellularityFileStore
) : CellularityFileRepository by store {

    constructor(
        storageDirectory: Path,
        native: CellularityNativeFiles = DesktopNativeFiles
    ) : this(
        CellularityFileStore(
            directoryProvider = {
                Files.createDirectories(storageDirectory)
                storageDirectory.toFile()
            },
            native = native,
            atomicFiles = AtomicFileStore(NioDurableFileOperations),
            storageExecutor = StorageExecutor(),
            durableFiles = NioDurableFileOperations
        )
    )

    suspend fun readGenomePreview(id: StoredGenomeId): DecodedEmbeddedFilePreview? =
        store.readGenomePreview(id)

    suspend fun writeGenomeDraftWithPreview(
        nameOrFilename: String?,
        genome: GenomeDraft,
        preview: (genomeDraftWire: ByteArray) -> EncodedGenomePreview?
    ): StorageResult<Unit> = store.writeGenomeDraftWithPreview(nameOrFilename, genome, preview)

    suspend fun inspectStoredSubstrate(
        id: StoredSubstrateId
    ): StorageResult<NativeSubstrateInspection> = store.inspectStoredSubstrate(id)

    /** The encoded picture a stored plate carries, or null when it has none. */
    suspend fun readSubstratePreview(id: StoredSubstrateId): DecodedEmbeddedFilePreview? =
        store.readSubstratePreview(id)

    /** Where a stored substrate is, for the renderer that decodes plates from a path. */
    suspend fun substrateFilePath(id: StoredSubstrateId): StorageResult<Path> =
        store.execute { Path.of(store.substratePathOnSimulationWorker(id)) }

    /**
     * Writes the running plate under [id]. [replaceExisting] is true only when the plate being
     * saved is the one already open under that name; anything else under that name is refused.
     */
    suspend fun saveSubstrate(
        id: StoredSubstrateId,
        replaceExisting: Boolean,
        produce: suspend (temporary: Path) -> Unit
    ): StorageResult<Unit> = store.execute {
        store.saveSubstrateAtomicallyOnSimulationWorker(id, replaceExisting) { temporaryPath ->
            runBlocking { produce(Path.of(temporaryPath)) }
        }
    }

    /** Resolves the name and kind of a file outside the store, by the shared naming rules. */
    suspend fun resolveExternalFile(source: Path): StorageResult<DesktopExternalFile> =
        store.execute {
            if (!Files.isRegularFile(source)) {
                throw StorageOperationException(
                    StorageErrorCode.NOT_FOUND,
                    "There is no file at that location"
                )
            }
            val name = store.externalFileName(source.fileName?.toString().orEmpty())
            DesktopExternalFile(
                source = source,
                displayName = name.displayName,
                fileName = name.safeFileName,
                kind = when (name.kind) {
                    ExternalCellularityFileKind.SUBSTRATE -> DesktopExternalFileKind.SUBSTRATE
                    ExternalCellularityFileKind.GENOME -> DesktopExternalFileKind.GENOME
                }
            )
        }

    /** Copies an external file into the store, validated in place, refusing a taken name. */
    suspend fun importExternalFile(file: DesktopExternalFile): StorageResult<Unit> =
        store.importExternalFile(
            name = ExternalFileName(
                displayName = file.displayName,
                safeFileName = file.fileName,
                kind = when (file.kind) {
                    DesktopExternalFileKind.SUBSTRATE -> ExternalCellularityFileKind.SUBSTRATE
                    DesktopExternalFileKind.GENOME -> ExternalCellularityFileKind.GENOME
                }
            ),
            declaredSizeBytes = Files.size(file.source)
        ) { Files.newInputStream(file.source) }
}
