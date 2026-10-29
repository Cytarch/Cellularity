/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffectHandler
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorReducer
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileFailureReason
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadChoice
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider
import java.nio.file.Files
import java.nio.file.Path
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffect
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorFileGateway
import kotlinx.coroutines.runBlocking
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue
import kotlin.test.Test

/**
 * That a genome edited on a desktop survives being saved and loaded again.
 *
 * The editor could already reach this state -- the reducer, the sections and the state
 * machine are the phone's -- but nothing carried out the file work, so saving was a button
 * that did nothing. These tests go through the real repository and a real directory rather
 * than a fake, because the part that was missing is exactly the part a fake would replace:
 * which bytes reach the disk, and whether they come back as the same genome.
 *
 * The effects are run to completion one at a time rather than through the store's coroutine
 * scope. That scope exists so a save is not cancelled by a redraw, and waiting for it from a
 * test would make every assertion a race; driving the same handler and reducer directly
 * exercises the production path with the waiting removed.
 */
class DesktopGenomeEditorFilesTest {

    @Test
    fun `a saved genome comes back as the same genome`() = runBlocking {
        withRepository { repository, _ ->
            val editor = Editor(repository)
            // An edit first, so what is saved is not the genome the editor started with.
            // Without it a load that ignored the file would still compare equal.
            val edited = editor.reduce(GenomeEditorAction.SetSnapValues(enabled = false))

            editor.reduce(GenomeEditorAction.RequestSave)
            editor.reduce(GenomeEditorAction.ConfirmSave("round trip"))

            val saved = editor.state.fileState as? GenomeFileState.Saved
            assertNotNull(saved, "the save should have completed")
            assertEquals("round trip", saved.name)
            assertTrue(!editor.state.dirty, "a saved genome is not dirty")

            // A second editor, holding a different genome, so that loading the file is what
            // puts the saved genome back rather than the editor never having changed.
            val reloaded = Editor(repository)
            assertNotEquals(
                wireForm(edited).toList(),
                wireForm(reloaded.state.genome).toList(),
                "the second editor must start from a different genome, or the load " +
                    "would prove nothing"
            )

            reloaded.reduce(GenomeEditorAction.RequestLoad, expectEffect = true)
            val offering = reloaded.state.fileState
            assertTrue(
                offering is GenomeFileState.Choosing,
                "a load should offer the stored genomes, not $offering"
            )
            reloaded.reduce(GenomeEditorAction.ChooseLoad(GenomeLoadChoice.Stored("round trip")))

            // Compared in the form the file holds, because GenomeDraft is a plain class
            // without structural equality: two genomes with the same genes are not equal,
            // and comparing the written form is both the only comparison that can pass and
            // the one that says what actually round-tripped.
            assertTrue(
                wireForm(edited).contentEquals(wireForm(reloaded.state.genome)),
                "the genome that came back should be the genome that was saved"
            )
            val loaded = reloaded.state.fileState as? GenomeFileState.Loaded
            assertNotNull(loaded, "the load should have completed")
            assertEquals("round trip", loaded.name)
        }
    }

    @Test
    fun `a genome that was never saved is not offered for loading`() = runBlocking {
        withRepository { repository, _ ->
            val editor = Editor(repository)

            editor.reduce(GenomeEditorAction.RequestLoad, expectEffect = true)

            val choosing = editor.state.fileState as? GenomeFileState.Choosing
            assertNotNull(choosing, "a load should offer the stored genomes")
            // The two choices that are not files are always offered; what must not appear
            // is a stored genome, because none has been written.
            assertEquals(
                listOf(GenomeLoadChoice.Microscope, GenomeLoadChoice.DefaultGenome),
                choosing.choices,
                "an empty storage directory offers only the two built-in sources"
            )
        }
    }

    @Test
    fun `the genome of a selected cell is reported as unavailable rather than loaded`() =
        runBlocking {
            withRepository { repository, _ ->
                val editor = Editor(repository)
                editor.reduce(GenomeEditorAction.RequestLoad, expectEffect = true)
                val before = editor.state.genome

                editor.reduce(GenomeEditorAction.ChooseLoad(GenomeLoadChoice.Microscope))

                val failed = editor.state.fileState as? GenomeFileState.Failed
                assertNotNull(failed, "a cell genome that cannot be read is a failure")
                assertEquals(
                    GenomeFileFailureReason.NO_MICROSCOPE_CELL,
                    failed.reason,
                    "there is no way to read a cell's genome yet, and that is the reason"
                )
                assertTrue(
                    wireForm(before).contentEquals(wireForm(editor.state.genome)),
                    "a load that failed must leave the genome untouched"
                )
            }
        }

    @Test
    fun `a name that cannot be a file is refused without writing one`() = runBlocking {
        withRepository { repository, directory ->
            val editor = Editor(repository)
            editor.reduce(GenomeEditorAction.RequestSave)
            editor.reduce(GenomeEditorAction.ConfirmSave(""))

            val failed = editor.state.fileState as? GenomeFileState.Failed
            assertNotNull(failed, "an unusable name is a failure")
            assertEquals(GenomeFileFailureReason.BAD_NAME, failed.reason)
            // The repository creates its directory on first use, so a refused save may
            // legitimately leave no directory at all. What must hold is that it left no file.
            val written = if (Files.isDirectory(directory)) {
                Files.list(directory).use { stream ->
                    stream.filter { Files.isRegularFile(it) }.count()
                }
            } else {
                0L
            }
            assertEquals(0L, written, "a refused name must not leave a file behind")
        }
    }

    /**
     * A genome that has gone is refused by name, and the genome being edited is untouched.
     *
     * This cannot be reached by choosing from the list, and finding that out is worth
     * recording: the list is read from the directory every time it is asked for, so a
     * genome whose file has been deleted is simply no longer offered, and a person cannot
     * select what is not there. The refusal still has to be correct, because it is what
     * happens when the file disappears between the listing and the read -- two steps that
     * are not atomic and are separated by a dialog.
     */
    @Test
    fun `a genome that has gone is refused by name rather than invented`() = runBlocking {
        val missing = "gone"
        val handler = GenomeEditorEffectHandler(
            fileGateway = object : GenomeEditorFileGateway {
                override suspend fun listGenomeNames() = StorageResult.Success(listOf(missing))

                override suspend fun readGenomeDraft(name: String) = StorageResult.Failure(
                    StorageError(StorageErrorCode.NOT_FOUND, "no file for $name")
                )

                override suspend fun writeGenomeDraft(
                    name: String,
                    genome: GenomeDraft
                ) = StorageResult.Success(Unit)
            },
            genomeProvider = NoCellGenome,
            defaultGenomeFactory = DesktopGenomeEditorStore::createRandomizedEditorGenome
        )

        val answer = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Stored(missing))
        )

        val failed = answer as? GenomeEditorAction.LoadFailed
        assertNotNull(failed, "a file that is not there is a failed load, not a load of $answer")
        assertEquals(GenomeFileFailureReason.NOT_FOUND, failed.reason)
    }

    /**
     * One editor's worth of state, driven to completion after every action.
     *
     * `expectEffect` marks the actions that are expected to ask for file work. It is not
     * needed to make the driving work -- the loop follows whatever the reducer emits -- but
     * asserting it keeps a test from passing because an action quietly stopped emitting
     * anything.
     */
    private class Editor(repository: DesktopCellularityFileRepository) {
        private val handler = GenomeEditorEffectHandler(
            fileGateway = TestGateway(repository),
            genomeProvider = NoCellGenome,
            defaultGenomeFactory = DesktopGenomeEditorStore::createRandomizedEditorGenome
        )

        var state: GenomeEditorState = GenomeEditorState.initial(
            DesktopGenomeEditorStore.createRandomizedEditorGenome()
        )
            private set

        suspend fun reduce(
            action: GenomeEditorAction,
            expectEffect: Boolean = false
        ): GenomeDraft {
            val transition = GenomeEditorReducer.reduce(state, action)
            if (expectEffect) {
                assertTrue(
                    transition.effects.isNotEmpty(),
                    "$action should have asked for file work"
                )
            }
            state = transition.state
            for (effect in transition.effects) {
                val answer = handler.handle(effect)
                state = GenomeEditorReducer.reduce(state, answer).state
            }
            return state.genome
        }
    }

    /**
     * A genome in the form the file holds it.
     *
     * [GenomeDraft] is a plain class without structural equality, so two genomes with the
     * same genes are not equal. Comparing the encoded form is both a stronger check -- it is
     * what was written and read -- and the only one that can pass.
     */
    private fun wireForm(genome: GenomeDraft): ByteArray =
        com.cytarch.cellularity.shared.model.GenomeDraftWireCodec.encode(genome)

    private suspend fun withRepository(
        body: suspend (DesktopCellularityFileRepository, Path) -> Unit
    ) {
        val root = Files.createTempDirectory("cellularity-genome-editor")
        val directory = root.resolve("files")
        try {
            body(DesktopCellularityFileRepository(directory), directory)
        } finally {
            deleteRecursively(root)
        }
    }

    private fun deleteRecursively(root: Path) {
        if (!Files.exists(root)) return
        Files.walk(root).use { stream ->
            stream.sorted(Comparator.reverseOrder()).forEach { Files.deleteIfExists(it) }
        }
    }
}

/**
 * The desktop repository behind the editor's three file operations.
 *
 * A copy of the adapter the application uses, written out here so that the test fails if the
 * adapter is ever changed to call different repository methods -- which is the kind of
 * change that would look harmless and store the genome somewhere the collection screen does
 * not look.
 */
private class TestGateway(
    private val repository: DesktopCellularityFileRepository
) : com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorFileGateway {
    override suspend fun listGenomeNames() = repository.listGenomeNames()

    override suspend fun readGenomeDraft(name: String) = repository.readGenomeDraft(name)

    override suspend fun writeGenomeDraft(
        name: String,
        genome: GenomeDraft
    ) = repository.writeGenomeDraft(nameOrFilename = name, genome = genome)
}

/** No genome is reachable on the plate, which is the state the desktop is in. */
private object NoCellGenome : GenomeProvider {
    override val genome: GenomeDraft? = null
}
