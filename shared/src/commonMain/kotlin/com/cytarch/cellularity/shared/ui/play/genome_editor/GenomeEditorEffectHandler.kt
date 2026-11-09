/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.GenomeDraft

/** Where the genome the editor starts from comes from, when it is the one on the plate. */
interface GenomeProvider {
    val genome: GenomeDraft?
}

/**
 * The three file operations the editor needs, and nothing else.
 *
 * The editor is about a genome, not about the disk, so it is told these three things rather
 * than handed a repository that can list substrates and delete files. That is also what
 * makes it the same editor on both platforms: a phone satisfies this with its repository and
 * a desktop with its own, and neither has to be present for the handler to be tested.
 */
interface GenomeEditorFileGateway {
    suspend fun listGenomeNames(): StorageResult<List<String>>
    suspend fun readGenomeDraft(name: String): StorageResult<GenomeDraft>
    suspend fun writeGenomeDraft(name: String, genome: GenomeDraft): StorageResult<Unit>
}

/**
 * Carries out the file work the reducer asks for and answers with what the reducer expects.
 *
 * Effects are separated from the state machine on purpose: the reducer stays a pure function
 * that can be reasoned about and tested without a disk, and everything that can fail --
 * naming a file, finding it, writing it -- happens here and comes back as an action. That
 * also means the user's edit is never half-applied: the genome only becomes the loaded one
 * when a read succeeded.
 *
 * The failure mapping is the part worth reading twice. A storage error is about a file; the
 * editor needs to know what to say to a person, and the same underlying condition reads
 * differently depending on the direction. A file that is not there is a failed read and a
 * fine place to write; a name that already exists is a failed write and cannot happen on a
 * read.
 */
class GenomeEditorEffectHandler(
    private val fileGateway: GenomeEditorFileGateway,
    private val genomeProvider: GenomeProvider,
    private val defaultGenomeFactory: () -> GenomeDraft
) {
    suspend fun handle(effect: GenomeEditorEffect): GenomeEditorAction = when (effect) {
        GenomeEditorEffect.ListStoredGenomes -> listStoredGenomes()
        is GenomeEditorEffect.LoadGenome -> loadGenome(effect.choice)
        is GenomeEditorEffect.SaveGenome -> saveGenome(effect)
    }

    private suspend fun listStoredGenomes(): GenomeEditorAction =
        when (val result = fileGateway.listGenomeNames()) {
            is StorageResult.Success -> GenomeEditorAction.LoadChoicesReady(result.value)
            is StorageResult.Failure -> GenomeEditorAction.LoadFailed(
                GenomeFileFailureReason.READ_FAILED,
                result.error.detail
            )
        }

    private suspend fun loadGenome(choice: GenomeLoadChoice): GenomeEditorAction =
        when (choice) {
            GenomeLoadChoice.Microscope -> loadMicroscopeGenome()
            GenomeLoadChoice.DefaultGenome -> GenomeEditorAction.LoadSucceeded(
                genome = defaultGenomeFactory(),
                origin = GenomeLoadOrigin.DEFAULT_GENOME
            )
            is GenomeLoadChoice.Stored ->
                when (val result = fileGateway.readGenomeDraft(choice.name)) {
                    is StorageResult.Success -> GenomeEditorAction.LoadSucceeded(
                        genome = result.value,
                        origin = GenomeLoadOrigin.STORED_FILE,
                        name = choice.name
                    )
                    is StorageResult.Failure -> GenomeEditorAction.LoadFailed(
                        result.error.toGenomeReadFailure(),
                        result.error.detail
                    )
                }
        }

    private fun loadMicroscopeGenome(): GenomeEditorAction {
        val genome = genomeProvider.genome
            ?: return GenomeEditorAction.LoadFailed(
                GenomeFileFailureReason.NO_MICROSCOPE_CELL
            )
        return GenomeEditorAction.LoadSucceeded(
            genome = genome,
            origin = GenomeLoadOrigin.MICROSCOPE
        )
    }

    private suspend fun saveGenome(
        effect: GenomeEditorEffect.SaveGenome
    ): GenomeEditorAction = when (val result = fileGateway.writeGenomeDraft(
        effect.name,
        effect.snapshot
    )) {
        is StorageResult.Success -> GenomeEditorAction.SaveSucceeded(effect.name)
        is StorageResult.Failure -> GenomeEditorAction.SaveFailed(
            result.error.toGenomeWriteFailure(),
            result.error.detail
        )
    }
}

private fun StorageError.toGenomeReadFailure(): GenomeFileFailureReason = when (code) {
    StorageErrorCode.BAD_NAME -> GenomeFileFailureReason.BAD_NAME
    StorageErrorCode.NOT_FOUND -> GenomeFileFailureReason.NOT_FOUND
    StorageErrorCode.NEWER_VERSION -> GenomeFileFailureReason.NEWER_VERSION
    StorageErrorCode.TOO_LARGE -> GenomeFileFailureReason.TOO_LARGE
    StorageErrorCode.UNSUPPORTED_FORMAT,
    StorageErrorCode.CORRUPTED -> GenomeFileFailureReason.INVALID_GENOME
    StorageErrorCode.NAME_EXISTS,
    StorageErrorCode.PERMISSION_REVOKED,
    StorageErrorCode.IO_FAILURE -> GenomeFileFailureReason.READ_FAILED
}

private fun StorageError.toGenomeWriteFailure(): GenomeFileFailureReason = when (code) {
    StorageErrorCode.BAD_NAME -> GenomeFileFailureReason.BAD_NAME
    StorageErrorCode.TOO_LARGE -> GenomeFileFailureReason.TOO_LARGE
    StorageErrorCode.UNSUPPORTED_FORMAT,
    StorageErrorCode.CORRUPTED,
    StorageErrorCode.NEWER_VERSION -> GenomeFileFailureReason.INVALID_GENOME
    StorageErrorCode.NOT_FOUND,
    StorageErrorCode.NAME_EXISTS,
    StorageErrorCode.PERMISSION_REVOKED,
    StorageErrorCode.IO_FAILURE -> GenomeFileFailureReason.WRITE_FAILED
}
