/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.files

import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * The desktop repository is the shared CellularityFileStore over the real native codec, so
 * these check that it behaves exactly as Android does with real files: release-format genomes
 * and substrates, original-game genomes, and refusal of what is not a Cellularity file.
 */
class DesktopSharedFileStoreTest {

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Before
    fun requireNativeLibrary() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
    }

    private fun storage(): Path = temporaryFolder.newFolder("storage").toPath()

    private fun <T> value(result: StorageResult<T>): T {
        assertTrue("expected success but was $result", result is StorageResult.Success)
        return (result as StorageResult.Success).value
    }

    private fun failure(result: StorageResult<*>): StorageErrorCode {
        assertTrue("expected failure but was $result", result is StorageResult.Failure)
        return (result as StorageResult.Failure).error.code
    }

    private fun repositoryRoot(): Path {
        var directory: Path? = Path.of("").toAbsolutePath()
        while (directory != null) {
            if (Files.isDirectory(directory.resolve("testdata/release-format"))) return directory
            directory = directory.parent
        }
        error("repository root not found")
    }

    private fun hexFixture(name: String): ByteArray {
        val text = Files.readString(repositoryRoot().resolve("testdata/release-format/$name"))
            .filter { !it.isWhitespace() }
        return ByteArray(text.length / 2) { index ->
            text.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private fun corpusFixture(name: String): ByteArray =
        Files.readAllBytes(repositoryRoot().resolve("native/tests/fixtures/corpus/$name"))

    private fun outside(name: String, bytes: ByteArray): Path =
        temporaryFolder.newFolder().toPath().resolve(name).also { Files.write(it, bytes) }

    private fun wire(genome: GenomeDraft): ByteArray = GenomeDraftWireCodec.encode(genome)

    private fun genome(): GenomeDraft =
        GenomeDraft.editorDefaults { listOf(0.3f, 0.75f, 0.3f) }

    @Test
    fun aSavedGenomeIsAReleaseContainerThatReadsBackUnchanged() = runBlocking {
        val directory = storage()
        val repository = DesktopCellularityFileRepository(directory)
        val written = genome()

        value(repository.writeGenomeDraft("Round trip", written))

        val stored = Files.readAllBytes(directory.resolve("Round trip.genome"))
        assertArrayEquals("CLTY".encodeToByteArray(), stored.copyOfRange(0, 4))
        assertArrayEquals(wire(written), wire(value(repository.readGenomeDraft("Round trip"))))
        val info = value(repository.readGenomeInfo(StoredGenomeId("Round trip.genome")))
        assertEquals(written.genes.size, info.geneCount)
        assertEquals(FileLoadCompatibility.LOADABLE, info.compatibility)
    }

    @Test
    fun aReleaseGenomeFromAnotherDeviceIsImportedAndLoaded() = runBlocking {
        val repository = DesktopCellularityFileRepository(storage())
        val source = outside("Golden.genome", hexFixture("genome-v1-golden.hex"))

        val resolved = value(repository.resolveExternalFile(source))
        assertEquals(DesktopExternalFileKind.GENOME, resolved.kind)
        value(repository.importExternalFile(resolved))

        assertTrue(value(repository.readGenomeDraft("Golden")).genes.isNotEmpty())
    }

    @Test
    fun anOriginalGameGenomeIsImportedAndLoaded() = runBlocking {
        val repository = DesktopCellularityFileRepository(storage())
        val source = outside("Legacy.genome", corpusFixture("legacy-genome-v103.bin"))

        value(repository.importExternalFile(value(repository.resolveExternalFile(source))))

        assertTrue(value(repository.readGenomeDraft("Legacy")).genes.isNotEmpty())
    }

    @Test
    fun aReleaseSubstrateIsImportedAndDescribed() = runBlocking {
        val repository = DesktopCellularityFileRepository(storage())
        val source = outside("Golden.substrate", hexFixture("substrate-v1-golden.hex"))

        val resolved = value(repository.resolveExternalFile(source))
        assertEquals(DesktopExternalFileKind.SUBSTRATE, resolved.kind)
        value(repository.importExternalFile(resolved))

        val info = value(repository.readSubstrateInfo(StoredSubstrateId("Golden")))
        assertEquals(FileLoadCompatibility.LOADABLE, info.compatibility)
        assertTrue((info.maximumCellCount ?: 0) > 0)
    }

    @Test
    fun aFileThatIsNotACellularityDocumentIsRefusedAndNotStored() = runBlocking {
        val directory = storage()
        val repository = DesktopCellularityFileRepository(directory)
        val source = outside("Notes.genome", "not a genome".encodeToByteArray())

        val code = failure(repository.importExternalFile(value(repository.resolveExternalFile(source))))

        assertEquals(StorageErrorCode.CORRUPTED, code)
        assertEquals(emptyList<String>(), value(repository.listGenomeNames()))
    }

    @Test
    fun anImportDoesNotReplaceAFileAlreadyStored() = runBlocking {
        val repository = DesktopCellularityFileRepository(storage())
        value(repository.writeGenomeDraft("Taken", genome()))
        val source = outside("Taken.genome", hexFixture("genome-v1-golden.hex"))

        val code = failure(repository.importExternalFile(value(repository.resolveExternalFile(source))))

        assertEquals(StorageErrorCode.NAME_EXISTS, code)
        assertArrayEquals(wire(genome()), wire(value(repository.readGenomeDraft("Taken"))))
    }

    @Test
    fun aSavedPlateAppearsOnlyWhenTheWriterProducedAValidSubstrate() = runBlocking {
        val directory = storage()
        val repository = DesktopCellularityFileRepository(directory)
        val golden = hexFixture("substrate-v1-golden.hex")

        value(repository.saveSubstrate(StoredSubstrateId("Plate"), replaceExisting = false) {
            Files.write(it, golden)
        })
        assertArrayEquals(golden, Files.readAllBytes(directory.resolve("Plate.substrate")))

        assertEquals(
            StorageErrorCode.NAME_EXISTS,
            failure(repository.saveSubstrate(StoredSubstrateId("Plate"), replaceExisting = false) {
                Files.write(it, golden)
            })
        )
        value(repository.saveSubstrate(StoredSubstrateId("Plate"), replaceExisting = true) {
            Files.write(it, golden)
        })

        assertEquals(
            StorageErrorCode.CORRUPTED,
            failure(repository.saveSubstrate(StoredSubstrateId("Broken"), replaceExisting = false) {
                Files.write(it, golden.copyOf(golden.size / 2))
            })
        )
        assertEquals(listOf(StoredSubstrateId("Plate")), value(repository.listSubstrateIds()))
    }

    @Test
    fun aGenomeSavedFromTheEditorCarriesItsPicture() = runBlocking {
        val roots = com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        // The preview session belongs to the thread that created it, as the editor's preview
        // surface owns its own: the session and every capture run on this one thread.
        val owner = java.util.concurrent.Executors.newSingleThreadExecutor()
        val handles = owner.submit<LongArray> {
            val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(16, 16)
            val session = if (anchor == 0L) 0L else DesktopNativeRenderer
                .nativeCreateGenomePreviewSession(
                    anchor, roots!!.shaderRoot.toString(), roots.textureRoot.toString(), 64
                )
            longArrayOf(anchor, session)
        }.get()
        val (anchor, session) = handles[0] to handles[1]
        try {
            assumeTrue("no window system for the preview's surface", anchor != 0L)
            assertTrue("the preview session did not start", session != 0L)
            val capture = com.cytarch.cellularity.desktop.DesktopGenomePreviewCapture()
            capture.attach { bytes, time, completion ->
                owner.execute {
                    val side = DesktopNativeRenderer.nativeGenomePreviewPixels()
                    val pixels = java.nio.ByteBuffer.allocateDirect(side * side * 4)
                    val status = DesktopNativeRenderer.nativeCaptureGenomePreview(
                        session, bytes, time, pixels, side * 4
                    )
                    completion.complete(
                        if (status != 0) null else {
                            val rgba = ByteArray(side * side * 4).also { pixels.get(it) }
                            com.cytarch.cellularity.desktop.CapturedGenomePreview(rgba, side, side)
                        }
                    )
                }
                true
            }
            val repository = DesktopCellularityFileRepository(storage())
            val editorGenome = com.cytarch.cellularity.shared.ui.play.genome_editor
                .GenomeEditorDefaults.createRandomizedEditorGenome()

            value(repository.writeGenomeDraftWithPreview("Pictured", editorGenome) { wire ->
                capture.capture(wire, 48.0)?.let {
                    DesktopSubstratePreviewEncoder.encodeGenomePng(it.rgba, it.pixelWidth, it.pixelHeight)
                }
            })

            val info = value(repository.readGenomeInfo(StoredGenomeId("Pictured.genome")))
            assertTrue("the saved genome has no usable picture", info.previewAvailable)
            assertTrue(repository.readGenomePreview(StoredGenomeId("Pictured.genome")) != null)
        } finally {
            owner.submit {
                if (session != 0L) DesktopNativeRenderer.nativeStopSession(session)
                if (anchor != 0L) DesktopNativeRenderer.nativeDestroyWindow(anchor)
            }.get()
            owner.shutdown()
        }
    }
}
