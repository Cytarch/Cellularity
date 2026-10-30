/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.files

import com.cytarch.cellularity.shared.files.CellularityFileLimits
import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.GenomeFileInfo
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.SubstrateFileInfo
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeFileInspector
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeFileSummary
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeRangeReader
import com.cytarch.cellularity.shared.files.release.SaveContainerFormatException
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.GenomeDraftWireFormatException
import com.cytarch.cellularity.shared.model.GenomeInitialGeneCompatibility
import com.cytarch.cellularity.shared.model.SubstrateInspection
import com.cytarch.cellularity.shared.model.SubstrateInspectionWireCodec
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import java.io.FileNotFoundException
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.channels.FileChannel
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.NoSuchFileException
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/**
 * JVM storage for the desktop launcher.
 *
 * The Android adapter delegates four things this module cannot reach: the native
 * substrate inspector, the native release-genome codec, the Java legacy genome reader,
 * and the application-private directory it owns. What remains is the file-system
 * contract itself, rewritten against java.nio against a caller-supplied directory, so
 * the desktop build can list, inspect, read, write, rename and delete the same files
 * the common UI asks for.
 *
 * Two consequences are worth stating plainly, because they change what this class can
 * honestly report rather than what it can store:
 *
 *  - CLTY containers can be recognised and described, but not decoded. Metadata
 *    inspection is public shared code and is used in full; reading the body needs the
 *    native runtime, which the desktop module does not link. A CLTY genome therefore
 *    reports its real gene count and compatibility and fails at load time with an
 *    explicit unsupported-format error instead of a fabricated genome.
 *  - Drafts written here use the shared draft wire format, which is the format the
 *    native runtime already accepts and the only genome encoder available on this side
 *    of the JNI boundary. A draft written by this class is readable by this class and by
 *    the native runtime, but not by the Android adapter, which only accepts CLTY and the
 *    frozen Java legacy stream.
 *
 * Every write goes through a temporary file in the same directory followed by a move, so
 * an interrupted write leaves the previous file intact; naming is validated before any
 * path is built so a crafted display name cannot reach outside the storage directory.
 */
class DesktopCellularityFileRepository(
    /** Directory that owns every file this repository can see. */
    storageDirectory: Path,
    /**
     * Overrides the genome byte limit. Tests use a smaller value; production code leaves
     * it at the shared limit so the desktop adapter accepts exactly what Android accepts.
     */
    private val maximumGenomeBytes: Long = CellularityFileLimits.MAX_GENOME_SIZE_BYTES
) : CellularityFileRepository {

    private val requestedDirectory = storageDirectory.toAbsolutePath()
    private val initializationLock = Any()

    @Volatile
    private var initializedDirectory: Path? = null

    override suspend fun listSubstrateIds(): StorageResult<List<StoredSubstrateId>> =
        storageOperation { checkCancellation ->
            listFileNames()
                .filter(CellularityFileNames::isSubstrateFileName)
                .map(CellularityFileNames::substrateNameFromFileName)
                .map(::StoredSubstrateId)
                .also { checkCancellation() }
        }

    override suspend fun readSubstrateInfo(
        id: StoredSubstrateId
    ): StorageResult<SubstrateFileInfo> = storageOperation {
        val file = resolve(CellularityFileNames.substrateFileName(id.value))
        if (!Files.isRegularFile(file)) {
            throw StorageOperationException(
                StorageErrorCode.NOT_FOUND,
                "Substrate file does not exist"
            )
        }
        // No size ceiling is enforced here. The native operational limit that bounds
        // substrates on Android is a bridge constant in the app module, and this adapter
        // never loads a substrate into memory: it reads fixed offset ranges only.
        when (val classification = substrateFileKind(file)) {
            is SubstrateFileKind.Unknown -> throw StorageOperationException(
                StorageErrorCode.UNSUPPORTED_FORMAT,
                "Substrate file is not a release container"
            )
            is SubstrateFileKind.Genome -> throw StorageOperationException(
                StorageErrorCode.UNSUPPORTED_FORMAT,
                "File contains a standalone genome, not a substrate"
            )
            is SubstrateFileKind.Container -> {
                // What is inside the plate comes from the codec that can read it -- the same
                // inspection the phone's list uses, decoded by the codec the two platforms share.
                // Without it the freezer showed zero cells and no time for a plate holding
                // thousands: honest about not knowing, and useless about the plate.
                val inspection = inspectSubstrate(file, classification)
                SubstrateFileInfo(
                    time = inspection?.time?.div(SECONDS_PER_HOUR) ?: 0.0,
                    cellCount = inspection?.cellCount ?: 0,
                    maximumCellCount = inspection?.configuration?.maxCellCount,
                    foodCount = inspection?.foodCount,
                    maximumFoodCount = inspection?.configuration?.maxFoodCount,
                    foodMass = inspection?.foodMass,
                    dishDiameterMillimeters = inspection?.configuration?.radius?.times(2.0),
                    createdAtUnixMillis = inspection?.createdAtUnixMillis
                        ?: fileCreationMillis(file),
                    knownOrganismCount = inspection?.knownOrganismCount,
                    organismIdentityComplete = inspection?.organismIdentityComplete ?: false,
                    previewAvailable = inspection?.preview?.usable == true,
                    compatibility = when {
                        inspection == null -> classification.compatibility
                        inspection.loadSupported -> FileLoadCompatibility.LOADABLE
                        else -> FileLoadCompatibility.NEWER_VERSION
                    }
                )
            }
        }
    }

    /**
     * The plate's own account of itself, or null when the codec would not describe it.
     *
     * Null is a real answer and not a failure: a file this class has already classified as a
     * container can still be one the decoder refuses -- a newer schema, a truncated body -- and the
     * card then shows what it can justify (the file exists, when it was written) rather than numbers
     * nobody can account for.
     */
    private fun inspectSubstrate(
        file: Path,
        classification: SubstrateFileKind.Container
    ): SubstrateInspection? {
        // The allocation is the fallback the decoder needs for a plate that predates recording its
        // own, and it is the same one Android passes: the default configuration. Passing the plate's
        // own would be circular -- reading it is what this call is for.
        val fallback = SimulationContract.defaultConfiguration
        val bytes = DesktopNativeRenderer.nativeInspectSubstrate(
            substratePath = file.toString(),
            maxCellCount = fallback.maxCellCount,
            maxFoodCount = fallback.maxFoodCount,
            dishRadius = fallback.radius
        )
        return SubstrateInspectionWireCodec.decodeOrNull(bytes)
    }

    override suspend fun substrateExists(name: String?): StorageResult<Boolean> =
        storageOperation {
            val file = resolve(CellularityFileNames.substrateFileName(name))
            Files.isRegularFile(file)
        }

    override suspend fun deleteSubstrate(id: StoredSubstrateId): StorageResult<Unit> =
        storageOperation { checkCancellation ->
            val file = resolve(CellularityFileNames.substrateFileName(id.value))
            if (!Files.isRegularFile(file)) {
                throw StorageOperationException(
                    StorageErrorCode.NOT_FOUND,
                    "Substrate file does not exist"
                )
            }
            checkCancellation()
            deleteFile(file)
        }

    /**
     * Where a stored substrate actually is, for the one caller that opens a file itself.
     *
     * The native renderer decodes a plate from a path, so opening a saved experiment needs
     * the absolute path and not the bytes: handing over the file would mean reading a
     * substrate into the heap only for the renderer to stream it back out. That is also why
     * this is not part of the shared contract -- a phone has no path to give, because its
     * storage is a content provider rather than a directory.
     *
     * The path is produced by the same resolution the reading and writing paths use, so a
     * name that would escape the storage directory is refused here for the same reason it
     * is refused there, and the refusal is a storage failure like any other rather than a
     * thrown exception the caller has to know about.
     */
    suspend fun substrateFilePath(id: StoredSubstrateId): StorageResult<Path> =
        storageOperation {
            val file = resolve(CellularityFileNames.substrateFileName(id.value))
            if (!Files.isRegularFile(file)) {
                throw StorageOperationException(
                    StorageErrorCode.NOT_FOUND,
                    "Substrate file does not exist"
                )
            }
            file
        }

    override suspend fun renameSubstrate(
        id: StoredSubstrateId,
        newDisplayName: String
    ): StorageResult<Unit> = storageOperation { checkCancellation ->
        renameFile(
            source = resolve(CellularityFileNames.substrateFileName(id.value)),
            target = resolve(CellularityFileNames.substrateFileName(newDisplayName)),
            checkCancellation = checkCancellation
        )
    }

    override suspend fun listGenomeIds(): StorageResult<List<StoredGenomeId>> =
        storageOperation { checkCancellation ->
            listFileNames()
                .filter(CellularityFileNames::isGenomeFileName)
                .map(::StoredGenomeId)
                .also { checkCancellation() }
        }

    override suspend fun listGenomeNames(): StorageResult<List<String>> =
        storageOperation { checkCancellation ->
            listFileNames()
                .filter(CellularityFileNames::isGenomeFileName)
                .map(CellularityFileNames::genomeNameFromFileName)
                .also { checkCancellation() }
        }

    override suspend fun readGenomeInfo(
        id: StoredGenomeId
    ): StorageResult<GenomeFileInfo> = storageOperation {
        val file = resolve(id.fileName)
        if (!Files.isRegularFile(file)) {
            throw StorageOperationException(
                StorageErrorCode.NOT_FOUND,
                "Genome file does not exist"
            )
        }
        val fileBytes = Files.size(file)
        if (fileBytes > maximumGenomeBytes) {
            throw StorageOperationException(
                StorageErrorCode.TOO_LARGE,
                "Genome exceeds the release file size limit"
            )
        }
        val displayName = id.defaultDisplayName
        when (fileKind(file)) {
            FileKind.ReleaseContainer -> {
                val summary = inspectReleaseGenome(file)
                GenomeFileInfo(
                    displayName = displayName,
                    geneCount = summary.geneCount,
                    initialGeneIndex = summary.initialGeneIndex,
                    uniqueGeneCount = summary.uniqueGeneCount,
                    cellTypeMask = summary.cellTypeMask,
                    // The summary's preview descriptor is a claim about the container,
                    // not proof that this adapter can hand the image to the UI. Only
                    // Android can read the encoded range through the native bridge and
                    // CRC-check it, so availability stays false here.
                    previewAvailable = false,
                    compatibility = if (summary.bodySupported) {
                        FileLoadCompatibility.LOADABLE
                    } else {
                        // Mirrors the release inspector: a newer content schema is
                        // legible as metadata even though its body cannot be interpreted.
                        FileLoadCompatibility.NEWER_VERSION
                    }
                )
            }
            FileKind.DraftWire -> {
                val genome = decodeDraftFile(file, fileBytes)
                GenomeFileInfo(
                    displayName = displayName,
                    geneCount = genome.genes.size,
                    initialGeneIndex = GenomeInitialGeneCompatibility
                        .extract(genome)
                        ?.initialGeneIndex,
                    // Canonical deduplication is owned by the release encoder, so the
                    // draft row count cannot be reduced here. One unique gene per gene is
                    // an upper bound that keeps the row readable; the real count appears
                    // once the file has been through the release codec.
                    uniqueGeneCount = genome.genes.size,
                    cellTypeMask = genome.cellTypeMask(),
                    previewAvailable = false,
                    compatibility = FileLoadCompatibility.LOADABLE
                )
            }
            FileKind.Unrecognised -> throw StorageOperationException(
                StorageErrorCode.UNSUPPORTED_FORMAT,
                "File is not a genome the desktop adapter can read"
            )
        }
    }

    override suspend fun readGenomeDraft(
        nameOrFilename: String?
    ): StorageResult<GenomeDraft> = storageOperation {
        val file = resolve(CellularityFileNames.genomeFileName(nameOrFilename))
        if (!Files.isRegularFile(file)) {
            throw StorageOperationException(
                StorageErrorCode.NOT_FOUND,
                "Genome file does not exist"
            )
        }
        val fileBytes = Files.size(file)
        if (fileBytes > maximumGenomeBytes) {
            throw StorageOperationException(
                StorageErrorCode.TOO_LARGE,
                "Genome exceeds the release file size limit"
            )
        }
        when (fileKind(file)) {
            FileKind.DraftWire -> decodeDraftFile(file, fileBytes).requireLoadable()
            FileKind.ReleaseContainer -> throw StorageOperationException(
                StorageErrorCode.UNSUPPORTED_FORMAT,
                "Reading a release genome body needs the native codec, which the " +
                    "desktop adapter does not link"
            )
            FileKind.Unrecognised -> throw StorageOperationException(
                StorageErrorCode.UNSUPPORTED_FORMAT,
                "File is not a genome the desktop adapter can read"
            )
        }
    }

    override suspend fun writeGenomeDraft(
        nameOrFilename: String?,
        genome: GenomeDraft
    ): StorageResult<Unit> = storageOperation { checkCancellation ->
        // Validated before anything is written. The release file records a single
        // initial gene index, so a draft with zero or several initial genes could be
        // stored but never loaded again; refusing it here keeps a save from producing a
        // file the gene bank would list as readable and then fail to open.
        genome.requireValidDraft()
        val encoded = try {
            GenomeDraftWireCodec.encode(genome)
        } catch (failure: IllegalArgumentException) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                failure.message ?: "Genome draft cannot be encoded"
            )
        }
        if (encoded.size.toLong() > maximumGenomeBytes) {
            throw StorageOperationException(
                StorageErrorCode.TOO_LARGE,
                "Genome exceeds the release file size limit"
            )
        }
        checkCancellation()
        val target = resolve(CellularityFileNames.genomeFileName(nameOrFilename))
        atomicReplace(
            target = target,
            declaredBytes = encoded.size.toLong(),
            checkCancellation = checkCancellation
        ) { temporary ->
            Files.write(
                temporary,
                encoded,
                StandardOpenOption.WRITE,
                StandardOpenOption.TRUNCATE_EXISTING
            )
        }
        checkCancellation()
        Unit
    }

    override suspend fun deleteGenome(id: StoredGenomeId): StorageResult<Unit> =
        storageOperation { checkCancellation ->
            val file = resolve(id.fileName)
            if (!Files.isRegularFile(file)) {
                throw StorageOperationException(
                    StorageErrorCode.NOT_FOUND,
                    "Genome file does not exist"
                )
            }
            checkCancellation()
            deleteFile(file)
        }

    override suspend fun renameGenome(
        id: StoredGenomeId,
        newDisplayName: String
    ): StorageResult<Unit> = storageOperation { checkCancellation ->
        renameFile(
            source = resolve(id.fileName),
            target = resolve(CellularityFileNames.genomeFileName(newDisplayName)),
            checkCancellation = checkCancellation
        )
    }

    /**
     * Reads the name and the kind of a file from outside the storage directory.
     *
     * Metadata only: the bytes are read by [importExternalFile], after the name has been accepted.
     * This is the desktop's half of what the phone asks its content resolver, and it follows the
     * phone's rules rather than inventing any: the extension decides the kind -- a name that ends
     * in `.genome` is a genome and everything else is a plate -- the display name is the file's own
     * leaf name without that extension, and the stored name is built by the same helpers every
     * other storage operation uses.
     *
     * A file that is not there, or whose name cannot be a name in the storage directory, is
     * refused here rather than at the end of a copy.
     */
    suspend fun resolveExternalFile(source: Path): StorageResult<DesktopExternalFile> =
        storageOperation {
            if (!Files.isRegularFile(source)) {
                throw StorageOperationException(
                    StorageErrorCode.NOT_FOUND,
                    "There is no file at that location"
                )
            }
            val leafName = source.fileName?.toString()?.trim().orEmpty()
            if (!CellularityFileNames.isValidDisplayName(leafName)) {
                throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "External file has an invalid display name"
                )
            }

            val kind = if (leafName.endsWith(GENOME_EXTENSION, ignoreCase = true)) {
                DesktopExternalFileKind.GENOME
            } else {
                DesktopExternalFileKind.SUBSTRATE
            }
            val baseName = when (kind) {
                DesktopExternalFileKind.SUBSTRATE ->
                    if (leafName.endsWith(SUBSTRATE_EXTENSION, ignoreCase = true)) {
                        leafName.dropLast(SUBSTRATE_EXTENSION.length)
                    } else {
                        leafName
                    }

                DesktopExternalFileKind.GENOME -> leafName.dropLast(GENOME_EXTENSION.length)
            }
            // A file called ".genome" has no name left once the extension is taken off, and a name
            // that is empty or path-like cannot be stored. Checked before the name helpers are
            // asked to build anything, so the refusal says which file is wrong.
            if (!CellularityFileNames.isValidDisplayName(baseName)) {
                throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "External file has an invalid display name"
                )
            }

            DesktopExternalFile(
                source = source,
                displayName = baseName,
                fileName = when (kind) {
                    DesktopExternalFileKind.SUBSTRATE ->
                        CellularityFileNames.substrateFileName(baseName)

                    DesktopExternalFileKind.GENOME ->
                        CellularityFileNames.genomeFileName(baseName)
                },
                kind = kind
            )
        }

    /**
     * Copies a file from outside the storage directory into it, under its own name.
     *
     * Nothing is replaced. A file that is already there under that name is reported as such, which
     * is what the phone does and what the rename here does: an import that overwrote a plate would
     * destroy the one file the user was trying to keep, and the report would look like success.
     *
     * The bytes are checked before the copy rather than after it. The phone validates the temporary
     * file because reading from a content provider can fail halfway; a file on a disk is read by
     * position, and a copy that ends short is caught by the same length check every other write
     * goes through, so what is worth refusing up front is a document that is not what its name
     * claims -- a text file called `.substrate`, a genome of a kind this adapter cannot recognise.
     *
     * A substrate is copied without a size ceiling. The limit the phone enforces is its own native
     * loader's, because it reads the whole plate into memory; this side streams the file and hands
     * the renderer a path, so a ceiling here would refuse plates that the desktop can open.
     */
    suspend fun importExternalFile(file: DesktopExternalFile): StorageResult<Unit> =
        storageOperation { checkCancellation ->
            val target = resolve(file.fileName)
            if (Files.isRegularFile(target)) {
                throw StorageOperationException(
                    StorageErrorCode.NAME_EXISTS,
                    "A file with that name already exists"
                )
            }
            validateImportedFile(file)
            checkCancellation()
            atomicReplace(
                target = target,
                declaredBytes = Files.size(file.source),
                checkCancellation = checkCancellation,
                replaceExisting = false
            ) { temporary ->
                Files.copy(file.source, temporary, StandardCopyOption.REPLACE_EXISTING)
            }
            checkCancellation()
            Unit
        }

    /**
     * The picture a stored plate carries, or null when it has none.
     *
     * The bytes are the container's own encoded preview rather than a re-encoding: what the card
     * shows is what a reader of the file would get. Null covers both "this plate was saved without
     * a picture" and "the picture could not be read", because the card draws the same placeholder
     * for either -- and a plate is not less of a plate for having no picture.
     */
    suspend fun readSubstratePreview(id: StoredSubstrateId): StorageResult<ByteArray?> =
        storageOperation {
            val file = resolve(CellularityFileNames.substrateFileName(id.value))
            if (!Files.isRegularFile(file)) {
                throw StorageOperationException(
                    StorageErrorCode.NOT_FOUND,
                    "Substrate file does not exist"
                )
            }
            DesktopNativeRenderer.nativeReadSubstratePreview(file.toString())
        }

    /**
     * Writes the running plate into the collection under [id].
     *
     * The bytes come from [produce], which is the native writer: it is handed the temporary path and
     * writes the plate there, because the world belongs to the simulation's own thread and a plate
     * has to be written from the world that is being played rather than from a copy of it. What this
     * method owns is the part the world cannot know about -- where the file belongs, that it must
     * not appear half-written, and [replaceExisting], which is true only when the plate being saved
     * is the one already open under that name.
     *
     * The size is not declared, because the writer decides it; what is checked instead is that the
     * file it produced is a plate, by the container's own classification. A writer that stopped
     * halfway leaves something the classifier does not recognise, and that is exactly what must not
     * be moved into the collection under a name a person can see.
     */
    suspend fun saveSubstrate(
        id: StoredSubstrateId,
        replaceExisting: Boolean,
        produce: suspend (temporary: Path) -> Unit
    ): StorageResult<Unit> = storageOperation { checkCancellation ->
        val target = resolve(CellularityFileNames.substrateFileName(id.value))
        // Refused before the world is written, when it is going to be refused anyway: a name that is
        // taken is knowable now, and the writer is the slow part. The move refuses it again, which is
        // what covers the interval between this check and the file appearing.
        if (!replaceExisting && Files.isRegularFile(target)) {
            throw StorageOperationException(
                StorageErrorCode.NAME_EXISTS,
                "A file with that name already exists"
            )
        }
        atomicReplace(
            target = target,
            declaredBytes = null,
            checkCancellation = checkCancellation,
            replaceExisting = replaceExisting,
            validate = ::requireWrittenPlate,
            produce = produce
        )
        checkCancellation()
        Unit
    }

    /**
     * Refuses a written file that is not a plate.
     *
     * Asked of the file itself rather than of the writer: the writer is native code that reported
     * success, and this is the last place where a claim about the file can still be checked against
     * the file. It is the same question the collection asks when it lists a plate, so a save cannot
     * commit something the list would then call unreadable.
     */
    private fun requireWrittenPlate(temporary: Path) {
        when (substrateFileKind(temporary)) {
            is SubstrateFileKind.Container -> Unit
            is SubstrateFileKind.Genome -> throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "The writer produced a standalone genome instead of a plate"
            )
            is SubstrateFileKind.Unknown -> throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "The writer produced a file that is not a plate"
            )
        }
    }

    /**
     * Refuses a file that is not the kind of document its name claims to be.
     *
     * This is as far as validation reaches on this side of the JNI boundary. The phone asks the
     * native codecs to verify a genome in full and to read a release container's metadata; the
     * desktop adapter links neither, so what it can honestly do is recognise the layouts it knows
     * by their own markers and refuse what it cannot place at all. A draft genome is decoded, since
     * that is the format this side both writes and reads, and a release container is accepted
     * because the collection that lists it reports its compatibility -- and its refusal, if it
     * cannot be opened -- from the same inspection that runs for a file that was already there.
     */
    private fun validateImportedFile(file: DesktopExternalFile) {
        when (file.kind) {
            DesktopExternalFileKind.SUBSTRATE -> when (substrateFileKind(file.source)) {
                is SubstrateFileKind.Unknown -> throw StorageOperationException(
                    StorageErrorCode.UNSUPPORTED_FORMAT,
                    "Substrate file is not a release container"
                )

                is SubstrateFileKind.Genome -> throw StorageOperationException(
                    StorageErrorCode.UNSUPPORTED_FORMAT,
                    "File contains a standalone genome, not a substrate"
                )

                is SubstrateFileKind.Container -> Unit
            }

            DesktopExternalFileKind.GENOME -> {
                val fileBytes = Files.size(file.source)
                if (fileBytes > maximumGenomeBytes) {
                    throw StorageOperationException(
                        StorageErrorCode.TOO_LARGE,
                        "Genome exceeds the release file size limit"
                    )
                }
                when (fileKind(file.source)) {
                    FileKind.DraftWire -> decodeDraftFile(file.source, fileBytes).requireLoadable()

                    FileKind.ReleaseContainer -> Unit

                    FileKind.Unrecognised -> throw StorageOperationException(
                        StorageErrorCode.UNSUPPORTED_FORMAT,
                        "File is not a genome the desktop adapter can read"
                    )
                }
            }
        }
    }

    /**
     * Runs [operation] on the storage dispatcher and converts expected storage failures
     * into [StorageResult.Failure].
     *
     * Cancellation is rethrown before any other mapping so a cancelled caller never
     * receives a failure it would treat as a real storage answer. The check callback is
     * handed to operations that may sit in a long copy or a batch of file-system calls,
     * which is the point at which a desktop user closing the window expects the work to
     * stop rather than finish.
     */
    private suspend fun <R> storageOperation(
        operation: suspend (checkCancellation: () -> Unit) -> R
    ): StorageResult<R> = withContext(Dispatchers.IO) {
        val context = currentCoroutineContext()
        context.ensureActive()
        try {
            StorageResult.Success(operation { context.ensureActive() })
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: StorageOperationException) {
            StorageResult.Failure(StorageError(failure.code, failure.message.orEmpty()))
        } catch (failure: IllegalArgumentException) {
            // The shared name helpers reject a null, blank or path-like display name with
            // an IllegalArgumentException. Turning it into BAD_NAME keeps the common
            // layer's promise that malformed input arrives as a storage outcome; letting
            // it escape would crash the caller instead of showing a validation message.
            StorageResult.Failure(
                StorageError(
                    StorageErrorCode.BAD_NAME,
                    failure.message ?: "Invalid Cellularity file name"
                )
            )
        } catch (failure: SecurityException) {
            // A desktop profile can revoke write access to a directory while the
            // application is running; the common layer already has a distinct code for
            // "storage exists but is no longer usable".
            StorageResult.Failure(
                StorageError(
                    StorageErrorCode.PERMISSION_REVOKED,
                    failure.message ?: "Storage permission was revoked"
                )
            )
        } catch (failure: FileNotFoundException) {
            // The file was listed a moment ago and is gone now. That is a real "not
            // found" for the caller, not an I/O fault, and the two produce different
            // messages in the freezer.
            StorageResult.Failure(
                StorageError(
                    StorageErrorCode.NOT_FOUND,
                    failure.message ?: "File does not exist"
                )
            )
        } catch (failure: NoSuchFileException) {
            StorageResult.Failure(
                StorageError(
                    StorageErrorCode.NOT_FOUND,
                    failure.message ?: "File does not exist"
                )
            )
        } catch (failure: IOException) {
            StorageResult.Failure(
                StorageError(
                    StorageErrorCode.IO_FAILURE,
                    failure.message ?: "Storage operation failed"
                )
            )
        }
    }

    /**
     * Resolves the storage directory on first use.
     *
     * The Android adapter defers the same work for the same reason: the constructor is
     * called from UI composition, and creating a directory there would block the first
     * frame. The lock and the volatile field keep two concurrent callers from racing to
     * create the same directory, and the result is cached because path canonicalisation
     * is a real file-system call.
     */
    private fun storageDirectory(): Path {
        initializedDirectory?.let { return it }
        return synchronized(initializationLock) {
            initializedDirectory?.let { return it }
            Files.createDirectories(requestedDirectory)
            if (!Files.isDirectory(requestedDirectory)) {
                throw IOException("Storage location is not a directory")
            }
            // Names are compared against the canonical directory, so both sides of the
            // comparison have to come from the same call.
            val directory = requestedDirectory.toRealPath()
            removeAbandonedTemporaryFiles(directory)
            initializedDirectory = directory
            directory
        }
    }

    /**
     * Builds the path of a stored file inside the storage directory.
     *
     * Display names reach this method from the UI and, indirectly, from user input, so
     * the name is rejected before a path exists rather than sanitised afterwards: a name
     * that escapes the storage directory would let a save overwrite an arbitrary file.
     * The resolved parent is then compared against the storage directory so a symbolic
     * link placed inside the directory cannot become a way out of it either.
     */
    private fun resolve(fileName: String): Path {
        if (!CellularityFileNames.isValidDisplayName(fileName)) {
            throw StorageOperationException(
                StorageErrorCode.BAD_NAME,
                "File name is not a safe single path element"
            )
        }
        val directory = storageDirectory()
        val candidate = directory.resolve(fileName)
        if (candidate.parent != directory) {
            throw StorageOperationException(
                StorageErrorCode.BAD_NAME,
                "File name must not contain a path"
            )
        }
        if (Files.exists(candidate)) {
            val real = candidate.toRealPath()
            if (real.parent != directory) {
                throw StorageOperationException(
                    StorageErrorCode.BAD_NAME,
                    "A symbolic link cannot point outside the storage directory"
                )
            }
        }
        return candidate
    }

    /**
     * Snapshot of the stored file names, sorted like the Android adapter so a listing
     * does not reshuffle between refreshes.
     *
     * Only regular files are listed. The temporary files of an interrupted write and the
     * data files themselves share this directory, and a `.writing` sibling must never be
     * mistaken for user data.
     */
    private fun listFileNames(): List<String> =
        Files.newDirectoryStream(storageDirectory()).use { entries ->
            entries
                .filter { Files.isRegularFile(it) }
                .map { it.fileName.toString() }
                .sortedWith(String.CASE_INSENSITIVE_ORDER)
        }

    /** Deletes a file and makes the directory entry durable, as the Android store does. */
    private fun deleteFile(file: Path) {
        Files.delete(file)
        forceDirectoryEntry(storageDirectory())
    }

    /**
     * Renames a stored file without ever replacing an existing one.
     *
     * The common layer reports a duplicate name as its own outcome, so an overwriting
     * move would silently destroy the file the user was trying to keep.
     */
    private fun renameFile(
        source: Path,
        target: Path,
        checkCancellation: () -> Unit
    ) {
        if (!Files.isRegularFile(source)) {
            throw StorageOperationException(
                StorageErrorCode.NOT_FOUND,
                "File does not exist"
            )
        }
        if (Files.exists(target)) {
            throw StorageOperationException(
                StorageErrorCode.NAME_EXISTS,
                "A file with that name already exists"
            )
        }
        checkCancellation()
        try {
            Files.move(source, target)
        } catch (failure: FileAlreadyExistsException) {
            throw StorageOperationException(
                StorageErrorCode.NAME_EXISTS,
                "A file with that name already exists"
            )
        }
        forceDirectoryEntry(storageDirectory())
    }

    /**
     * Writes [produce] into a sibling temporary file and moves it over [target].
     *
     * The temporary file lives in the storage directory rather than the system temporary
     * directory so the move stays on one volume, which is what makes it atomic: a crash
     * leaves either the previous file or the new one, never a half-written genome.
     *
     * [replaceExisting] is false for an import, which must not overwrite a plate the user
     * already has. The refusal is made at the move rather than only before the copy, because
     * the two are separated by however long the copy takes: a name that appeared in the
     * meantime is exactly what an overwriting move would destroy.
     *
     * [declaredBytes] is null when the producer decides the size -- a plate is written by the
     * native writer, which knows how much world there was to write -- and the check then falls to
     * [validate], which is asked what the file *is* rather than how long it is. A caller that can
     * count its own bytes passes the count and needs no validation: a size that disagrees with the
     * write is the same fault, caught earlier.
     */
    private suspend fun atomicReplace(
        target: Path,
        declaredBytes: Long?,
        checkCancellation: () -> Unit,
        replaceExisting: Boolean = true,
        validate: ((temporary: Path) -> Unit)? = null,
        produce: suspend (temporary: Path) -> Unit
    ) {
        val temporary = Files.createTempFile(
            storageDirectory(),
            target.fileName.toString(),
            TEMPORARY_SUFFIX
        )
        try {
            produce(temporary)
            // Checked after the write instead of trusting the producer's own count: a
            // producer that reported success while writing less would commit a truncated
            // draft that only fails much later, when the user tries to open it.
            val storedBytes = Files.size(temporary)
            if (declaredBytes != null && storedBytes != declaredBytes) {
                throw IOException(
                    "Temporary file holds $storedBytes bytes, expected $declaredBytes"
                )
            }
            if (declaredBytes == null && storedBytes <= 0L) {
                throw IOException("Temporary file is empty")
            }
            validate?.invoke(temporary)
            checkCancellation()
            try {
                if (replaceExisting) {
                    Files.move(
                        temporary,
                        target,
                        StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE
                    )
                } else {
                    // Deliberately *not* an atomic move: on Windows an atomic move replaces the
                    // file that is already there and says nothing about it, because the flag that
                    // promises atomicity is expressed as a rename that may overwrite. Asking for a
                    // plain move keeps the one property this branch exists for -- a name that is
                    // taken is refused -- and a move inside one directory is a rename on the same
                    // volume either way. It was a real fault, not a theoretical one: this branch
                    // used to pass ATOMIC_MOVE and a save under a taken name quietly replaced the
                    // plate that was there.
                    Files.move(temporary, target)
                }
            } catch (failure: FileAlreadyExistsException) {
                throw StorageOperationException(
                    StorageErrorCode.NAME_EXISTS,
                    "A file with that name already exists"
                )
            } catch (failure: AtomicMoveNotSupportedException) {
                // Some volumes cannot move atomically. Replacing the file is still
                // better than failing the save, and the temporary file is complete by
                // this point, so the worst case is a copy that is interrupted and
                // detected as corrupt on the next read.
                if (replaceExisting) {
                    Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING)
                } else {
                    try {
                        Files.move(temporary, target)
                    } catch (existing: FileAlreadyExistsException) {
                        throw StorageOperationException(
                            StorageErrorCode.NAME_EXISTS,
                            "A file with that name already exists"
                        )
                    }
                }
            }
            forceDirectoryEntry(storageDirectory())
        } catch (failure: Throwable) {
            removeTemporaryFile(temporary)
            throw failure
        }
    }

    /** Best-effort cleanup; a leftover temporary file is removed on the next start. */
    private fun removeTemporaryFile(temporary: Path) {
        try {
            Files.deleteIfExists(temporary)
        } catch (failure: IOException) {
            // The primary failure is the one the caller needs to see.
        }
    }

    private fun removeAbandonedTemporaryFiles(directory: Path) {
        Files.newDirectoryStream(directory, "*$TEMPORARY_SUFFIX").use { abandoned ->
            abandoned.forEach(::removeTemporaryFile)
        }
    }

    /**
     * Flushes the directory entry for a create, rename or delete.
     *
     * Without it a power loss can leave a newly written genome missing even though its
     * contents were flushed, which is exactly the case atomic writes exist to prevent.
     * Directory channels are not openable on every platform, so a refusal here is not
     * treated as a failed operation: the data is already durable, only the name may be
     * stale after a crash.
     */
    private fun forceDirectoryEntry(directory: Path) {
        try {
            FileChannel.open(directory, StandardOpenOption.READ).use { channel ->
                channel.force(true)
            }
        } catch (failure: IOException) {
            // Best effort; see the method comment.
        }
    }

    private fun substrateFileKind(file: Path): SubstrateFileKind {
        val bootstrap = readRange(file, 0L, BOOTSTRAP_BYTES) ?: return SubstrateFileKind.Unknown
        if (!bootstrap.copyOfRange(0, MAGIC_BYTES).contentEquals(CONTAINER_MAGIC)) {
            // An .substrate file that is not a release container is either a legacy
            // stream this adapter has no reader for or something the user renamed. Both
            // are reported as an unsupported format rather than as corruption, because
            // nothing about the file itself is known to be damaged.
            return SubstrateFileKind.Unknown
        }
        if (u16(bootstrap, 8) != SUBSTRATE_DOCUMENT_KIND) return SubstrateFileKind.Genome
        if (crc32(bootstrap, BOOTSTRAP_CRC_BYTES) != u32(bootstrap, BOOTSTRAP_CRC_OFFSET)) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "Release container bootstrap checksum is invalid"
            )
        }
        val containerMajor = u16(bootstrap, 4)
        if (containerMajor > SUPPORTED_CONTAINER_MAJOR) {
            // The bootstrap is intact enough to prove a newer writer produced this file,
            // which is the one case the freezer can describe as "newer version" instead
            // of "unreadable".
            return SubstrateFileKind.Container(FileLoadCompatibility.NEWER_VERSION)
        }
        val declaredBytes = u64(bootstrap, DECLARED_FILE_BYTES_OFFSET)
        val fileBytes = Files.size(file)
        if (declaredBytes != fileBytes) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "Release container is truncated or has trailing data"
            )
        }
        val metadata = releaseRegion(
            offset = u64(bootstrap, METADATA_OFFSET_OFFSET),
            bytes = u64(bootstrap, METADATA_BYTES_OFFSET),
            minimumBytes = 1L,
            limit = MAXIMUM_METADATA_BYTES,
            label = "metadata",
            fileBytes = fileBytes
        )
        val preview = releaseRegion(
            offset = u64(bootstrap, PREVIEW_OFFSET_OFFSET),
            bytes = u64(bootstrap, PREVIEW_BYTES_OFFSET),
            minimumBytes = 0L,
            limit = MAXIMUM_PREVIEW_BYTES,
            label = "preview",
            fileBytes = fileBytes
        )
        val directory = releaseRegion(
            offset = u64(bootstrap, DIRECTORY_OFFSET_OFFSET),
            bytes = u64(bootstrap, DIRECTORY_BYTES_OFFSET),
            minimumBytes = 8L,
            limit = MAXIMUM_DIRECTORY_BYTES,
            label = "directory",
            fileBytes = fileBytes
        )
        if (metadata.overlaps(preview) || metadata.overlaps(directory) ||
            preview.overlaps(directory)
        ) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "Release container regions overlap"
            )
        }
        // The metadata CRC and the section directory are checked by the release decoder
        // when the substrate is opened; recognising a well-formed envelope is all this
        // adapter can honestly certify.
        //
        // TODO(native-inspection): replace this envelope check with
        // NativeCellularityControllerBridge.inspectSubstratePath once the desktop module
        // links the native release-substrate codec, and populate SubstrateFileInfo from
        // the decoded inspection the way AndroidCellularityFileRepository does.
        return SubstrateFileKind.Container(FileLoadCompatibility.LOADABLE)
    }

    /**
     * Checks one container region against the grammar the release decoder enforces.
     *
     * The bounds are repeated rather than imported because the release contract object is
     * internal to :shared. Only the lengths that are structurally knowable from the
     * bootstrap are checked here; the region contents stay the release decoder's business.
     */
    private fun releaseRegion(
        offset: Long,
        bytes: Long,
        minimumBytes: Long,
        limit: Long,
        label: String,
        fileBytes: Long
    ): ReleaseRegion {
        if (bytes == 0L && minimumBytes == 0L) {
            if (offset != 0L) {
                throw StorageOperationException(
                    StorageErrorCode.CORRUPTED,
                    "Release container $label region is empty but has an offset"
                )
            }
            return ReleaseRegion(EMPTY_REGION, EMPTY_REGION)
        }
        if (offset < 1L || bytes < minimumBytes || bytes > limit || bytes > fileBytes ||
            offset > fileBytes - bytes
        ) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "Release container $label region is outside the file"
            )
        }
        return ReleaseRegion(offset, offset + bytes)
    }

    private fun fileKind(file: Path): FileKind {
        val magic = readRange(file, 0L, MAGIC_BYTES) ?: return FileKind.Unrecognised
        // Compared as bytes rather than as a decoded integer: the header is the only
        // part of the two formats that shares nothing, and a byte comparison cannot be
        // got wrong by picking the endianness of the wrong one of them.
        return when {
            magic.contentEquals(CONTAINER_MAGIC) -> FileKind.ReleaseContainer
            magic.contentEquals(DRAFT_WIRE_MAGIC) -> FileKind.DraftWire
            else -> FileKind.Unrecognised
        }
    }

    /**
     * Reads only the CLTY bootstrap and metadata block.
     *
     * Sorting these two layouts by magic rather than by extension is what keeps a
     * mislabelled file from being handed to the wrong decoder; the size ceiling is
     * applied by the callers, so this is the only path that has to be bounded.
     */
    private fun inspectReleaseGenome(file: Path): ReleaseGenomeFileSummary {
        try {
            RandomAccessFile(file.toFile(), "r").use { input ->
                return ReleaseGenomeFileInspector.inspectMetadata(
                    fileByteCount = input.length(),
                    reader = input.releaseRangeReader()
                )
            }
        } catch (failure: SaveContainerFormatException) {
            throw StorageOperationException(
                storageErrorCode(failure.code),
                failure.message ?: "Invalid release genome"
            )
        }
    }

    private fun RandomAccessFile.releaseRangeReader(): ReleaseGenomeRangeReader =
        ReleaseGenomeRangeReader { offset, byteCount -> readRange(this, offset, byteCount) }

    /**
     * Reads a bounded range, reporting an unreadable range as an empty result.
     *
     * The release inspector and the container check both expect a reader that returns
     * fewer bytes than asked for when the file is short; turning that into an empty array
     * lets the shared code report truncation with its own error code instead of this
     * adapter inventing one.
     */
    private fun readRange(file: Path, offset: Long, byteCount: Int): ByteArray? {
        val fileBytes = try {
            Files.size(file)
        } catch (failure: NoSuchFileException) {
            return null
        }
        if (offset < 0L || byteCount < 0 || offset > fileBytes ||
            byteCount.toLong() > fileBytes - offset
        ) {
            return null
        }
        RandomAccessFile(file.toFile(), "r").use { input ->
            return readRange(input, offset, byteCount)
        }
    }

    private fun readRange(input: RandomAccessFile, offset: Long, byteCount: Int): ByteArray {
        if (offset < 0L || byteCount < 0 || offset > input.length() ||
            byteCount.toLong() > input.length() - offset
        ) {
            return ByteArray(0)
        }
        val bytes = ByteArray(byteCount)
        input.seek(offset)
        input.readFully(bytes)
        return bytes
    }

    /**
     * Reads a draft file after proving it did not change underneath the read.
     *
     * The decoded genome is only meaningful for the bytes that were actually on disk, and
     * this class cannot hold a lock against another process on a desktop. A file whose
     * size or timestamp moved between the two checks is reported as corrupt rather than
     * loaded, because a partially written draft decodes into a plausible but wrong genome.
     */
    private fun decodeDraftFile(file: Path, fileBytes: Long): GenomeDraft {
        val before = Files.getLastModifiedTime(file).toMillis()
        val bytes = Files.readAllBytes(file)
        if (bytes.size.toLong() != fileBytes ||
            Files.getLastModifiedTime(file).toMillis() != before
        ) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "Genome draft changed while it was being read"
            )
        }
        val genome = try {
            GenomeDraftWireCodec.decode(bytes)
        } catch (failure: GenomeDraftWireFormatException) {
            throw StorageOperationException(
                storageErrorCode(failure.code),
                failure.message ?: "Invalid genome draft"
            )
        } catch (failure: IllegalArgumentException) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                failure.message ?: "Invalid genome draft"
            )
        }
        return genome.also { draft ->
            if (GenomeInitialGeneCompatibility.extract(draft) == null) {
                throw StorageOperationException(
                    StorageErrorCode.CORRUPTED,
                    "Genome draft does not mark exactly one initial gene"
                )
            }
        }
    }

    /**
     * Refuses a draft that could not be loaded back.
     *
     * Only the initial-gene marker is checked, because that is the one piece of draft
     * state the wire format stores outside the gene list. Value ranges stay the native
     * runtime's business: the Android adapter validates drafts through the bridge rather
     * than in Kotlin, and duplicating those limits here would let the two drift apart.
     */
    private fun GenomeDraft.requireValidDraft() {
        if (GenomeInitialGeneCompatibility.extract(this) == null) {
            throw StorageOperationException(
                StorageErrorCode.CORRUPTED,
                "A genome must mark exactly one initial gene"
            )
        }
    }

    private fun GenomeDraft.requireLoadable(): GenomeDraft = also { draft ->
        draft.requireValidDraft()
    }

    /** The bit mask the release encoder derives from the unique genes of a genome. */
    private fun GenomeDraft.cellTypeMask(): Long {
        var mask = 0L
        genes.forEach { gene ->
            val nativeValue = gene.cellType.nativeValue
            if (nativeValue in 0 until Long.SIZE_BITS) mask = mask or (1L shl nativeValue)
        }
        return mask
    }

    /**
     * Creation time to show for a substrate.
     *
     * The real value is release metadata the native inspector would return. The file's own
     * timestamp is used only while that is unavailable, and only when the platform reports
     * one, so the freezer shows an approximate age instead of claiming the epoch.
     */
    private fun fileCreationMillis(file: Path): Long? = try {
        Files.getLastModifiedTime(file).toMillis().takeIf { it > 0L }
    } catch (failure: IOException) {
        null
    }

    /**
     * Translates a shared format-error code into the storage vocabulary the UI branches on.
     *
     * A version mismatch and a size refusal are actionable ("the file is from a newer
     * build", "the file is too big"), while everything else is a damaged or unrecognised
     * file; keeping the distinction means the freezer does not describe a future file as
     * corrupt.
     */
    private fun storageErrorCode(code: CellularityErrorCode): StorageErrorCode = when (code) {
        CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
        CellularityErrorCode.NEWER_PRODUCT_VERSION -> StorageErrorCode.NEWER_VERSION
        CellularityErrorCode.LIMIT_EXCEEDED -> StorageErrorCode.TOO_LARGE
        CellularityErrorCode.IO_FAILURE -> StorageErrorCode.IO_FAILURE
        CellularityErrorCode.TRUNCATED_INPUT,
        CellularityErrorCode.INVALID_LENGTH,
        CellularityErrorCode.INVALID_HEADER,
        CellularityErrorCode.INVALID_VALUE,
        CellularityErrorCode.INVALID_COUNT,
        CellularityErrorCode.TRAILING_DATA -> StorageErrorCode.CORRUPTED
        else -> StorageErrorCode.UNSUPPORTED_FORMAT
    }

    private fun u16(bytes: ByteArray, offset: Int): Int =
        (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8)

    private fun u32(bytes: ByteArray, offset: Int): Int =
        u16(bytes, offset) or (u16(bytes, offset + 2) shl 16)

    private fun u64(bytes: ByteArray, offset: Int): Long =
        (u32(bytes, offset).toLong() and 0xffff_ffffL) or
            (u32(bytes, offset + 4).toLong() shl 32)

    private companion object {
        /** `CLTY`, the release container magic, stored as the first four file bytes. */
        val CONTAINER_MAGIC = "CLTY".encodeToByteArray()

        /**
         * The first four bytes of the draft wire format, derived from the shared constant
         * so this file cannot disagree with the codec about them.
         */
        val DRAFT_WIRE_MAGIC = byteArrayOf(
            (GenomeContract.WIRE_MAGIC ushr 24).toByte(),
            (GenomeContract.WIRE_MAGIC ushr 16).toByte(),
            (GenomeContract.WIRE_MAGIC ushr 8).toByte(),
            GenomeContract.WIRE_MAGIC.toByte()
        )

        /**
         * The fixed bootstrap grammar this adapter reads: magic, container major/minor,
         * document kind, header size, feature and header flags, declared byte count, the
         * metadata block, the embedded preview, the section directory and a CRC32 over
         * the first 108 bytes, all little-endian. It is duplicated rather than imported
         * because the shared contract object is internal to :shared; the values are
         * covered by the desktop tests that read a container written by the shared code.
         */
        const val BOOTSTRAP_BYTES = 112
        const val BOOTSTRAP_CRC_BYTES = 108
        const val BOOTSTRAP_CRC_OFFSET = 108
        const val MAGIC_BYTES = 4
        const val SUBSTRATE_DOCUMENT_KIND = 1
        const val SUPPORTED_CONTAINER_MAJOR = 1

        /**
         * Offsets of the bootstrap fields this adapter reads, counted from the start of
         * the file past the magic, the container version, the document kind, the header
         * length, the feature bits, the header flags and the preview descriptor. They are
         * named rather than written inline because the layout is a run of fixed-width
         * fields: an offset that is one field out reads a neighbouring value and yields a
         * plausible but wrong verdict about an intact file.
         *
         * Cross-checked against the shared encoder by
         * `releaseBootstrapOffsetsMatchTheSharedEncoder`.
         */
        const val DECLARED_FILE_BYTES_OFFSET = 24
        const val METADATA_OFFSET_OFFSET = 32
        const val METADATA_BYTES_OFFSET = 40
        const val PREVIEW_OFFSET_OFFSET = 64
        const val PREVIEW_BYTES_OFFSET = 72
        const val DIRECTORY_OFFSET_OFFSET = 88
        const val DIRECTORY_BYTES_OFFSET = 96

        /** Duplicated from the internal release contract; see [BOOTSTRAP_BYTES]. */
        const val MAXIMUM_METADATA_BYTES = 64L * 1024L
        const val MAXIMUM_PREVIEW_BYTES = 8L * 1024L * 1024L
        const val MAXIMUM_DIRECTORY_BYTES = 4L * 1024L * 1024L

        const val EMPTY_REGION = 0L

        /**
         * The extensions that decide what an outside file is.
         *
         * The same two strings the shared name helpers build file names from, kept here because
         * that object exposes them only through the names it constructs, and the question
         * `resolveExternalFile` asks is about the name the user's file already has. Compared
         * without regard to case, because Windows hands over whatever case the file was saved
         * with; the stored name is then built by the shared helper, which is lower case.
         */
        const val SUBSTRATE_EXTENSION = ".substrate"
        const val GENOME_EXTENSION = ".genome"

        /** Suffix of an in-flight write; see [atomicReplace]. */
        const val TEMPORARY_SUFFIX = ".writing"

        /**
         * How many seconds of simulated time an hour is.
         *
         * A plate records its time in seconds; the collection shows hours, and both platforms
         * divide here rather than storing a second unit in the shared model.
         */
        const val SECONDS_PER_HOUR = 3600.0

        /**
         * CRC32 over [endExclusive] bytes of [bytes], matching the checksum the release
         * container stores in its bootstrap.
         */
        fun crc32(bytes: ByteArray, endExclusive: Int): Int {
            var crc = -1
            for (index in 0 until endExclusive) {
                crc = crc xor (bytes[index].toInt() and 0xff)
                repeat(8) {
                    crc = (crc ushr 1) xor (0xedb88320.toInt() and -(crc and 1))
                }
            }
            return crc.inv()
        }
    }
}

/** The layouts a stored `.substrate` file can have, as far as the desktop can tell. */
private sealed interface SubstrateFileKind {
    /** A recognised release container whose envelope passed inspection. */
    data class Container(val compatibility: FileLoadCompatibility) : SubstrateFileKind

    /** The file is a release container, but it holds a genome body. */
    data object Genome : SubstrateFileKind

    /** Not a release container: a legacy stream, or a file the user renamed. */
    data object Unknown : SubstrateFileKind
}

/** The genome layouts this adapter can recognise by magic. */
private enum class FileKind {
    ReleaseContainer,
    DraftWire,
    Unrecognised
}

/** Half-open byte range of a container region. */
private data class ReleaseRegion(val start: Long, val endExclusive: Long) {
    fun overlaps(other: ReleaseRegion): Boolean {
        if (start == endExclusive || other.start == other.endExclusive) return false
        return start < other.endExclusive && other.start < endExclusive
    }
}

/**
 * Expected storage failure carrying the code the common layer should branch on.
 *
 * It exists so the file-system code can report a precise outcome from deep inside a
 * helper without every intermediate function growing a sealed result of its own.
 */
private class StorageOperationException(
    val code: StorageErrorCode,
    message: String
) : Exception(message)
