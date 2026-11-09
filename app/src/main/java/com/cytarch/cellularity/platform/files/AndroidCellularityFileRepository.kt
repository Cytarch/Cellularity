package com.cytarch.cellularity.platform.files

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.core.content.FileProvider
import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.preview.GenomePreviewCaptureCoordinator
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.genome.DecodedEmbeddedFilePreview
import com.cytarch.cellularity.shared.files.genome.ReleaseGenomeCodecBridge
import com.cytarch.cellularity.shared.files.storage.AtomicFileStore
import com.cytarch.cellularity.shared.files.storage.CellularityFileStore
import com.cytarch.cellularity.shared.files.storage.CellularityNativeFiles
import com.cytarch.cellularity.shared.files.storage.ExternalCellularityFileKind
import com.cytarch.cellularity.shared.files.storage.ExternalFileName
import com.cytarch.cellularity.shared.files.storage.StorageExecutor
import com.cytarch.cellularity.shared.files.storage.StorageOperationException
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.nativewire.NativeSubstrateInspection
import java.io.File
import java.io.FileNotFoundException

data class ExternalCellularityFile(
    val displayName: String,
    val safeFileName: String,
    val kind: ExternalCellularityFileKind,
    val sourceUri: Uri,
    val declaredSizeBytes: Long?
)

@JvmInline
internal value class PrivateSubstratePath(val value: String)

/** Android's JNI route to the shared file store's native operations. */
internal object AndroidNativeFiles : CellularityNativeFiles {
    override val releaseGenomeCodec: ReleaseGenomeCodecBridge = NativeReleaseGenomeCodecBridge

    override fun validateGenomeDraft(genomeDraftWire: ByteArray) {
        NativeCellularityControllerBridge.validateGenomeDraft(genomeDraftWire)
    }

    override fun operationalSubstrateFileLimitBytes(): Long =
        NativeCellularityControllerBridge.operationalSubstrateFileLimitBytes()

    override fun inspectSubstratePath(
        path: String,
        previewMode: Boolean,
        fallback: SimulationConfiguration
    ): ByteArray = NativeCellularityControllerBridge.inspectSubstratePath(path, previewMode, fallback)

    override fun validateSubstratePath(
        path: String,
        previewMode: Boolean,
        fallback: SimulationConfiguration
    ) {
        NativeCellularityControllerBridge.validateSubstratePath(path, previewMode, fallback)
    }

    override fun readSubstratePreviewPath(path: String): ByteArray =
        NativeCellularityControllerBridge.readSubstratePreviewPath(path)
}

/**
 * Android file adapter: the shared [CellularityFileStore] in the app's private files
 * directory, plus what only Android has -- document-provider imports and FileProvider shares.
 * Common code receives IDs and metadata, never paths or payloads.
 */
class AndroidCellularityFileRepository private constructor(
    private val context: Context,
    private val store: CellularityFileStore
) : CellularityFileRepository by store {

    internal constructor(
        context: Context,
        atomicFiles: AtomicFileStore,
        storageExecutor: StorageExecutor
    ) : this(
        context.applicationContext ?: context,
        CellularityFileStore(
            directoryProvider = { (context.applicationContext ?: context).filesDir },
            native = AndroidNativeFiles,
            atomicFiles = atomicFiles,
            storageExecutor = storageExecutor,
            durableFiles = AndroidDurableFileOperations
        )
    )

    constructor(context: Context) : this(
        context = context,
        atomicFiles = AtomicFileStore(AndroidDurableFileOperations),
        storageExecutor = StorageExecutor()
    )

    internal suspend fun readGenomePreview(id: StoredGenomeId): DecodedEmbeddedFilePreview? =
        store.readGenomePreview(id)

    internal suspend fun writeGenomeDraftWithPreview(
        nameOrFilename: String?,
        genome: GenomeDraft,
        previewCapture: GenomePreviewCaptureCoordinator,
        maximumPreviewTime: Double
    ): StorageResult<Unit> = store.writeGenomeDraftWithPreview(nameOrFilename, genome) { wire ->
        try {
            previewCapture.capture(
                genomeDraftBytes = wire,
                maximumSimulationTime = maximumPreviewTime
            )?.let {
                GenomePreviewEncoder.encodePng(
                    rgbaBytes = requireNotNull(it.payload),
                    pixelWidth = it.width,
                    pixelHeight = it.height,
                    rowStrideBytes = it.rowStrideBytes
                )
            }
        } catch (failure: Exception) {
            AppLog.error("Could not generate the saved genome preview; saving without it", failure)
            null
        }
    }

    /** Metadata inspection used before opening a retained simulation session. */
    internal suspend fun inspectStoredSubstrate(
        id: StoredSubstrateId
    ): StorageResult<NativeSubstrateInspection> = store.inspectStoredSubstrate(id)

    /** Reads only the CRC-checked encoded image for a visible Freezer row. */
    internal suspend fun readSubstratePreview(id: StoredSubstrateId): DecodedEmbeddedFilePreview? =
        store.readSubstratePreview(id)

    /**
     * The simulation gateway calls this only from its serialized IO worker while
     * holding native-world ownership. It is deliberately not part of the common repository.
     */
    internal fun substratePathOnSimulationWorker(id: StoredSubstrateId): PrivateSubstratePath =
        PrivateSubstratePath(store.substratePathOnSimulationWorker(id))

    /** See [substratePathOnSimulationWorker]. */
    internal fun saveSubstrateAtomicallyOnSimulationWorker(
        id: StoredSubstrateId,
        writer: (PrivateSubstratePath) -> Unit
    ) {
        store.saveSubstrateAtomicallyOnSimulationWorker(id) { path ->
            writer(PrivateSubstratePath(path))
        }
    }

    /** Resolves metadata only. Content is first opened by [importExternalFile]. */
    suspend fun resolveExternalFile(uri: Uri): StorageResult<ExternalCellularityFile> =
        store.execute {
            val sourceDisplayName = queryDisplayName(uri)
                ?: uri.lastPathSegment
                ?: throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "External file has no display name"
                )
            val name = store.externalFileName(sourceDisplayName)
            ExternalCellularityFile(
                displayName = name.displayName,
                safeFileName = name.safeFileName,
                kind = name.kind,
                sourceUri = uri,
                declaredSizeBytes = queryDeclaredSize(uri)
            )
        }

    suspend fun importExternalFile(file: ExternalCellularityFile): StorageResult<Unit> =
        store.importExternalFile(
            name = ExternalFileName(file.displayName, file.safeFileName, file.kind),
            declaredSizeBytes = file.declaredSizeBytes
        ) {
            context.contentResolver.openInputStream(file.sourceUri)
                ?: throw FileNotFoundException("External file is unavailable")
        }

    suspend fun shareUriForInternalFile(filename: String): StorageResult<Uri> =
        store.execute { checkCancellation ->
            val exportFile = store.exportCopy(
                filename = filename,
                exportDirectory = File(context.cacheDir, "exports"),
                checkCancellation = checkCancellation
            )
            try {
                FileProvider.getUriForFile(
                    context,
                    "${context.packageName}.fileprovider",
                    exportFile
                )
            } catch (failure: IllegalArgumentException) {
                throw StorageOperationException(
                    StorageErrorCode.IO_FAILURE,
                    failure.message ?: "Could not publish export file"
                )
            }
        }

    private fun queryDisplayName(uri: Uri): String? = context.contentResolver.query(
        uri,
        arrayOf(OpenableColumns.DISPLAY_NAME),
        null,
        null,
        null
    )?.use { cursor ->
        if (!cursor.moveToFirst()) return@use null
        val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (index >= 0) cursor.getString(index) else null
    }

    private fun queryDeclaredSize(uri: Uri): Long? = context.contentResolver.query(
        uri,
        arrayOf(OpenableColumns.SIZE),
        null,
        null,
        null
    )?.use { cursor ->
        if (!cursor.moveToFirst()) return@use null
        val index = cursor.getColumnIndex(OpenableColumns.SIZE)
        if (index >= 0 && !cursor.isNull(index)) {
            cursor.getLong(index).takeIf { it >= 0L }
        } else {
            null
        }
    }
}
