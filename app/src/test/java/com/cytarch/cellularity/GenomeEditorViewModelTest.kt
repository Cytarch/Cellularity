package com.cytarch.cellularity

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeInitialGeneCompatibility
import com.cytarch.cellularity.shared.model.GenomeMetadata
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffect
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeScalarField
import com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeEditorViewModel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomeEditorViewModelTest {
    @Test
    fun deterministicInitialGenomeHasCanonicalShapeAndOneInitialGene() {
        val viewModel = GenomeEditorViewModel(testGenome())
        val state = viewModel.state.value

        assertEquals(GenomeContract.MIN_GENE_COUNT, state.genome.genes.size)
        assertEquals(1, state.genome.genes.count { it.initial })
        state.genome.genes.forEachIndexed { index, gene ->
            assertEquals(index, gene.child1GeneIndex)
            assertEquals(index, gene.child2GeneIndex)
            assertEquals(CellType.PHAGOCYTE, gene.cellType)
            assertEquals(GenomeContract.PARAMETRIC_VALUE_COUNT, gene.parametricValues.size)
            assertEquals(GenomeContract.INTEGER_VALUE_COUNT, gene.integerValues.size)
        }
        assertEquals(GenomeMetadata(0), GenomeInitialGeneCompatibility.extract(state.genome))

        assertSame(state.genome, viewModel.applicableGenome)
    }

    @Test
    fun dispatchPublishesReducerStateWithoutMutableCompatibilityPath() {
        val viewModel = GenomeEditorViewModel(testGenome(4))
        val oldState = viewModel.state.value
        val oldSibling = oldState.genome.genes[1]

        viewModel.dispatch(GenomeEditorAction.SelectGene(GeneIndex(3)))
        viewModel.dispatch(GenomeEditorAction.SetCellType(CellType.NEUROCYTE))

        val current = viewModel.state.value
        assertNotSame(oldState, current)
        assertEquals(GeneIndex(3), current.selectedGene)
        assertEquals(CellType.NEUROCYTE, current.selectedGeneDraft.cellType)
        assertEquals(CellType.PHAGOCYTE, oldState.genome.genes[3].cellType)
        assertSame(oldSibling, current.genome.genes[1])
        assertTrue(current.dirty)
    }

    @Test
    fun invalidDraftNeverReplacesApplicablePreviewSnapshot() {
        val viewModel = GenomeEditorViewModel(testGenome())
        val applicable = viewModel.applicableGenome

        viewModel.dispatch(
            GenomeEditorAction.SetScalar(GenomeScalarField.SPLIT_RATIO, Float.NaN)
        )

        assertFalse(viewModel.state.value.validation.isValid)
        assertSame(applicable, viewModel.applicableGenome)
    }

    @Test
    fun oneShotFileEffectCarriesImmutableSnapshot() = runBlocking {
        val viewModel = GenomeEditorViewModel(testGenome())
        viewModel.dispatch(GenomeEditorAction.SetCellType(CellType.CILIOCYTE))
        viewModel.dispatch(GenomeEditorAction.RequestSave)
        viewModel.dispatch(GenomeEditorAction.ConfirmSave("test"))

        val effect = viewModel.effects.first() as GenomeEditorEffect.SaveGenome
        viewModel.dispatch(GenomeEditorAction.SetCellType(CellType.NEUROCYTE))

        assertEquals(CellType.CILIOCYTE, effect.snapshot.genes[0].cellType)
        assertEquals(CellType.NEUROCYTE, viewModel.state.value.genome.genes[0].cellType)
    }

    private fun testGenome(geneCount: Int = 1): GenomeDraft {
        var genome = GenomeDraft.editorDefaults { listOf(1.0f, 0.0f, 0.0f) }
        repeat(geneCount - 1) {
            genome = genome.duplicateGene(genome.genes.lastIndex)
        }
        return genome
    }
}
