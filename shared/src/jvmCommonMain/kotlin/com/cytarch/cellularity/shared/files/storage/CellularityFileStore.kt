/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.files.storage

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
import com.cytarch.cellularity.shared.files.genome.DecodedEmbeddedFilePreview
import com.cytarch.cellularity.shared.files.genome.EncodedGenomePreview
import com.cytarch.cellularity.shared.files.genome.GenomeFileCodec
import com.cytarch.cellularity.shared.files.genome.ReleaseGenomeCodecBridge
import com.cytarch.cellularity.shared.files.release.EmbeddedPreviewFormat
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.nativewire.NativeSubstrateInspection
import com.cytarch.cellularity.shared.nativewire.NativeWireCodec
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream

/**
 * The native file operations both platforms' JNI bridges expose. They are the same C++
 * functions; only the JNI class they are reached through differs.
 */
interface CellularityNativeFiles {
    val releaseGenomeCodec: ReleaseGenomeCodecBridge
    fun validateGenomeDraft(genomeDraftWire: ByteArray)
    fun operationalSubstrateFileLimitBytes(): Long
    fun inspectSubstratePath(
        path: String,
        previewMode: Boolean,
        fallback: SimulationConfiguration
    ): ByteArray
    fun validateSubstratePath(
        path: String,
        previewMode: Boolean,
        fallback: SimulationConfiguration
    )
    fun readSubstratePreviewPath(path: String): ByteArray
}

enum class ExternalCellularityFileKind {
    SUBSTRATE,
    GENOME
}

/** What an external file's name says it is, decided by the rules both platforms share. */
data class ExternalFileName(
    val displayName: String,
    val safeFileName: String,
    val kind: ExternalCellularityFileKind
)

/**
 * The application's private document store: substrates and genomes in one directory.
 *
 * One implementation for every platform: naming, limits, durable atomic replacement, codecs,
 * validation and the preview cache. A platform supplies only the directory, its native bridge, its durable
 * file primitives and -- outside this class -- how an external file is opened or shared.
 */
class CellularityFileStore(
    private val directoryProvider: () -> File,
    private val native: CellularityNativeFiles,
    private val atomicFiles: AtomicFileStore,
    private val storageExecutor: StorageExecutor = StorageExecutor(),
    private val durableFiles: DurableFileOperations
) : CellularityFileRepository {

    private val directoryInitializationLock = Any()
    private val legacyFallbackConfiguration = SimulationContract.defaultConfiguration
    private val embeddedPreviewCache = EmbeddedPreviewCache()

    val maximumSubstrateBytes: Long by lazy { native.operationalSubstrateFileLimitBytes() }

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
        val file = substrateFile(id)
        val inspection = inspectSubstrateFile(file)
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
        val inspection = GenomeFileCodec.inspect(genomeFile(id.fileName))
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

    suspend fun readGenomePreview(id: StoredGenomeId): DecodedEmbeddedFilePreview? {
        val result = storageExecutor.execute {
            val file = genomeFile(id.fileName)
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
            GenomeFileCodec.read(source, native.releaseGenomeCodec).genome
                .also(::validateGenomeDraft)
        }
    }

    override suspend fun writeGenomeDraft(
        nameOrFilename: String?,
        genome: GenomeDraft
    ): StorageResult<Unit> = writeGenomeDraftWithPreview(nameOrFilename, genome) { null }

    /**
     * Writes a genome with the picture [preview] renders, or none when it returns null.
     * Rendering is the platform's (it owns the preview surface); a failure to render is
     * the platform's to log and turns into a genome saved without a picture.
     */
    suspend fun writeGenomeDraftWithPreview(
        nameOrFilename: String?,
        genome: GenomeDraft,
        preview: (genomeDraftWire: ByteArray) -> EncodedGenomePreview?
    ): StorageResult<Unit> = storageExecutor.execute { checkCancellation ->
        validateGenomeDraft(genome)
        val encodedPreview = preview(GenomeDraftWireCodec.encode(genome))
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
                        encodedPreview,
                        native.releaseGenomeCodec
                    )
                }
            },
            validate = { temporaryFile ->
                validateGenomeFile(temporaryFile, requirePreview = encodedPreview != null)
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
    suspend fun inspectStoredSubstrate(
        id: StoredSubstrateId
    ): StorageResult<NativeSubstrateInspection> = storageExecutor.execute {
        inspectSubstrateFile(substrateFile(id))
    }

    /** Reads only the CRC-checked encoded image for a visible collection row. */
    suspend fun readSubstratePreview(id: StoredSubstrateId): DecodedEmbeddedFilePreview? {
        val result = storageExecutor.execute {
            val file = substrateFile(id)
            val descriptor = inspectSubstrateFile(file).preview
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
                val bytes = native.readSubstratePreviewPath(file.absolutePath)
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
     * The path of a stored substrate, for the simulation to open. Called only from the
     * simulation's serialized worker while it holds native-world ownership.
     */
    fun substratePathOnSimulationWorker(id: StoredSubstrateId): String =
        substrateFile(id).absolutePath

    /** Atomically replaces a stored substrate with what [writer] writes to the given path. */
    fun saveSubstrateAtomicallyOnSimulationWorker(
        id: StoredSubstrateId,
        replaceExisting: Boolean = true,
        writer: (temporaryPath: String) -> Unit
    ) {
        val target = internalFile(CellularityFileNames.substrateFileName(id.value))
        if (!replaceExisting && target.exists()) throw DestinationExistsException()
        atomicFiles.replace(
            target = target,
            maximumBytes = maximumSubstrateBytes,
            replaceExisting = replaceExisting,
            produce = { temporaryFile -> writer(temporaryFile.absolutePath) },
            validate = { temporaryFile ->
                native.validateSubstratePath(
                    temporaryFile.absolutePath,
                    false,
                    legacyFallbackConfiguration
                )
            }
        )
    }

    /** Applies the shared naming rules to an external file's leaf name. */
    fun externalFileName(sourceDisplayName: String): ExternalFileName {
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
        return ExternalFileName(baseName, safeFileName, kind)
    }

    /**
     * Copies an external file into the store under [name], refusing an existing name. The
     * copy is validated in place before it is given its name.
     */
    suspend fun importExternalFile(
        name: ExternalFileName,
        declaredSizeBytes: Long?,
        open: () -> InputStream
    ): StorageResult<Unit> = storageExecutor.execute { checkCancellation ->
        val maximumBytes = maximumBytes(name.kind)
        requireDeclaredSizeWithinLimit(declaredSizeBytes, maximumBytes)

        val target = internalFile(name.safeFileName)
        if (target.exists()) throw DestinationExistsException()

        atomicFiles.replace(
            target = target,
            maximumBytes = maximumBytes,
            replaceExisting = false,
            checkCancellation = checkCancellation,
            produce = { temporaryFile ->
                BufferedInputStream(open()).use { bufferedInput ->
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
            validate = { temporaryFile -> validateImportedFile(temporaryFile, name.kind) }
        )
    }

    /** Copies a stored file into [exportDirectory] for the platform to share. */
    fun exportCopy(
        filename: String,
        exportDirectory: File,
        checkCancellation: () -> Unit
    ): File {
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

        val directory = exportDirectory.apply {
            if (!isDirectory && !mkdirs()) {
                throw IOException("Could not create export directory")
            }
        }.canonicalFile
        atomicFiles.cleanupTemporaryFiles(directory)
        val exportFile = File(directory, filename)
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
        return exportFile
    }

    /** Runs [operation] on the storage executor, for platform-side extensions. */
    suspend fun <T> execute(operation: (checkCancellation: () -> Unit) -> T): StorageResult<T> =
        storageExecutor.execute(operation)

    private fun inspectSubstrateFile(file: File): NativeSubstrateInspection =
        NativeWireCodec.decodeSubstrateInspection(
            native.inspectSubstratePath(file.absolutePath, false, legacyFallbackConfiguration)
        )

    private fun substrateFile(id: StoredSubstrateId): File {
        val file = internalFile(CellularityFileNames.substrateFileName(id.value))
        if (!file.isFile) throw FileNotFoundException("Substrate does not exist")
        if (file.length() > maximumSubstrateBytes) {
            throw FileSizeLimitExceededException()
        }
        return file
    }

    private fun openGenomeInput(nameOrFilename: String?): InputStream =
        BufferedInputStream(FileInputStream(genomeFile(nameOrFilename)))

    private fun genomeFile(nameOrFilename: String?): File {
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
                native.validateSubstratePath(file.absolutePath, false, legacyFallbackConfiguration)
            ExternalCellularityFileKind.GENOME -> validateGenomeFile(file)
        }
    }

    private fun validateGenomeFile(file: File, requirePreview: Boolean = false) {
        if (file.length() > CellularityFileLimits.MAX_GENOME_SIZE_BYTES) {
            throw FileSizeLimitExceededException()
        }
        FileInputStream(file).use { input ->
            validateGenomeDraft(GenomeFileCodec.read(input, native.releaseGenomeCodec).genome)
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
        native.validateGenomeDraft(GenomeDraftWireCodec.encode(genome))
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
        durableFiles.syncDirectory(privateDirectory())
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
            initializedPrivateDirectory ?: directoryProvider().canonicalFile.also { directory ->
                check(directory.isDirectory) { "Application files directory is unavailable" }
                atomicFiles.cleanupTemporaryFiles(directory)
                initializedPrivateDirectory = directory
            }
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

/** Small encoded-image cache; decoded bitmaps remain owned by visible rows. */
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
