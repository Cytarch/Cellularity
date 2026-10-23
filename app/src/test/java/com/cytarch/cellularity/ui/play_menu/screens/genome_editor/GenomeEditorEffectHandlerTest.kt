package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffect
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileFailureReason
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadChoice
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadOrigin
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

class GenomeEditorEffectHandlerTest {
    @Test
    fun listAndEveryLoadSourceReturnTypedActions() = runBlocking {
        val stored = genome(0.0f, 1.0f, 0.0f)
        val microscope = genome(0.0f, 0.0f, 1.0f)
        val fallback = genome(1.0f, 0.0f, 0.0f)
        val files = FakeGenomeEditorFileGateway(
            names = listOf("alpha", "beta"),
            storedGenome = stored
        )
        val handler = handler(files, microscope, fallback)

        val listed = handler.handle(GenomeEditorEffect.ListStoredGenomes)
            as GenomeEditorAction.LoadChoicesReady
        assertEquals(listOf("alpha", "beta"), listed.storedNames)

        val fromFile = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Stored("alpha"))
        ) as GenomeEditorAction.LoadSucceeded
        assertSame(stored, fromFile.genome)
        assertEquals(GenomeLoadOrigin.STORED_FILE, fromFile.origin)
        assertEquals("alpha", fromFile.name)

        val fromMicroscope = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Microscope)
        ) as GenomeEditorAction.LoadSucceeded
        assertSame(microscope, fromMicroscope.genome)
        assertEquals(GenomeLoadOrigin.MICROSCOPE, fromMicroscope.origin)

        val fromDefault = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.DefaultGenome)
        ) as GenomeEditorAction.LoadSucceeded
        assertSame(fallback, fromDefault.genome)
        assertEquals(GenomeLoadOrigin.DEFAULT_GENOME, fromDefault.origin)
    }

    @Test
    fun saveWritesTheExactImmutableEffectSnapshot() = runBlocking {
        val files = FakeGenomeEditorFileGateway()
        val snapshot = genome(1.0f, 0.0f, 0.0f)
        val handler = handler(files)

        val result = handler.handle(GenomeEditorEffect.SaveGenome("stable", snapshot))

        assertEquals(GenomeEditorAction.SaveSucceeded("stable"), result)
        assertEquals("stable", files.savedName)
        assertSame(snapshot, files.savedGenome)
    }

    @Test
    fun missingMicroscopeSelectionAndStorageFailuresStayTyped() = runBlocking {
        val files = FakeGenomeEditorFileGateway()
        val handler = handler(files)

        val noCell = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Microscope)
        ) as GenomeEditorAction.LoadFailed
        assertEquals(GenomeFileFailureReason.NO_MICROSCOPE_CELL, noCell.reason)

        files.readFailure = StorageError(StorageErrorCode.NOT_FOUND, "gone")
        val missing = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Stored("gone"))
        ) as GenomeEditorAction.LoadFailed
        assertEquals(GenomeFileFailureReason.NOT_FOUND, missing.reason)

        files.readFailure = StorageError(StorageErrorCode.TOO_LARGE)
        val tooLarge = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Stored("large"))
        ) as GenomeEditorAction.LoadFailed
        assertEquals(GenomeFileFailureReason.TOO_LARGE, tooLarge.reason)

        files.readFailure = StorageError(StorageErrorCode.NEWER_VERSION, "newer")
        val newer = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Stored("future"))
        ) as GenomeEditorAction.LoadFailed
        assertEquals(GenomeFileFailureReason.NEWER_VERSION, newer.reason)

        files.readFailure = StorageError(StorageErrorCode.CORRUPTED, "invalid")
        val invalid = handler.handle(
            GenomeEditorEffect.LoadGenome(GenomeLoadChoice.Stored("broken"))
        ) as GenomeEditorAction.LoadFailed
        assertEquals(GenomeFileFailureReason.INVALID_GENOME, invalid.reason)
    }

    @Test
    fun writeFailuresAreSeparatedFromBadNamesAndLimits() = runBlocking {
        val files = FakeGenomeEditorFileGateway()
        val handler = handler(files)
        val effect = GenomeEditorEffect.SaveGenome("name", genome(1.0f, 0.0f, 0.0f))

        files.writeFailure = StorageError(StorageErrorCode.BAD_NAME, "name")
        assertEquals(
            GenomeFileFailureReason.BAD_NAME,
            (handler.handle(effect) as GenomeEditorAction.SaveFailed).reason
        )
        files.writeFailure = StorageError(StorageErrorCode.TOO_LARGE)
        assertEquals(
            GenomeFileFailureReason.TOO_LARGE,
            (handler.handle(effect) as GenomeEditorAction.SaveFailed).reason
        )
        files.writeFailure = StorageError(StorageErrorCode.IO_FAILURE, "disk")
        assertEquals(
            GenomeFileFailureReason.WRITE_FAILED,
            (handler.handle(effect) as GenomeEditorAction.SaveFailed).reason
        )
    }

    private fun handler(
        files: FakeGenomeEditorFileGateway,
        microscope: GenomeDraft? = null,
        fallback: GenomeDraft = genome(1.0f, 0.0f, 0.0f)
    ): GenomeEditorEffectHandler = GenomeEditorEffectHandler(
        fileGateway = files,
        genomeProvider = object : GenomeProvider {
            override val genome: GenomeDraft? = microscope
        },
        defaultGenomeFactory = { fallback }
    )

    private fun genome(red: Float, green: Float, blue: Float): GenomeDraft =
        GenomeDraft.editorDefaults { listOf(red, green, blue) }

    private class FakeGenomeEditorFileGateway(
        private val names: List<String> = emptyList(),
        private val storedGenome: GenomeDraft = GenomeDraft.editorDefaults {
            listOf(1.0f, 0.0f, 0.0f)
        }
    ) : GenomeEditorFileGateway {
        var readFailure: StorageError? = null
        var writeFailure: StorageError? = null
        var savedName: String? = null
        var savedGenome: GenomeDraft? = null

        override suspend fun listGenomeNames(): StorageResult<List<String>> =
            StorageResult.Success(names)

        override suspend fun readGenomeDraft(name: String): StorageResult<GenomeDraft> =
            readFailure?.let { StorageResult.Failure(it) }
                ?: StorageResult.Success(storedGenome)

        override suspend fun writeGenomeDraft(
            name: String,
            genome: GenomeDraft
        ): StorageResult<Unit> {
            writeFailure?.let { return StorageResult.Failure(it) }
            savedName = name
            savedGenome = genome
            return StorageResult.Success(Unit)
        }
    }
}
