/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.files

import com.cytarch.cellularity.shared.files.CellularityFileLimits
import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeFileInspector
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Behaviour of the desktop storage adapter.
 *
 * The tests deliberately build the CLTY containers with their own writer instead of
 * reusing the shared one: the shared encoder is internal to :shared, and a test that
 * borrowed it would prove only that the adapter reads what the same code wrote, not that
 * it reads the frozen layout. The genome gene count recorded here is verified against
 * the public inspector in [releaseGenomeGeneCountIsReadableThroughThePublicInspector] so
 * the hand-built container cannot drift from the format unnoticed.
 */
class DesktopCellularityFileRepositoryTest {

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private fun storageDirectory(): Path = temporaryFolder.newFolder("storage").toPath()

    private fun repository(
        directory: Path = storageDirectory(),
        maximumGenomeBytes: Long = CellularityFileLimits.MAX_GENOME_SIZE_BYTES
    ) = DesktopCellularityFileRepository(directory, maximumGenomeBytes)

    private fun smallGenome(geneCount: Int = 3): GenomeDraft =
        GenomeDraft.editorDefaults { geneIndex ->
            listOf(geneIndex / 10.0f, 0.5f, 0.5f)
        }.let { defaults ->
            GenomeDraft(List(geneCount) { index -> defaults.genes[index % defaults.genes.size] })
                .withOnlyInitial(0)
        }

    private fun <T> value(result: StorageResult<T>): T {
        assertTrue("expected success but was $result", result is StorageResult.Success)
        return (result as StorageResult.Success).value
    }

    private fun failure(result: StorageResult<*>): StorageErrorCode {
        assertTrue("expected failure but was $result", result is StorageResult.Failure)
        return (result as StorageResult.Failure).error.code
    }

    @Test
    fun genomeDraftRoundTripsThroughTheStorageDirectory() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val genome = smallGenome()

        value(repository.writeGenomeDraft("My Plate", genome))

        assertTrue(Files.isRegularFile(storage.resolve("My Plate.genome")))
        // Nothing in flight is left behind for the next listing to pick up.
        assertEquals(
            listOf("My Plate.genome"),
            Files.newDirectoryStream(storage).use { stream ->
                stream.map { it.fileName.toString() }.sorted()
            }
        )
        val stored = Files.readAllBytes(storage.resolve("My Plate.genome"))
        assertEquals(
            "stored magic",
            GenomeContract.WIRE_MAGIC,
            (stored[0].toInt() and 0xff shl 24) or (stored[1].toInt() and 0xff shl 16) or
                (stored[2].toInt() and 0xff shl 8) or (stored[3].toInt() and 0xff)
        )

        val restored = value(repository.readGenomeDraft("My Plate"))
        assertEquals(genome.genes.size, restored.genes.size)
        assertEquals(
            GenomeDraftWireCodec.encode(genome).toList(),
            GenomeDraftWireCodec.encode(restored).toList()
        )
    }

    @Test
    fun genomeFileNameIsAcceptedWithAndWithoutItsExtension() = runBlocking {
        val repository = repository()
        value(repository.writeGenomeDraft("Tubulin", smallGenome()))

        val byName = value(repository.readGenomeDraft("Tubulin"))
        val byFileName = value(repository.readGenomeDraft("Tubulin.genome"))

        assertEquals(byName.genes.size, byFileName.genes.size)
        assertEquals(listOf("Tubulin.genome"), value(repository.listGenomeIds()).map { it.fileName })
        assertEquals(listOf("Tubulin"), value(repository.listGenomeNames()))
    }

    @Test
    fun readingAMissingGenomeReportsNotFound() = runBlocking {
        val repository = repository()

        assertEquals(StorageErrorCode.NOT_FOUND, failure(repository.readGenomeDraft("Absent")))
        assertEquals(
            StorageErrorCode.NOT_FOUND,
            failure(repository.readGenomeInfo(StoredGenomeId("Absent.genome")))
        )
        assertFalse(value(repository.substrateExists("Absent")))
    }

    @Test
    fun aTruncatedGenomeDraftIsReportedAsCorrupted() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        // Stored through the adapter, then cut short the way an interrupted copy of the
        // file would leave it.
        value(repository.writeGenomeDraft("Truncated", smallGenome()))
        val file = storage.resolve("Truncated.genome")
        val bytes = Files.readAllBytes(file)
        Files.write(file, bytes.copyOfRange(0, bytes.size - 8))

        assertEquals(StorageErrorCode.CORRUPTED, failure(repository.readGenomeDraft("Truncated")))
    }

    @Test
    fun aForeignFileIsReportedAsAnUnsupportedFormat() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        Files.write(storage.resolve("Alien.genome"), "not a genome".toByteArray())

        assertEquals(
            StorageErrorCode.UNSUPPORTED_FORMAT,
            failure(repository.readGenomeDraft("Alien"))
        )
        assertEquals(
            StorageErrorCode.UNSUPPORTED_FORMAT,
            failure(repository.readGenomeInfo(StoredGenomeId("Alien.genome")))
        )
    }

    @Test
    fun aGenomeAboveTheLimitIsReportedAsTooLarge() = runBlocking {
        val repository = repository(maximumGenomeBytes = 512L)
        val genome = smallGenome()

        assertEquals(
            StorageErrorCode.TOO_LARGE,
            failure(repository.writeGenomeDraft("Huge", genome))
        )
        // Nothing was committed, so the failed save did not leave a partial file.
        assertEquals(emptyList<String>(), value(repository.listGenomeNames()))
    }

    @Test
    fun aDraftWithoutASingleInitialGeneIsRefused() = runBlocking {
        val repository = repository()
        val genome = smallGenome()
        val withoutInitial = GenomeDraft(genome.genes.map { it.copy(initial = false) })

        assertEquals(
            StorageErrorCode.CORRUPTED,
            failure(repository.writeGenomeDraft("NoInitial", withoutInitial))
        )
        assertEquals(emptyList<String>(), value(repository.listGenomeNames()))
    }

    @Test
    fun genomeInfoDescribesADraftWrittenByThisAdapter() = runBlocking {
        val repository = repository()
        value(repository.writeGenomeDraft("Counter", smallGenome(geneCount = 3)))

        val info = value(repository.readGenomeInfo(StoredGenomeId("Counter.genome")))

        assertEquals("Counter", info.displayName)
        assertEquals(3, info.geneCount)
        assertEquals(0, info.initialGeneIndex)
        assertEquals(FileLoadCompatibility.LOADABLE, info.compatibility)
        assertFalse(info.previewAvailable)
    }

    @Test
    fun releaseGenomeGeneCountIsReadableThroughThePublicInspector() {
        // Guards the hand-built container in this test against drifting away from the
        // format the adapter is expected to recognise.
        val summary = ReleaseGenomeFileInspector.inspect(releaseGenomeBytes(geneCount = 7))

        assertEquals(7, summary.geneCount)
        assertEquals(1, summary.contentSchemaMajor)
        assertTrue(summary.bodySupported)
    }

    /**
     * Proves the adapter reads the container's declared byte count from the right field.
     *
     * The adapter repeats the bootstrap layout instead of importing it, because the
     * release contract is internal to :shared, and a repeated layout is the one mistake a
     * test cannot catch by building its own containers: the test would repeat it too.
     * This test moves a byte at the end of the file and nothing else, so the only way to
     * notice is to compare the declared count against the real one.
     */
    @Test
    fun aContainerWithTrailingBytesIsReportedAsCorrupted() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val intact = releaseContainerBytes(geneCount = 5, documentKind = SUBSTRATE_KIND)
        Files.write(storage.resolve("Intact.substrate"), intact)
        Files.write(
            storage.resolve("Extended.substrate"),
            intact + ByteArray(16) { 0x5a }
        )

        assertEquals(
            FileLoadCompatibility.LOADABLE,
            value(repository.readSubstrateInfo(StoredSubstrateId("Intact"))).compatibility
        )
        assertEquals(
            StorageErrorCode.CORRUPTED,
            failure(repository.readSubstrateInfo(StoredSubstrateId("Extended")))
        )
    }

    @Test
    fun releaseGenomeInfoIsReadFromTheContainerMetadata() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        Files.write(storage.resolve("Release.genome"), releaseGenomeBytes(geneCount = 7))

        val info = value(repository.readGenomeInfo(StoredGenomeId("Release.genome")))

        assertEquals(7, info.geneCount)
        assertEquals(0, info.initialGeneIndex)
        assertEquals(7, info.uniqueGeneCount)
        assertEquals(FileLoadCompatibility.LOADABLE, info.compatibility)
        assertFalse(info.previewAvailable)
    }

    @Test
    fun releaseGenomeBodyReadIsRefusedInsteadOfInvented() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        Files.write(storage.resolve("Release.genome"), releaseGenomeBytes(geneCount = 7))

        // The desktop adapter has no release body decoder. Reporting an unsupported
        // format is the honest answer; returning a synthesized genome would not be.
        assertEquals(
            StorageErrorCode.UNSUPPORTED_FORMAT,
            failure(repository.readGenomeDraft("Release"))
        )
    }

    @Test
    fun substrateInfoDescribesARecognisedContainer() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val file = storage.resolve("Dish.substrate")
        Files.write(file, releaseContainerBytes(geneCount = 5, documentKind = SUBSTRATE_KIND))

        val info = value(repository.readSubstrateInfo(StoredSubstrateId("Dish")))

        assertEquals(FileLoadCompatibility.LOADABLE, info.compatibility)
        assertFalse(info.previewAvailable)
        assertNotNull(info.createdAtUnixMillis)
        // Simulation state is native-owned; the adapter must not guess it.
        assertEquals(0.0, info.time, 0.0)
        assertEquals(0, info.cellCount)
        assertTrue(value(repository.substrateExists("Dish")))
        assertEquals(listOf("Dish"), value(repository.listSubstrateIds()).map { it.value })
    }

    @Test
    fun substrateInfoReportsANewerContainerAsANewerVersion() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        Files.write(
            storage.resolve("Future.substrate"),
            releaseContainerBytes(
                geneCount = 5,
                documentKind = SUBSTRATE_KIND,
                containerMajor = 2
            )
        )

        val info = value(repository.readSubstrateInfo(StoredSubstrateId("Future")))

        assertEquals(FileLoadCompatibility.NEWER_VERSION, info.compatibility)
    }

    @Test
    fun substrateInfoReportsADamagedEnvelopeAsCorrupted() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val file = storage.resolve("Damaged.substrate")
        Files.write(file, releaseContainerBytes(geneCount = 5, documentKind = SUBSTRATE_KIND))
        val bytes = Files.readAllBytes(file)
        // The bootstrap checksum covers the first 108 bytes; breaking one of them is the
        // cheapest way to build the container a torn write would leave behind.
        bytes[63] = (bytes[63].toInt() xor 0x40).toByte()
        Files.write(file, bytes)

        assertEquals(
            StorageErrorCode.CORRUPTED,
            failure(repository.readSubstrateInfo(StoredSubstrateId("Damaged")))
        )
    }

    @Test
    fun substrateInfoReportsATruncatedContainerAsCorrupted() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val file = storage.resolve("Short.substrate")
        val bytes = releaseContainerBytes(geneCount = 5, documentKind = SUBSTRATE_KIND)
        Files.write(file, bytes.copyOfRange(0, bytes.size - 4))

        assertEquals(
            StorageErrorCode.CORRUPTED,
            failure(repository.readSubstrateInfo(StoredSubstrateId("Short")))
        )
    }

    @Test
    fun substrateInfoReportsAGenomeContainerAsAnUnsupportedFormat() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        Files.write(storage.resolve("Genome.substrate"), releaseGenomeBytes(geneCount = 5))

        assertEquals(
            StorageErrorCode.UNSUPPORTED_FORMAT,
            failure(repository.readSubstrateInfo(StoredSubstrateId("Genome")))
        )
    }

    @Test
    fun substrateInfoReportsAFileThatIsNotAContainerAsAnUnsupportedFormat() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        Files.write(storage.resolve("Legacy.substrate"), ByteArray(64) { 0x11 })

        assertEquals(
            StorageErrorCode.UNSUPPORTED_FORMAT,
            failure(repository.readSubstrateInfo(StoredSubstrateId("Legacy")))
        )
    }

    @Test
    fun deletingAndRenamingKeepTheDirectoryConsistent() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        value(repository.writeGenomeDraft("First", smallGenome()))
        value(repository.writeGenomeDraft("Second", smallGenome()))

        value(repository.renameGenome(StoredGenomeId("First.genome"), "Renamed"))

        assertEquals(listOf("Renamed", "Second"), value(repository.listGenomeNames()))
        assertTrue(Files.isRegularFile(storage.resolve("Renamed.genome")))
        assertFalse(Files.exists(storage.resolve("First.genome")))

        // An existing name is never overwritten: losing the user's file would be worse
        // than a refused rename.
        assertEquals(
            StorageErrorCode.NAME_EXISTS,
            failure(repository.renameGenome(StoredGenomeId("Renamed.genome"), "Second"))
        )
        assertEquals(listOf("Renamed", "Second"), value(repository.listGenomeNames()))

        value(repository.deleteGenome(StoredGenomeId("Renamed.genome")))
        assertEquals(listOf("Second"), value(repository.listGenomeNames()))
        assertEquals(
            StorageErrorCode.NOT_FOUND,
            failure(repository.deleteGenome(StoredGenomeId("Renamed.genome")))
        )
    }

    @Test
    fun substrateRenameAndDeleteTrackTheSameRules() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val container = releaseContainerBytes(geneCount = 5, documentKind = SUBSTRATE_KIND)
        Files.write(storage.resolve("Old.substrate"), container)
        Files.write(storage.resolve("Kept.substrate"), container)

        value(repository.renameSubstrate(StoredSubstrateId("Old"), "New"))

        assertEquals(listOf("Kept", "New"), value(repository.listSubstrateIds()).map { it.value })
        assertEquals(
            StorageErrorCode.NAME_EXISTS,
            failure(repository.renameSubstrate(StoredSubstrateId("New"), "Kept"))
        )

        value(repository.deleteSubstrate(StoredSubstrateId("New")))
        assertEquals(listOf("Kept"), value(repository.listSubstrateIds()).map { it.value })
        assertEquals(
            StorageErrorCode.NOT_FOUND,
            failure(repository.deleteSubstrate(StoredSubstrateId("New")))
        )
    }

    @Test
    fun namesThatWouldEscapeTheStorageDirectoryAreRefused() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val outside = temporaryFolder.newFile("outside.genome").toPath()
        Files.write(outside, GenomeDraftWireCodec.encode(smallGenome()))

        assertEquals(
            StorageErrorCode.BAD_NAME,
            failure(repository.writeGenomeDraft("../outside", smallGenome()))
        )
        assertEquals(
            StorageErrorCode.BAD_NAME,
            failure(repository.writeGenomeDraft("..", smallGenome()))
        )
        // The file outside the storage directory is untouched by the refused write.
        assertTrue(Files.isRegularFile(outside))
        assertEquals(
            "outside",
            CellularityFileNames.genomeNameFromFileName(outside.fileName.toString())
        )
    }

    @Test
    fun theStorageDirectoryIsCreatedOnFirstUse() = runBlocking {
        val parent = temporaryFolder.newFolder("empty").toPath()
        val nested = parent.resolve("a").resolve("b")
        val repository = repository(nested)

        assertEquals(emptyList<String>(), value(repository.listGenomeNames()))
        assertTrue(Files.isDirectory(nested))
    }

    @Test
    fun listingIgnoresDirectoriesAndFilesOfOtherKinds() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        value(repository.writeGenomeDraft("Real", smallGenome()))
        Files.write(storage.resolve("Notes.txt"), "hello".toByteArray())
        Files.createDirectory(storage.resolve("Nested.genome"))

        assertEquals(listOf("Real"), value(repository.listGenomeNames()))
        assertEquals(emptyList<String>(), value(repository.listSubstrateIds()))
    }

    @Test
    fun cancellationPropagatesInsteadOfBeingReportedAsAFailure(): Unit = runBlocking {
        val repository = repository()
        // The channel makes the two outcomes distinguishable. If the repository call is
        // reached and does its job, the send records "completed"; if cancellation won the
        // race, the send never happens and the job is cancelled. Either way the one thing
        // that must never appear is a completed call, which is what a swallowed
        // cancellation would produce.
        val completion = Channel<String>(Channel.RENDEZVOUS)
        val request = launch(Dispatchers.Default) {
            val result = try {
                repository.listGenomeNames()
                "completed"
            } catch (cancelled: CancellationException) {
                "cancelled"
            }
            completion.trySend(result)
        }
        request.cancel()

        var reportedAsCompleted = false
        try {
            // Bounded so a repository that swallowed the cancellation fails the test
            // instead of hanging the build.
            val outcome = withTimeout(5_000) { completion.receive() }
            reportedAsCompleted = outcome == "completed"
        } catch (cancelled: CancellationException) {
            // Nothing was reported: the call never completed, which is the expected shape.
        }
        assertFalse(
            "a cancelled repository call reported a completed storage result",
            reportedAsCompleted
        )
        assertTrue("the cancelled call was not observed as cancelled", request.isCancelled)
    }

    @Test
    fun aConcurrentWriteLeavesThePreviousFileIntact() = runBlocking {
        val storage = storageDirectory()
        val repository = repository(storage)
        val small = smallGenome(geneCount = 3)
        val large = smallGenome(geneCount = 5)
        value(repository.writeGenomeDraft("Shared", small))

        val writes = List(4) { index ->
            async(Dispatchers.IO) {
                repository.writeGenomeDraft("Shared", if (index % 2 == 0) small else large)
            }
        }
        writes.forEach { it.await() }

        // Whatever the interleaving, the file that survives is one complete genome and
        // no temporary file is left beside it.
        val restored = value(repository.readGenomeDraft("Shared"))
        assertTrue(restored.genes.size == 3 || restored.genes.size == 5)
        withTimeout(5_000) {
            assertEquals(
                listOf("Shared.genome"),
                Files.newDirectoryStream(storage).use { stream ->
                    stream.map { it.fileName.toString() }.sorted()
                }
            )
        }
    }

    companion object {
        const val BOOTSTRAP_BYTES = 112
        const val BOOTSTRAP_CRC_BYTES = 108
        const val BATCH_HEADER_BYTES = 112
        const val SUBSTRATE_KIND = 1
        const val GENOME_KIND = 2
        const val CONTAINER_MAJOR = 1

        fun releaseGenomeBytes(geneCount: Int): ByteArray =
            releaseContainerBytes(geneCount = geneCount, documentKind = GENOME_KIND)

        /**
         * A container with a valid bootstrap and metadata block and an empty directory.
         *
         * There is no body, so the release decoder would reject the file at the section
         * level. The bootstrap, the metadata block and the directory header are all
         * well-formed, which is exactly what the desktop adapter is allowed to inspect
         * and what the public inspector validates, so this is the strongest container
         * the adapter can be tested against without a body encoder.
         */
        fun releaseContainerBytes(
            geneCount: Int,
            documentKind: Int,
            containerMajor: Int = CONTAINER_MAJOR
        ): ByteArray {
            val metadata = metadata(documentKind, geneCount)
            val directory = emptyDirectory()
            val metadataOffset = BOOTSTRAP_BYTES.toLong()
            val directoryOffset = metadataOffset + metadata.size
            val fileBytes = directoryOffset + directory.size

            val bootstrap = LittleEndianWriter(BOOTSTRAP_BYTES)
            bootstrap.raw("CLTY".encodeToByteArray())
            bootstrap.u16(containerMajor)
            bootstrap.u16(0)
            bootstrap.u16(documentKind)
            bootstrap.u16(BOOTSTRAP_BYTES)
            bootstrap.u32(0)
            bootstrap.u32(0)
            bootstrap.u32(0)
            bootstrap.u64(fileBytes)
            bootstrap.u64(metadataOffset)
            bootstrap.u64(metadata.size.toLong())
            bootstrap.u32(crc32(metadata))
            bootstrap.u16(0)
            bootstrap.u16(0)
            bootstrap.u32(0)
            bootstrap.u32(0)
            bootstrap.u64(0)
            bootstrap.u64(0)
            bootstrap.u32(0)
            bootstrap.u32(0)
            bootstrap.u64(directoryOffset)
            bootstrap.u64(directory.size.toLong())
            bootstrap.u32(crc32(directory))
            bootstrap.u32(crc32(bootstrap.toByteArray(), BOOTSTRAP_CRC_BYTES))
            val bootstrapBytes = bootstrap.toByteArray()
            assertEquals(BOOTSTRAP_BYTES, bootstrapBytes.size)
            return bootstrapBytes + metadata + directory
        }

        /** A directory with no sections, which is the smallest legal one. */
        fun emptyDirectory(): ByteArray {
            val writer = LittleEndianWriter(8)
            writer.u32(1)
            writer.u32(0)
            return writer.toByteArray()
        }

        /**
         * The metadata block for [documentKind].
         *
         * The critical flag is part of the registered schema, not a free choice: the
         * decoder rejects an entry whose flag disagrees with the field definition, so
         * each field below carries the flag the release codec gives it.
         */
        fun metadata(documentKind: Int, geneCount: Int): ByteArray {
            val entries = if (documentKind == GENOME_KIND) {
                listOf(
                    MetadataField(0x0001, "content_schema_major", 1L, MetadataValueType.U32, true),
                    MetadataField(0x0002, "content_schema_minor", 0L, MetadataValueType.U32, true),
                    MetadataField(0x2001, "gene_count", geneCount.toLong(), MetadataValueType.U32, true),
                    MetadataField(0x2002, "initial_gene_index", 0L, MetadataValueType.U32, true),
                    MetadataField(0x2003, "unique_gene_count", geneCount.toLong(), MetadataValueType.U32, false)
                )
            } else {
                listOf(
                    MetadataField(0x0001, "content_schema_major", 1L, MetadataValueType.U32, true),
                    MetadataField(0x0002, "content_schema_minor", 0L, MetadataValueType.U32, true),
                    // 1.5 hours as an IEEE-754 double, which the F64 field stores raw.
                    MetadataField(
                        0x1001,
                        "simulation_time_hours",
                        1.5.toBits().toLong(),
                        MetadataValueType.F64,
                        true
                    ),
                    MetadataField(0x1002, "cell_count", 3L, MetadataValueType.U64, true),
                    MetadataField(0x1003, "food_count", 4L, MetadataValueType.U64, true),
                    MetadataField(
                        0x1004,
                        "food_mass",
                        2.5.toBits().toLong(),
                        MetadataValueType.F64,
                        true
                    ),
                    MetadataField(
                        0x1005,
                        "dish_radius",
                        60.0.toBits().toLong(),
                        MetadataValueType.F64,
                        true
                    ),
                    MetadataField(0x1006, "max_cell_count", 4096L, MetadataValueType.U64, true),
                    MetadataField(0x1007, "max_food_count", 4096L, MetadataValueType.U64, true)
                )
            }
            val writer = LittleEndianWriter(1024)
            writer.u32(entries.size.toLong())
            entries.forEach { entry ->
                val name = entry.name.encodeToByteArray()
                writer.u32(entry.fieldId.toLong())
                writer.u8(entry.valueType.wireId)
                writer.u8(if (entry.critical) 1 else 0)
                writer.u16(name.size)
                writer.u32(entry.valueType.byteCount)
                writer.raw(name)
                if (entry.valueType.byteCount == 4) {
                    writer.u32(entry.rawBits.toInt())
                } else {
                    writer.u64(entry.rawBits)
                }
            }
            return writer.toByteArray()
        }

        fun crc32(bytes: ByteArray, endExclusive: Int = bytes.size): Int {
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

private data class MetadataEntry(val fieldId: Int, val name: String, val value: Long)

/** One metadata entry as the container stores it, including its schema-mandated flag. */
private data class MetadataField(
    val fieldId: Int,
    val name: String,
    val rawBits: Long,
    val valueType: MetadataValueType,
    val critical: Boolean
)

private enum class MetadataValueType(val wireId: Int, val byteCount: Int) {
    U32(1, 4),
    U64(2, 8),
    I64(3, 8),
    F64(4, 8)
}
/** Little-endian writer matching the release container, which stores fixed sizes. */
private class LittleEndianWriter(expectedBytes: Int) {
    private val bytes = ByteArrayOutputStream(expectedBytes)

    fun u8(value: Int) {
        bytes.write(value and 0xff)
    }

    fun u16(value: Int) {
        u8(value)
        u8(value ushr 8)
    }

    fun u32(value: Int) {
        u16(value)
        u16(value ushr 16)
    }

    fun u32(value: Long) {
        u32(value.toInt())
    }

    fun u64(value: Long) {
        u32(value.toInt())
        u32((value ushr 32).toInt())
    }

    fun raw(value: ByteArray) {
        bytes.write(value)
    }

    fun toByteArray(): ByteArray = bytes.toByteArray()
}
