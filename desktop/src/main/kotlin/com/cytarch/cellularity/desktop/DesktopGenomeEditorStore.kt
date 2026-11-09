/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorDefaults
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffectHandler
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorFileGateway
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorReducer
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider
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

    companion object {
        fun createRandomizedEditorGene(geneIndex: Int): GeneDraft =
            GenomeEditorDefaults.createRandomizedEditorGene(geneIndex)

        fun createRandomizedEditorGenome(): GenomeDraft =
            GenomeEditorDefaults.createRandomizedEditorGenome()
    }
}
