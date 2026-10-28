/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.cytarch.cellularity.shared.model.EditorGeneTemplate
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeMetadata
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorReducer
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
import kotlin.math.sqrt
import kotlin.random.Random

/**
 * The genome being edited.
 *
 * Every edit goes through the shared reducer, which is the same one the phone dispatches
 * to, so the rules about what a genome may be and which edits keep it valid are not
 * restated here. What this adds is only the part the phone got from its view model: a
 * place for the current state to live and a way to hand an action to the reducer.
 *
 * The effects the reducer emits are file operations and preview requests. Neither is
 * wired up yet, so they are collected and dropped rather than left to accumulate; a
 * channel nobody drains would grow for as long as the editor is open.
 */
class DesktopGenomeEditorStore(
    initialGenome: GenomeDraft = createRandomizedEditorGenome()
) {
    var state by mutableStateOf(GenomeEditorState.initial(initialGenome))
        private set

    /** Effects the reducer asked for and nothing has handled. */
    var unsupportedEffectCount by mutableStateOf(0)
        private set

    fun dispatch(action: GenomeEditorAction) {
        val transition = GenomeEditorReducer.reduce(state, action)
        state = transition.state
        unsupportedEffectCount += transition.effects.size
    }

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
