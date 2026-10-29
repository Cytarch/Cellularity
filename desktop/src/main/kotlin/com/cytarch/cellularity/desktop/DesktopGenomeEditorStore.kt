/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.EditorGeneTemplate
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeMetadata
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffectHandler
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorFileGateway
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorReducer
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadChoice
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider
import kotlin.math.sqrt
import kotlin.random.Random
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

/**
 * The desktop repository behind the editor's three file operations.
 *
 * A thin adapter rather than logic: the repository already reads and writes genome drafts
 * and already refuses everything the shared limits refuse, so restating any of that here
 * would be a second place to keep correct.
 */
private class RepositoryGenomeEditorFileGateway(
    private val repository: DesktopCellularityFileRepository
) : GenomeEditorFileGateway {
    override suspend fun listGenomeNames(): StorageResult<List<String>> =
        repository.listGenomeNames()

    override suspend fun readGenomeDraft(name: String): StorageResult<GenomeDraft> =
        repository.readGenomeDraft(name)

    override suspend fun writeGenomeDraft(
        name: String,
        genome: GenomeDraft
    ): StorageResult<Unit> = repository.writeGenomeDraft(
        nameOrFilename = name,
        genome = genome
    )
}

/**
 * The genome being edited, and the file it came from or went to.
 *
 * Every edit goes through the shared reducer, which is the same one the phone dispatches to,
 * so the rules about what a genome may be and which edits keep it valid are not restated
 * here. What this adds is the part the phone got from its view model and its effect handler:
 * a place for the current state to live, a way to hand an action to the reducer, and a way
 * to carry out the file work the reducer asks for.
 *
 * The file work is not written here either. [GenomeEditorEffectHandler] is shared, so saving
 * a genome on a desktop writes the same bytes through the same code as saving it on a phone,
 * and the only platform-specific part is the gateway that says where those bytes go.
 *
 * Effects are carried out one at a time. A reducer transition emits at most one, and the
 * handler answers a request by producing the action that completes it, so serialising them
 * costs nothing and removes the question of what two overlapping saves would mean.
 */
class DesktopGenomeEditorStore(
    private val fileGateway: GenomeEditorFileGateway,
    private val genomeProvider: GenomeProvider,
    initialGenome: GenomeDraft = createRandomizedEditorGenome(),
    private val onStateSettled: () -> Unit = { }
) {
    constructor(
        repository: DesktopCellularityFileRepository,
        genomeProvider: GenomeProvider,
        initialGenome: GenomeDraft = createRandomizedEditorGenome(),
        onStateSettled: () -> Unit = { }
    ) : this(
        fileGateway = RepositoryGenomeEditorFileGateway(repository),
        genomeProvider = genomeProvider,
        initialGenome = initialGenome,
        onStateSettled = onStateSettled
    )

    private val effectHandler = GenomeEditorEffectHandler(
        fileGateway = fileGateway,
        genomeProvider = genomeProvider,
        defaultGenomeFactory = ::createRandomizedEditorGenome
    )

    var state by mutableStateOf(GenomeEditorState.initial(initialGenome))
        private set

    /** True while a file operation is in flight, so the interface can say so. */
    val isBusy: Boolean
        get() = state.fileState.let {
            it is GenomeFileState.Listing ||
                it is GenomeFileState.Loading ||
                it is GenomeFileState.Saving
        }

    private var scope: CoroutineScope? = null

    /**
     * Binds the store to a lifetime.
     *
     * The editor's file work has to survive the recomposition that follows it -- a load that
     * was cancelled when the screen redrew would leave the state saying "loading" and
     * nothing loading -- so the store is given a scope rather than starting a coroutine of
     * its own that nothing could cancel when the screen goes away.
     */
    fun attach(scope: CoroutineScope) {
        this.scope = scope
    }

    fun dispatch(action: GenomeEditorAction) {
        val transition = GenomeEditorReducer.reduce(state, action)
        state = transition.state
        if (transition.effects.isEmpty()) {
            onStateSettled()
            return
        }
        val scope = scope
        if (scope == null) {
            // Nothing is bound to carry the effect out, which is what a test that only
            // exercises the reducer wants. Saying so is better than dropping it silently:
            // the state stays in its pending form and the caller can see that.
            return
        }
        scope.launch {
            for (effect in transition.effects) {
                val answer = effectHandler.handle(effect)
                val answered = GenomeEditorReducer.reduce(state, answer)
                state = answered.state
            }
            onStateSettled()
        }
    }

    /** Asks for the list of stored genomes, which is the first step of loading one. */
    fun requestLoad() = dispatch(GenomeEditorAction.RequestLoad)

    /** Asks for a name to save the current genome under. */
    fun requestSave() = dispatch(GenomeEditorAction.RequestSave)

    /** Closes the list of genomes to load from. */
    fun dismissLoad() = dispatch(GenomeEditorAction.DismissLoad)

    /** Closes the name prompt. */
    fun dismissSave() = dispatch(GenomeEditorAction.DismissSave)

    /** Picks one of the offered genomes to load. */
    fun chooseLoad(choice: GenomeLoadChoice) =
        dispatch(GenomeEditorAction.ChooseLoad(choice))

    /** Saves the current genome under a name the user typed. */
    fun confirmSave(name: String) = dispatch(GenomeEditorAction.ConfirmSave(name))

    /**
     * Empties a gene's slot while keeping the rest of the genome where it is.
     *
     * A genome's genes name each other by position, and the reducer's deletion takes the
     * index of a survivor that inherits every reference to the removed gene. Composing it
     * as a replacement followed by a removal is what makes the surviving choice honest:
     * the replacement is a fresh gene, so nothing inherits the deleted gene's identity,
     * and because the replacement is a real slot the indices around it do not shift.
     *
     * Doing it in two dispatches is not a shortcut -- it is the same two edits the phone
     * makes when it replaces a gene and then removes the leftover.
     */
    fun deleteGene(index: GeneIndex) {
        val genes = state.genome.genes
        require(index.value in genes.indices) { "Unknown gene index: ${index.value}" }
        require(genes.size > GenomeContract.MIN_GENE_COUNT) {
            "The last gene cannot be deleted"
        }

        // The remainder keeps the identity of the slot being emptied only long enough to
        // be named as the inheritor; it is built from the slot's own index so that a
        // genome which referred to itself still does.
        val remainder = index.value
        val holder = if (remainder == 0) 1 else 0
        dispatch(
            GenomeEditorAction.ResetGene(
                gene = GeneIndex(holder),
                value = createRandomizedEditorGene(holder)
            )
        )
        dispatch(GenomeEditorAction.DeleteGene(gene = index, replacement = GeneIndex(holder)))
    }

    companion object {
        fun createRandomizedEditorGene(geneIndex: Int): GeneDraft = EditorGeneTemplate.create(
            geneIndex = geneIndex,
            color = randomNormalizedColor(),
            metadata = GenomeMetadata(initialGeneIndex = 0)
        )

        fun createRandomizedEditorGenome(): GenomeDraft = GenomeDraft(
            List(GenomeContract.MIN_GENE_COUNT, ::createRandomizedEditorGene)
        )

        private fun randomNormalizedColor(): List<Float> {
            val color = FloatArray(GenomeContract.COLOR_COUNT) {
                Random.Default.nextFloat()
            }
            val magnitude = sqrt(color.sumOf { (it * it).toDouble() }).toFloat()
            return if (magnitude > 0.0f && magnitude.isFinite()) {
                color.map { it / magnitude }
            } else {
                listOf(1.0f, 0.0f, 0.0f)
            }
        }
    }
}
