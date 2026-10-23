package com.cytarch.cellularity.platform.files

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.core.content.FileProvider
import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.preview.GenomePreviewCaptureCoordinator
import com.cytarch.cellularity.platform.simulation.NativeSubstrateInspection
import com.cytarch.cellularity.platform.simulation.NativeWireCodec
import com.cytarch.cellularity.shared.files.CellularityFileLimits
import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.GenomeFileInfo
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.SubstrateFileInfo
import com.cytarch.cellularity.shared.files.release.EmbeddedPreviewFormat
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.SimulationContract
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream

enum class ExternalCellularityFileKind {
    SUBSTRATE,
    GENOME
}

data class ExternalCellularityFile(
    val displayName: String,
    val safeFileName: String,
    val kind: ExternalCellularityFileKind,
    val sourceUri: Uri,
    val declaredSizeBytes: Long?
)

@JvmInline
internal value class PrivateSubstratePath(val value: String)

/** Android-owned file adapter. Common code receives IDs and metadata, never paths or payloads. */
class AndroidCellularityFileRepository internal constructor(
    context: Context,
    private val atomicFiles: AtomicFileStore,
    private val storageExecutor: AndroidStorageExecutor
) : CellularityFileRepository {
    constructor(context: Context) : this(
        context = context,
        atomicFiles = AtomicFileStore(),
        storageExecutor = AndroidStorageExecutor()
    )

    private val context = context.applicationContext ?: context
    private val directoryInitializationLock = Any()
    private val legacyFallbackConfiguration = SimulationContract.defaultConfiguration
    private val maximumSubstrateBytes =
        NativeCellularityControllerBridge.operationalSubstrateFileLimitBytes()
    private val embeddedPreviewCache = EmbeddedPreviewCache()

    @Volatile
    private var initializedPrivateDirectory: File? = null

    override suspend fun listSubstrateIds(): StorageResult<List<StoredSubstrateId>> =
        storageExecutor.execute {
            listFileNames()
                .filter(CellularityFileNames::isSubstrateFileName)
                .map(CellularityFileNames::substrateNameFromFileName)
                .map(::StoredSubstrateId)
        }

    override suspend fun readSubstrateInfo(
        id: StoredSubstrateId
    ): StorageResult<SubstrateFileInfo> = storageExecutor.execute {
        val file = substrateFileOnStorageThread(id)
        val inspection = inspectSubstrateFileOnStorageThread(file)
        SubstrateFileInfo(
            time = inspection.time / SECONDS_PER_HOUR,
            cellCount = inspection.cellCount,
            maximumCellCount = inspection.configuration.maxCellCount,
            foodCount = inspection.foodCount,
            maximumFoodCount = inspection.configuration.maxFoodCount,
            foodMass = inspection.foodMass,
            dishDiameterMillimeters = inspection.configuration.radius * 2.0,
            createdAtUnixMillis = inspection.createdAtUnixMillis
                ?: file.lastModified().takeIf { it > 0 },
            knownOrganismCount = inspection.knownOrganismCount,
            organismIdentityComplete = inspection.organismIdentityComplete,
            previewAvailable = inspection.preview?.usable == true,
            compatibility = if (inspection.loadSupported) {
                FileLoadCompatibility.LOADABLE
            } else {
                FileLoadCompatibility.NEWER_VERSION
            }
        )
    }

    override suspend fun substrateExists(name: String?): StorageResult<Boolean> =
        storageExecutor.execute {
            internalFile(CellularityFileNames.substrateFileName(name)).isFile
        }

    override suspend fun deleteSubstrate(id: StoredSubstrateId): StorageResult<Unit> =
        storageExecutor.execute {
            deleteInternalFile(CellularityFileNames.substrateFileName(id.value))
        }

    override suspend fun renameSubstrate(
        id: StoredSubstrateId,
        newDisplayName: String
    ): StorageResult<Unit> = storageExecutor.execute { checkCancellation ->
        renameInternalFile(
            oldFilename = CellularityFileNames.substrateFileName(id.value),
            newFilename = CellularityFileNames.substrateFileName(newDisplayName),
            checkCancellation = checkCancellation
        )
    }

    override suspend fun listGenomeIds(): StorageResult<List<StoredGenomeId>> =
        storageExecutor.execute {
            listFileNames()
                .filter(CellularityFileNames::isGenomeFileName)
                .map(::StoredGenomeId)
        }

    override suspend fun listGenomeNames(): StorageResult<List<String>> =
        storageExecutor.execute {
            listFileNames()
                .filter(CellularityFileNames::isGenomeFileName)
                .map(CellularityFileNames::genomeNameFromFileName)
        }

    override suspend fun readGenomeInfo(
        id: StoredGenomeId
    ): StorageResult<GenomeFileInfo> = storageExecutor.execute {
        val inspection = GenomeFileCodec.inspect(genomeFileOnStorageThread(id.fileName))
        val summary = inspection.releaseSummary
        GenomeFileInfo(
            displayName = id.defaultDisplayName,
            geneCount = summary?.geneCount,
            initialGeneIndex = summary?.initialGeneIndex,
            uniqueGeneCount = summary?.uniqueGeneCount,
            cellTypeMask = summary?.cellTypeMask,
            previewAvailable = summary?.preview?.usable == true,
            compatibility = if (inspection.loadSupported) {
                FileLoadCompatibility.LOADABLE
            } else {
                FileLoadCompatibility.NEWER_VERSION
            }
        )
    }

    internal suspend fun readGenomePreview(
        id: StoredGenomeId
    ): DecodedEmbeddedFilePreview? {
        val result = storageExecutor.execute {
            val file = genomeFileOnStorageThread(id.fileName)
            val summary = GenomeFileCodec.inspect(file).releaseSummary
                ?: return@execute null
            if (!summary.preview.usable) return@execute null
            val key = EmbeddedPreviewCacheKey(
                path = file.absolutePath,
                fileBytes = file.length(),
                modifiedMillis = file.lastModified(),
                format = summary.preview.format,
                pixelWidth = summary.preview.pixelWidth,
                pixelHeight = summary.preview.pixelHeight,
                byteOffset = summary.preview.byteOffset,
                byteCount = summary.preview.byteCount,
                encodedCrc32 = summary.preview.encodedCrc32
            )
            embeddedPreviewCache.getOrLoad(key) {
                GenomeFileCodec.readPreview(file, summary)
            }
        }
        return when (result) {
            is StorageResult.Success -> result.value
            is StorageResult.Failure -> null
        }
    }

    override suspend fun readGenomeDraft(
        nameOrFilename: String?
    ): StorageResult<GenomeDraft> = storageExecutor.execute {
        openGenomeInput(nameOrFilename).use { source ->
            GenomeFileCodec.read(source).genome.also(::validateGenomeDraft)
        }
    }

    override suspend fun writeGenomeDraft(
        nameOrFilename: String?,
        genome: GenomeDraft
    ): StorageResult<Unit> = writeGenomeDraftInternal(
        nameOrFilename = nameOrFilename,
        genome = genome,
        previewCapture = null,
        maximumPreviewTime = null
    )

    internal suspend fun writeGenomeDraftWithPreview(
        nameOrFilename: String?,
        genome: GenomeDraft,
        previewCapture: GenomePreviewCaptureCoordinator,
        maximumPreviewTime: Double
    ): StorageResult<Unit> = writeGenomeDraftInternal(
        nameOrFilename = nameOrFilename,
        genome = genome,
        previewCapture = previewCapture,
        maximumPreviewTime = maximumPreviewTime
    )

    private suspend fun writeGenomeDraftInternal(
        nameOrFilename: String?,
        genome: GenomeDraft,
        previewCapture: GenomePreviewCaptureCoordinator?,
        maximumPreviewTime: Double?
    ): StorageResult<Unit> = storageExecutor.execute { checkCancellation ->
        validateGenomeDraft(genome)
        val preview = previewCapture?.let { capture ->
            val timeLimit = requireNotNull(maximumPreviewTime) {
                "Genome preview time limit is required for capture"
            }
            try {
                val rendered = capture.capture(
                    genomeDraftBytes = GenomeDraftWireCodec.encode(genome),
                    maximumSimulationTime = timeLimit
                )
                rendered?.let {
                    GenomePreviewEncoder.encodePng(
                        rgbaBytes = requireNotNull(it.payload),
                        pixelWidth = it.width,
                        pixelHeight = it.height,
                        rowStrideBytes = it.rowStrideBytes
                    )
                }
            } catch (failure: Exception) {
                AppLog.error(
                    "Could not generate the saved genome preview; saving without it",
                    failure
                )
                null
            }
        }
        checkCancellation()
        val target = internalFile(CellularityFileNames.genomeFileName(nameOrFilename))
        atomicFiles.replace(
            target = target,
            maximumBytes = CellularityFileLimits.MAX_GENOME_SIZE_BYTES,
            checkCancellation = checkCancellation,
            produce = { temporaryFile ->
                FileOutputStream(temporaryFile).use { output ->
                    GenomeFileCodec.write(
                        LimitedOutputStream(output, CellularityFileLimits.MAX_GENOME_SIZE_BYTES),
                        genome,
                        preview
                    )
                }
            },
            validate = { temporaryFile ->
                validateGenomeFile(
                    temporaryFile,
                    requirePreview = preview != null
                )
            }
        )
    }

    override suspend fun deleteGenome(id: StoredGenomeId): StorageResult<Unit> =
        storageExecutor.execute {
            deleteInternalFile(id.fileName)
        }

    override suspend fun renameGenome(
        id: StoredGenomeId,
        newDisplayName: String
    ): StorageResult<Unit> = storageExecutor.execute { checkCancellation ->
        renameInternalFile(
            oldFilename = id.fileName,
            newFilename = CellularityFileNames.genomeFileName(newDisplayName),
            checkCancellation = checkCancellation
        )
    }

    /** Metadata inspection used before opening a retained simulation session. */
    internal suspend fun inspectStoredSubstrate(
        id: StoredSubstrateId
    ): StorageResult<NativeSubstrateInspection> = storageExecutor.execute {
        inspectStoredSubstrateOnStorageThread(id)
    }

    /** Reads only the CRC-checked encoded image for a visible Freezer row. */
    internal suspend fun readSubstratePreview(
        id: StoredSubstrateId
    ): DecodedEmbeddedFilePreview? {
        val result = storageExecutor.execute {
            val file = substrateFileOnStorageThread(id)
            val path = PrivateSubstratePath(file.absolutePath)
            val descriptor = inspectSubstrateFileOnStorageThread(file).preview
                ?: return@execute null
            if (!descriptor.usable) return@execute null
            val key = EmbeddedPreviewCacheKey(
                path = file.absolutePath,
                fileBytes = file.length(),
                modifiedMillis = file.lastModified(),
                format = descriptor.format,
                pixelWidth = descriptor.pixelWidth,
                pixelHeight = descriptor.pixelHeight,
                byteOffset = 0,
                byteCount = descriptor.byteCount,
                encodedCrc32 = descriptor.encodedCrc32
            )
            embeddedPreviewCache.getOrLoad(key) {
                val bytes = NativeCellularityControllerBridge.readSubstratePreviewPath(
                    path.value
                )
                if (bytes.isEmpty() || bytes.size.toLong() != descriptor.byteCount) {
                    null
                } else {
                    DecodedEmbeddedFilePreview(
                        format = descriptor.format,
                        pixelWidth = descriptor.pixelWidth,
                        pixelHeight = descriptor.pixelHeight,
                        bytes = bytes
                    )
                }
            }
        }
        return when (result) {
            is StorageResult.Success -> result.value
            is StorageResult.Failure -> null
        }
    }

    /**
     * The simulation gateway calls this only from its serialized IO worker while
     * holding native-world ownership. It is deliberately not part of the common repository.
     */
    internal fun substratePathOnSimulationWorker(id: StoredSubstrateId): PrivateSubstratePath =
        substratePathOnStorageThread(id)

    /** See [substratePathOnSimulationWorker]. */
    internal fun saveSubstrateAtomicallyOnSimulationWorker(
        id: StoredSubstrateId,
        writer: (PrivateSubstratePath) -> Unit
    ) {
        val target = internalFile(CellularityFileNames.substrateFileName(id.value))
        atomicFiles.replace(
            target = target,
            maximumBytes = maximumSubstrateBytes,
            produce = { temporaryFile ->
                writer(PrivateSubstratePath(temporaryFile.absolutePath))
            },
            validate = { temporaryFile ->
                NativeCellularityControllerBridge.validateSubstratePath(
                    temporaryFile.absolutePath,
                    false,
                    legacyFallbackConfiguration
                )
            }
        )
    }

    /** Resolves metadata only. Content is first opened by [importExternalFile]. */
    suspend fun resolveExternalFile(uri: Uri): StorageResult<ExternalCellularityFile> =
        storageExecutor.execute {
            val sourceDisplayName = queryDisplayName(uri)
                ?: uri.lastPathSegment
                ?: throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "External file has no display name"
                )
            val leafName = sourceDisplayName
                .substringAfterLast('/')
                .substringAfterLast('\\')
                .trim()
            if (!CellularityFileNames.isValidDisplayName(leafName)) {
                throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "External file has an invalid display name"
                )
            }

            val kind = if (leafName.endsWith(".genome", ignoreCase = true)) {
                ExternalCellularityFileKind.GENOME
            } else {
                ExternalCellularityFileKind.SUBSTRATE
            }
            val baseName = when (kind) {
                ExternalCellularityFileKind.SUBSTRATE ->
                    if (leafName.endsWith(".substrate", ignoreCase = true)) {
                        leafName.dropLast(".substrate".length)
                    } else {
                        leafName
                    }
                ExternalCellularityFileKind.GENOME -> leafName.dropLast(".genome".length)
            }
            if (!CellularityFileNames.isValidDisplayName(baseName)) {
                throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "External file has an invalid display name"
                )
            }
            val safeFileName = when (kind) {
                ExternalCellularityFileKind.SUBSTRATE ->
                    CellularityFileNames.substrateFileName(baseName)
                ExternalCellularityFileKind.GENOME ->
                    CellularityFileNames.genomeFileName(baseName)
            }
            ExternalCellularityFile(
                displayName = baseName,
                safeFileName = safeFileName,
                kind = kind,
                sourceUri = uri,
                declaredSizeBytes = queryDeclaredSize(uri)
            )
        }

    suspend fun importExternalFile(
        file: ExternalCellularityFile
    ): StorageResult<Unit> = storageExecutor.execute { checkCancellation ->
        val maximumBytes = maximumBytes(file.kind)
        requireDeclaredSizeWithinLimit(file.declaredSizeBytes, maximumBytes)

        val target = internalFile(file.safeFileName)
        if (target.exists()) throw DestinationExistsException()

        atomicFiles.replace(
            target = target,
            maximumBytes = maximumBytes,
            replaceExisting = false,
            checkCancellation = checkCancellation,
            produce = { temporaryFile ->
                val input = context.contentResolver.openInputStream(file.sourceUri)
                    ?: throw FileNotFoundException("External file is unavailable")
                BufferedInputStream(input).use { bufferedInput ->
                    FileOutputStream(temporaryFile).use { fileOutput ->
                        BufferedOutputStream(fileOutput).use { bufferedOutput ->
                            copyWithLimit(
                                bufferedInput,
                                bufferedOutput,
                                maximumBytes,
                                checkCancellation
                            )
                        }
                    }
                }
            },
            validate = { temporaryFile -> validateImportedFile(temporaryFile, file.kind) }
        )
    }

    suspend fun shareUriForInternalFile(filename: String): StorageResult<Uri> =
        storageExecutor.execute { checkCancellation ->
            require(
                File(filename).name == filename &&
                    (CellularityFileNames.isSubstrateFileName(filename) ||
                        CellularityFileNames.isGenomeFileName(filename))
            ) { "Only Cellularity data files can be shared" }
            val source = internalFile(filename)
            if (!source.isFile) throw FileNotFoundException("File does not exist")
            val maximumBytes = if (CellularityFileNames.isSubstrateFileName(filename)) {
                maximumSubstrateBytes
            } else {
                CellularityFileLimits.MAX_GENOME_SIZE_BYTES
            }
            if (source.length() > maximumBytes) throw FileSizeLimitExceededException()

            val exportDirectory = File(context.cacheDir, "exports").apply {
                if (!isDirectory && !mkdirs()) {
                    throw IOException("Could not create export directory")
                }
            }.canonicalFile
            atomicFiles.cleanupTemporaryFiles(exportDirectory)
            val exportFile = File(exportDirectory, filename)
            atomicFiles.replace(
                target = exportFile,
                maximumBytes = maximumBytes,
                checkCancellation = checkCancellation,
                produce = { temporaryFile ->
                    FileInputStream(source).use { input ->
                        FileOutputStream(temporaryFile).use { output ->
                            copyWithLimit(input, output, maximumBytes, checkCancellation)
                        }
                    }
                }
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

    private fun inspectStoredSubstrateOnStorageThread(
        id: StoredSubstrateId
    ): NativeSubstrateInspection = inspectSubstrateFileOnStorageThread(
        substrateFileOnStorageThread(id)
    )

    private fun inspectSubstrateFileOnStorageThread(
        file: File
    ): NativeSubstrateInspection = NativeWireCodec.decodeSubstrateInspection(
        NativeCellularityControllerBridge.inspectSubstratePath(
            file.absolutePath,
            false,
            legacyFallbackConfiguration
        )
    )

    private fun substratePathOnStorageThread(id: StoredSubstrateId): PrivateSubstratePath {
        return PrivateSubstratePath(substrateFileOnStorageThread(id).absolutePath)
    }

    private fun substrateFileOnStorageThread(id: StoredSubstrateId): File {
        val file = internalFile(CellularityFileNames.substrateFileName(id.value))
        if (!file.isFile) throw FileNotFoundException("Substrate does not exist")
        if (file.length() > maximumSubstrateBytes) {
            throw FileSizeLimitExceededException()
        }
        return file
    }

    private fun openGenomeInput(nameOrFilename: String?): InputStream {
        return BufferedInputStream(FileInputStream(genomeFileOnStorageThread(nameOrFilename)))
    }

    private fun genomeFileOnStorageThread(nameOrFilename: String?): File {
        val file = internalFile(CellularityFileNames.genomeFileName(nameOrFilename))
        if (!file.isFile) throw FileNotFoundException("Genome does not exist")
        if (file.length() > CellularityFileLimits.MAX_GENOME_SIZE_BYTES) {
            throw FileSizeLimitExceededException()
        }
        return file
    }

    private fun validateImportedFile(file: File, kind: ExternalCellularityFileKind) {
        when (kind) {
            ExternalCellularityFileKind.SUBSTRATE ->
                NativeCellularityControllerBridge.validateSubstratePath(
                    file.absolutePath,
                    false,
                    legacyFallbackConfiguration
                )
            ExternalCellularityFileKind.GENOME -> validateGenomeFile(file)
        }
    }

    private fun validateGenomeFile(file: File, requirePreview: Boolean = false) {
        if (file.length() > CellularityFileLimits.MAX_GENOME_SIZE_BYTES) {
            throw FileSizeLimitExceededException()
        }
        FileInputStream(file).use { input ->
            validateGenomeDraft(GenomeFileCodec.read(input).genome)
        }
        if (requirePreview) {
            val summary = GenomeFileCodec.inspect(file).releaseSummary
                ?: throw IOException("Saved genome preview metadata is missing")
            val decoded = GenomeFileCodec.readPreview(file, summary)
            if (
                summary.preview.format != EmbeddedPreviewFormat.PNG ||
                summary.preview.pixelWidth <= 0 ||
                summary.preview.pixelHeight <= 0 ||
                decoded == null
            ) {
                throw IOException("Saved genome preview is invalid")
            }
        }
    }

    private fun validateGenomeDraft(genome: GenomeDraft) {
        NativeCellularityControllerBridge.validateGenomeDraft(
            GenomeDraftWireCodec.encode(genome)
        )
    }

    private fun maximumBytes(kind: ExternalCellularityFileKind): Long = when (kind) {
        ExternalCellularityFileKind.SUBSTRATE -> maximumSubstrateBytes
        ExternalCellularityFileKind.GENOME -> CellularityFileLimits.MAX_GENOME_SIZE_BYTES
    }

    private fun renameInternalFile(
        oldFilename: String,
        newFilename: String,
        checkCancellation: () -> Unit
    ) {
        atomicFiles.rename(
            source = internalFile(oldFilename),
            target = internalFile(newFilename),
            replaceExisting = false,
            checkCancellation = checkCancellation
        )
    }

    private fun deleteInternalFile(filename: String) {
        val file = internalFile(filename)
        if (!file.isFile) throw FileNotFoundException("File does not exist")
        if (!file.delete()) throw IOException("Could not delete file")
        AndroidDurableFileOperations.syncDirectory(privateDirectory())
    }

    private fun listFileNames(): List<String> = privateDirectory()
        .listFiles()
        .orEmpty()
        .filter(File::isFile)
        .map(File::getName)
        .sortedWith(String.CASE_INSENSITIVE_ORDER)

    private fun internalFile(filename: String): File {
        require(
            File(filename).name == filename &&
                '/' !in filename && '\\' !in filename && '\u0000' !in filename
        ) { "File name must not contain a path" }
        val directory = privateDirectory()
        val candidate = File(directory, filename)
        require(candidate.parentFile?.canonicalFile == directory) {
            "File must stay in application private storage"
        }
        if (candidate.exists()) {
            require(candidate.canonicalFile.parentFile == directory) {
                "Symbolic links are not accepted as application data files"
            }
        }
        return candidate
    }

    private fun privateDirectory(): File {
        initializedPrivateDirectory?.let { return it }
        return synchronized(directoryInitializationLock) {
            initializedPrivateDirectory ?: context.filesDir.canonicalFile.also { directory ->
                check(directory.isDirectory) { "Application files directory is unavailable" }
                atomicFiles.cleanupTemporaryFiles(directory)
                initializedPrivateDirectory = directory
            }
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

private const val SECONDS_PER_HOUR = 3600.0

private data class EmbeddedPreviewCacheKey(
    val path: String,
    val fileBytes: Long,
    val modifiedMillis: Long,
    val format: EmbeddedPreviewFormat,
    val pixelWidth: Int,
    val pixelHeight: Int,
    val byteOffset: Long,
    val byteCount: Long,
    val encodedCrc32: Long
)

/** Small encoded-image cache; decoded bitmaps remain owned by visible Compose rows. */
private class EmbeddedPreviewCache {
    private val lock = Any()
    private val entries = LinkedHashMap<
        EmbeddedPreviewCacheKey,
        DecodedEmbeddedFilePreview?
    >(16, 0.75f, true)
    private var cachedBytes = 0L

    fun getOrLoad(
        key: EmbeddedPreviewCacheKey,
        load: () -> DecodedEmbeddedFilePreview?
    ): DecodedEmbeddedFilePreview? {
        synchronized(lock) {
            if (entries.containsKey(key)) return entries[key]
        }

        val loaded = load()
        synchronized(lock) {
            if (entries.containsKey(key)) return entries[key]
            entries[key] = loaded
            cachedBytes += loaded?.bytes?.size ?: 0
            trim()
            return loaded
        }
    }

    private fun trim() {
        val iterator = entries.entries.iterator()
        while (
            iterator.hasNext() &&
            (entries.size > MAX_PREVIEW_CACHE_ENTRIES ||
                cachedBytes > MAX_PREVIEW_CACHE_BYTES)
        ) {
            val entry = iterator.next()
            cachedBytes -= entry.value?.bytes?.size ?: 0
            iterator.remove()
        }
    }

    private companion object {
        const val MAX_PREVIEW_CACHE_ENTRIES = 32
        const val MAX_PREVIEW_CACHE_BYTES = 16L * 1024L * 1024L
    }
}
