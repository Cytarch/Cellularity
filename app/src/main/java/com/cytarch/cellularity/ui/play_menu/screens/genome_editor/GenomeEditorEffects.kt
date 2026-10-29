package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.render.preview.GenomePreviewCaptureCoordinator
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffectHandler
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorFileGateway
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider

/**
 * The phone's answer to the editor's three file operations.
 *
 * The editor itself is shared, so what stays here is only what is Android about storing a
 * genome: the repository that writes into the app's own storage, and the preview capture
 * that a saved genome carries with it. Saving on the phone is therefore a write plus a
 * render, which is why this is not simply a pass-through to the repository.
 */
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

/** Retains Android preview resources and transient layout; editor data lives in [GenomeEditorViewModel]. */
class GenomeEditorCoordinator(
    fileRepository: AndroidCellularityFileRepository,
    genomeProvider: GenomeProvider
) {
    private val previewCapture = GenomePreviewCaptureCoordinator()
    val effectHandler = GenomeEditorEffectHandler(
        fileGateway = AndroidGenomeEditorFileGateway(fileRepository, previewCapture),
        genomeProvider = genomeProvider,
        defaultGenomeFactory = com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeEditorViewModel::createRandomizedEditorGenome
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
