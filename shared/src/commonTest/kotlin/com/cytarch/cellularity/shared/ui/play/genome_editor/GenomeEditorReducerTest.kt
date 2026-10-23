package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertNotSame
import kotlin.test.assertSame
import kotlin.test.assertTrue

class GenomeEditorReducerTest {
    @Test
    fun initialStateOwnsValidImmutablePreview() {
        val genome = testGenome()

        val state = GenomeEditorState.initial(genome)

        assertSame(genome, state.genome)
        assertSame(genome, state.preview.snapshot)
        assertEquals(GeneIndex(0), state.selectedGene)
        assertEquals(GenomeEditorSection.GENERAL, state.selectedSection)
        assertTrue(state.snapValues)
        assertFalse(state.dirty)
        assertTrue(state.validation.isValid)
        assertTrue(state.preview.sourceIsValid)
    }

    @Test
    fun typedEditsReplaceOnlySelectedGeneAndUpdatePreview() {
        var state = GenomeEditorState.initial(testGenome())
        state = reduce(state, GenomeEditorAction.SelectGene(GeneIndex(3)))
        val previousGenome = state.genome
        val previousGene = state.selectedGeneDraft
        val sibling = state.genome.genes[2]

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.NEUROCYTE))
        state = reduce(state, GenomeEditorAction.SetBoolean(GenomeBooleanField.STAY_ALIVE, false))
        state = reduce(
            state,
            GenomeEditorAction.SetChildGene(GenomeChild.FIRST, GeneIndex(7))
        )
        state = reduce(state, GenomeEditorAction.SetInteger(GeneIntegerId(2), 2))
        state = reduce(state, GenomeEditorAction.SetFloat(GeneFloatType.SMELL_RED, 0.25f))
        state = reduce(
            state,
            GenomeEditorAction.SetParametricValue(
                ParametricValueId(0),
                ParametricValueDraft(0.1f, 0.2f, 0.3f, 1, 2)
            )
        )

        assertNotSame(previousGenome, state.genome)
        assertNotSame(previousGene, state.selectedGeneDraft)
        assertSame(sibling, state.genome.genes[2])
        assertEquals(CellType.NEUROCYTE, state.selectedGeneDraft.cellType)
        assertFalse(state.selectedGeneDraft.stayAlive)
        assertEquals(7, state.selectedGeneDraft.child1GeneIndex)
        assertEquals(2, state.selectedGeneDraft.integerValues[2])
        assertEquals(0.25f, state.selectedGeneDraft.floatValues.smellRed)
        assertEquals(0.3f, state.selectedGeneDraft.parametricValues[0].variableC)
        assertTrue(state.dirty)
        assertTrue(state.validation.isValid)
        assertSame(state.genome, state.preview.snapshot)
    }

    @Test
    fun invalidEditIsVisibleButNeverReachesPreviewOrSaveEffect() {
        var state = GenomeEditorState.initial(testGenome())
        val lastValidPreview = state.preview.snapshot

        state = reduce(
            state,
            GenomeEditorAction.SetScalar(GenomeScalarField.SPLIT_MASS, Float.NaN)
        )

        assertFalse(state.validation.isValid)
        assertFalse(state.preview.sourceIsValid)
        assertSame(lastValidPreview, state.preview.snapshot)
        assertTrue(state.dirty)

        state = reduce(state, GenomeEditorAction.RequestSave)
        val transition = GenomeEditorReducer.reduce(state, GenomeEditorAction.ConfirmSave("invalid"))
        assertIs<GenomeFileState.Failed>(transition.state.fileState)
        assertEquals(GenomeFileFailureReason.INVALID_GENOME, transition.state.fileState.reason)
        assertTrue(transition.effects.isEmpty())
    }

    @Test
    fun divisionToggleUsesDiscreteNoSplitSentinel() {
        var state = GenomeEditorState.initial(testGenome())

        state = reduce(state, GenomeEditorAction.SetDivisionEnabled(false))

        assertEquals(CellDataConstants.MAX_SPLIT_MASS, state.selectedGeneDraft.splitMass)
        assertTrue(state.selectedGeneDraft.splitMass >= CellDataConstants.MAX_MASS.toFloat())

        state = reduce(state, GenomeEditorAction.SetDivisionEnabled(true))

        assertEquals(CellDataConstants.MAX_SPLITTABLE_MASS, state.selectedGeneDraft.splitMass)
        assertTrue(state.selectedGeneDraft.splitMass < CellDataConstants.MAX_MASS.toFloat())
        assertTrue(state.validation.isValid)
    }

    @Test
    fun splitMassSnappingKeepsNoSplitSeparateFromSplittableValues() {
        val gene = testGenome().genes.first()

        val noSplit = GenomeEditorMath.snapGene(
            gene.copy(splitMass = CellDataConstants.MAX_SPLIT_MASS)
        )
        val highestSplittable = GenomeEditorMath.snapGene(
            gene.copy(splitMass = CellDataConstants.MAX_SPLITTABLE_MASS)
        )

        assertEquals(CellDataConstants.MAX_SPLIT_MASS, noSplit.splitMass)
        assertEquals(CellDataConstants.MAX_SPLITTABLE_MASS, highestSplittable.splitMass)
    }

    @Test
    fun copyResetAndInitialActionsPreserveGenomeInvariants() {
        var state = GenomeEditorState.initial(testGenome())
        state = reduce(state, GenomeEditorAction.SelectGene(GeneIndex(4)))
        state = reduce(state, GenomeEditorAction.SetCellType(CellType.CILIOCYTE))
        state = reduce(state, GenomeEditorAction.CopyGene(GeneIndex(4), GeneIndex(0)))

        assertEquals(CellType.CILIOCYTE, state.genome.genes[0].cellType)
        assertTrue(state.genome.genes[0].initial)
        assertFalse(state.genome.genes[4].initial)

        val replacement = state.genome.genes[4].copy(cellType = CellType.NEUROCYTE, initial = true)
        state = reduce(state, GenomeEditorAction.ResetGene(GeneIndex(0), replacement))
        assertEquals(CellType.NEUROCYTE, state.genome.genes[0].cellType)
        assertTrue(state.genome.genes[0].initial)

        state = reduce(state, GenomeEditorAction.MakeInitial(GeneIndex(9)))
        assertEquals(1, state.genome.genes.count { it.initial })
        assertTrue(state.genome.genes[9].initial)
        assertTrue(state.validation.isValid)
    }

    @Test
    fun genesGrowAndShrinkOneAtATimeWhileReferencesKeepTheirMeaning() {
        val oneGeneGenome = GenomeDraft.editorDefaults { listOf(1.0f, 0.0f, 0.0f) }
        assertEquals(1, oneGeneGenome.genes.size)
        assertFailsWith<IllegalArgumentException> { oneGeneGenome.deleteGene(0, 0) }

        var state = GenomeEditorState.initial(oneGeneGenome)
        state = reduce(
            state,
            GenomeEditorAction.AddGene(
                after = GeneIndex(0),
                value = oneGeneGenome.genes[0].copy(initial = false)
            )
        )
        assertEquals(2, state.genome.genes.size)
        assertEquals(GeneIndex(1), state.selectedGene)

        state = reduce(state, GenomeEditorAction.DuplicateGene(GeneIndex(0)))
        assertEquals(3, state.genome.genes.size)
        assertEquals(GeneIndex(1), state.selectedGene)

        val referencesToMiddle = state.genome.genes.map { gene ->
            val integers = gene.integerValues.toMutableList().also { values ->
                values[GenomeContract.VIRUS_FROM_INTEGER_INDEX] = 1
                values[GenomeContract.VIRUS_TO_INTEGER_INDEX] = 1
            }
            gene.copy(
                child1GeneIndex = 1,
                child2GeneIndex = 1,
                integerValues = integers
            )
        }
        state = GenomeEditorState.initial(GenomeDraft(referencesToMiddle))
        state = reduce(state, GenomeEditorAction.SelectGene(GeneIndex(1)))
        state = reduce(
            state,
            GenomeEditorAction.DeleteGene(
                gene = GeneIndex(1),
                replacement = GeneIndex(2)
            )
        )

        assertEquals(2, state.genome.genes.size)
        assertEquals(GeneIndex(1), state.selectedGene)
        state.genome.genes.forEach { gene -> assertAllGeneReferences(gene, 1) }
        assertTrue(state.validation.isValid)

        state = reduce(
            state,
            GenomeEditorAction.MoveGene(from = GeneIndex(1), to = GeneIndex(0))
        )
        assertEquals(GeneIndex(0), state.selectedGene)
        state.genome.genes.forEach { gene -> assertAllGeneReferences(gene, 0) }
        assertEquals(1, state.genome.genes.count { it.initial })
        assertTrue(state.validation.isValid)
    }

    @Test
    fun childGeneCreationLinksNewGenesWithoutLeavingTheParent() {
        var state = GenomeEditorState.initial(testGenome())
        state = reduce(state, GenomeEditorAction.SelectGene(GeneIndex(3)))
        val originalSize = state.genome.genes.size

        state = reduce(
            state,
            GenomeEditorAction.AddChildGene(
                child = GenomeChild.FIRST,
                value = state.genome.genes[0].copy(
                    cellType = CellType.NEUROCYTE,
                    initial = false
                )
            )
        )

        assertEquals(originalSize + 1, state.genome.genes.size)
        assertEquals(GeneIndex(3), state.selectedGene)
        assertEquals(4, state.selectedGeneDraft.child1GeneIndex)
        assertEquals(3, state.selectedGeneDraft.child2GeneIndex)
        assertEquals(CellType.NEUROCYTE, state.genome.genes[4].cellType)

        state = reduce(
            state,
            GenomeEditorAction.AddChildGene(
                child = GenomeChild.SECOND,
                value = state.genome.genes[0].copy(
                    cellType = CellType.CILIOCYTE,
                    initial = false
                )
            )
        )

        assertEquals(originalSize + 2, state.genome.genes.size)
        assertEquals(GeneIndex(3), state.selectedGene)
        assertEquals(5, state.selectedGeneDraft.child1GeneIndex)
        assertEquals(4, state.selectedGeneDraft.child2GeneIndex)
        assertEquals(CellType.CILIOCYTE, state.genome.genes[4].cellType)
        assertEquals(CellType.NEUROCYTE, state.genome.genes[5].cellType)
        assertTrue(state.dirty)
        assertTrue(state.validation.isValid)
        assertSame(state.genome, state.preview.snapshot)
    }

    @Test
    fun fileActionsProduceBoundedOneShotEffectsAndCleanDirtyStateAfterSave() {
        var state = GenomeEditorState.initial(testGenome())
        state = reduce(state, GenomeEditorAction.SetCellType(CellType.NEUROCYTE))

        var transition = GenomeEditorReducer.reduce(state, GenomeEditorAction.RequestLoad)
        assertIs<GenomeFileState.Listing>(transition.state.fileState)
        assertEquals(listOf(GenomeEditorEffect.ListStoredGenomes), transition.effects)

        state = reduce(
            transition.state,
            GenomeEditorAction.LoadChoicesReady(listOf("alpha", "alpha", "beta"))
        )
        val choices = assertIs<GenomeFileState.Choosing>(state.fileState).choices
        assertEquals(4, choices.size)
        val stored = GenomeLoadChoice.Stored("alpha")
        transition = GenomeEditorReducer.reduce(state, GenomeEditorAction.ChooseLoad(stored))
        assertEquals(listOf(GenomeEditorEffect.LoadGenome(stored)), transition.effects)

        state = reduce(
            transition.state,
            GenomeEditorAction.LoadSucceeded(testGenome(), GenomeLoadOrigin.STORED_FILE, "alpha")
        )
        assertFalse(state.dirty)
        assertEquals("alpha", state.genomeName)

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.CILIOCYTE))
        state = reduce(state, GenomeEditorAction.RequestSave)
        transition = GenomeEditorReducer.reduce(state, GenomeEditorAction.ConfirmSave("updated"))
        val effect = assertIs<GenomeEditorEffect.SaveGenome>(transition.effects.single())
        val savedSnapshot = effect.snapshot
        state = reduce(transition.state, GenomeEditorAction.SetCellType(CellType.FLAGELLOCYTE))
        assertEquals(CellType.CILIOCYTE, savedSnapshot.genes[0].cellType)
        assertEquals(CellType.FLAGELLOCYTE, state.genome.genes[0].cellType)

        state = reduce(state, GenomeEditorAction.SaveSucceeded("updated"))
        assertTrue(state.dirty, "A stale completion must not clean a newer edit")
    }

    @Test
    fun successfulSaveCleansExactlyTheSnapshotBeingSaved() {
        var state = GenomeEditorState.initial(testGenome())
        state = reduce(state, GenomeEditorAction.SetCellType(CellType.CILIOCYTE))
        state = reduce(state, GenomeEditorAction.RequestSave)
        val transition = GenomeEditorReducer.reduce(state, GenomeEditorAction.ConfirmSave("saved"))

        state = reduce(transition.state, GenomeEditorAction.SaveSucceeded("saved"))

        assertFalse(state.dirty)
        assertEquals("saved", state.genomeName)
        assertIs<GenomeFileState.Saved>(state.fileState)
    }

    @Test
    fun previewControlsAreClampedAndDoNotDirtyGenome() {
        var state = GenomeEditorState.initial(testGenome())
        state = reduce(state, GenomeEditorAction.SetPreviewTime(999.0))
        state = reduce(
            state,
            GenomeEditorAction.SetPreviewOption(GenomePreviewOption.TREE, true)
        )
        state = reduce(state, GenomeEditorAction.SelectSection(GenomeEditorSection.SPECIFIC))

        assertEquals(GenomeEditorConfig.PREVIEW_MAX_TIME_HOURS, state.preview.timeHours)
        assertTrue(state.preview.treeEnabled)
        assertEquals(GenomeEditorSection.SPECIFIC, state.selectedSection)
        assertFalse(state.dirty)
    }

    private fun reduce(state: GenomeEditorState, action: GenomeEditorAction): GenomeEditorState =
        GenomeEditorReducer.reduce(state, action).state

    private fun assertAllGeneReferences(gene: com.cytarch.cellularity.shared.model.GeneDraft, index: Int) {
        assertEquals(index, gene.child1GeneIndex)
        assertEquals(index, gene.child2GeneIndex)
        assertEquals(index, gene.integerValues[GenomeContract.VIRUS_FROM_INTEGER_INDEX])
        assertEquals(index, gene.integerValues[GenomeContract.VIRUS_TO_INTEGER_INDEX])
    }

    private fun testGenome(): GenomeDraft {
        var genome = GenomeDraft.editorDefaults { listOf(1.0f, 0.0f, 0.0f) }
        repeat(11) {
            genome = genome.duplicateGene(genome.genes.lastIndex)
        }
        return genome
    }
}
