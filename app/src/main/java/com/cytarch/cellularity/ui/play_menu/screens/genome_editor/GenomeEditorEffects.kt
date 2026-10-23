package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.render.preview.GenomePreviewCaptureCoordinator
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffect
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileFailureReason
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadChoice
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadOrigin

interface GenomeProvider {
    val genome: GenomeDraft?
}

internal interface GenomeEditorFileGateway {
    suspend fun listGenomeNames(): StorageResult<List<String>>
    suspend fun readGenomeDraft(name: String): StorageResult<GenomeDraft>
    suspend fun writeGenomeDraft(name: String, genome: GenomeDraft): StorageResult<Unit>
}

private class AndroidGenomeEditorFileGateway(
    private val repository: AndroidCellularityFileRepository,
    private val previewCapture: GenomePreviewCaptureCoordinator
) : GenomeEditorFileGateway {
    override suspend fun listGenomeNames(): StorageResult<List<String>> =
        repository.listGenomeNames()

    override suspend fun readGenomeDraft(name: String): StorageResult<GenomeDraft> =
        repository.readGenomeDraft(name)

    override suspend fun writeGenomeDraft(
        name: String,
        genome: GenomeDraft
    ): StorageResult<Unit> = repository.writeGenomeDraftWithPreview(
        nameOrFilename = name,
        genome = genome,
        previewCapture = previewCapture,
        maximumPreviewTime = GenomeEditorConfig.PREVIEW_MAX_TIME_HOURS
    )
}

/** Executes Android storage/runtime effects without exposing them to reducer or section UI. */
class GenomeEditorEffectHandler internal constructor(
    private val fileGateway: GenomeEditorFileGateway,
    private val genomeProvider: GenomeProvider,
    private val defaultGenomeFactory: () -> GenomeDraft =
        GenomeEditorViewModel::createRandomizedEditorGenome
) {
    internal constructor(
        fileRepository: AndroidCellularityFileRepository,
        genomeProvider: GenomeProvider,
        previewCapture: GenomePreviewCaptureCoordinator,
        defaultGenomeFactory: () -> GenomeDraft =
            GenomeEditorViewModel::createRandomizedEditorGenome
    ) : this(
        AndroidGenomeEditorFileGateway(fileRepository, previewCapture),
        genomeProvider,
        defaultGenomeFactory
    )

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
            is GenomeLoadChoice.Stored -> when (val result = fileGateway.readGenomeDraft(choice.name)) {
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

/** Retains Android preview resources and transient layout; editor data lives in [GenomeEditorViewModel]. */
class GenomeEditorCoordinator(
    fileRepository: AndroidCellularityFileRepository,
    genomeProvider: GenomeProvider
) {
    private val previewCapture = GenomePreviewCaptureCoordinator()
    val effectHandler = GenomeEditorEffectHandler(
        fileRepository,
        genomeProvider,
        previewCapture
    )

    private var viewportState: FloatArray? = null
    private var previewHeightDp: Float? = null
    private var previewWidthDp: Float? = null

    fun restoreViewport(): FloatArray? = viewportState?.copyOf()

    fun rememberViewport(value: FloatArray) {
        if (value.size >= 3) viewportState = value.copyOf()
    }

    fun restorePreviewHeightDp(): Float? = previewHeightDp

    fun rememberPreviewHeightDp(value: Float) {
        if (value.isFinite() && value > 0.0f) previewHeightDp = value
    }

    fun restorePreviewWidthDp(): Float? = previewWidthDp

    fun rememberPreviewWidthDp(value: Float) {
        if (value.isFinite() && value > 0.0f) previewWidthDp = value
    }

    fun attachPreview(view: GenomePreviewView) {
        previewCapture.attach(view.previewCaptureEndpoint())
    }

    fun releasePreview(view: GenomePreviewView) {
        previewCapture.detach(view.previewCaptureEndpoint())
        view.currentViewportState()?.let(::rememberViewport)
        view.release()
    }
}
