package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class GenomeEditorSectionsTest {
    @Test
    fun navigationKeepsTheEstablishedThreeTabsInOrder() {
        assertEquals(
            listOf(
                GenomeEditorSection.GENERAL,
                GenomeEditorSection.SPLITTING,
                GenomeEditorSection.SPECIFIC
            ),
            GenomeEditorSection.entries
        )
    }

    @Test
    fun sectionSlicesContainOnlyTheirEstablishedFields() {
        val state = GenomeEditorState.initial(testGenome())
        val overview = GenomeEditorSections.overview(state)
        val structure = GenomeEditorSections.structure(state)
        val children = GenomeEditorSections.childRules(state)
        val signals = GenomeEditorSections.signals(state)

        assertEquals(GenomeContract.COLOR_COUNT, overview.color.size)
        assertEquals(GenomeContract.MAX_CONNECTION_COUNT_INDEX, GenomeIntegerField.MAX_CONNECTIONS.id.value)
        assertEquals(CellDataConstants.NEUROCYTE_OUTPUT_COUNT, signals.neuralOutputs.size)
        assertEquals(GeneIndex(0), children.first.targetGene)
        assertEquals(GeneIndex(0), children.second.targetGene)
        assertEquals(state.genome.genes.size, children.genes.options.size)
        assertEquals(GeneIndex(0), children.genes.options.first().id)
        assertEquals(1.0f, structure.cytoskeleton)
        assertFalse(overview.color is MutableList<*>)
        assertFalse(children.genes.options is MutableList<*>)
        assertFalse(children.genes.options.first().color is MutableList<*>)
    }

    @Test
    fun signalVisibilityAndCellSpecificParametersFollowCellType() {
        var state = GenomeEditorState.initial(testGenome())

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.SENSEOCYTE))
        assertTrue(GenomeEditorSections.signals(state).showsSensing)

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.SECROCYTE))
        assertTrue(GenomeEditorSections.signals(state).showsSecretion)

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.NEUROCYTE))
        assertTrue(GenomeEditorSections.signals(state).showsNeuralOutputs)

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.MYOCYTE))
        assertEquals(
            listOf(
                GenomeCellSpecificParameter.MYOCYTE_CONTRACTION,
                GenomeCellSpecificParameter.MYOCYTE_BENDING,
                GenomeCellSpecificParameter.MYOCYTE_LIFTING
            ),
            GenomeEditorSections.cellSpecific(state).parameters
        )

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.PHAGOCYTE))
        assertTrue(GenomeEditorSections.cellSpecific(state).parameters.isEmpty())
        assertFalse(GenomeEditorSections.signals(state).hasVisibleControls)
    }

    @Test
    fun divisionVisibilityFollowsTheToggleAndCellType() {
        var state = GenomeEditorState.initial(testGenome())

        var structure = GenomeEditorSections.structure(state)
        assertTrue(structure.supportsDivision)
        assertTrue(structure.divisionEnabled)

        state = reduce(state, GenomeEditorAction.SetDivisionEnabled(false))
        structure = GenomeEditorSections.structure(state)
        assertTrue(structure.supportsDivision)
        assertFalse(structure.divisionEnabled)

        state = reduce(state, GenomeEditorAction.SetDivisionEnabled(true))
        state = reduce(state, GenomeEditorAction.SetCellType(CellType.GAMETE))
        structure = GenomeEditorSections.structure(state)
        assertFalse(structure.supportsDivision)
        assertFalse(structure.divisionEnabled)

        state = reduce(state, GenomeEditorAction.SetCellType(CellType.PHAGOCYTE))
        structure = GenomeEditorSections.structure(state)
        assertTrue(structure.supportsDivision)
        assertTrue(structure.divisionEnabled)
    }

    @Test
    fun sensingColorFilterIsVisibleOnlyForCellRecognition() {
        var state = reduce(
            GenomeEditorState.initial(testGenome()),
            GenomeEditorAction.SetCellType(CellType.SENSEOCYTE)
        )

        repeat(CellDataConstants.SMELL_TYPE_COUNT) { smellType ->
            state = reduce(
                state,
                GenomeEditorAction.SetInteger(GenomeIntegerField.SMELL_TYPE.id, smellType)
            )
            assertEquals(
                smellType == CellDataConstants.SMELL_TYPE_CELL,
                GenomeEditorSections.signals(state).showsCellColorFilter,
                "smellType=$smellType"
            )
        }
    }

    @Test
    fun everyCellTypeExposesOnlyItsEstablishedSpecificParameters() {
        val expected = mapOf(
            CellType.PHAGOCYTE to emptyList(),
            CellType.FLAGELLOCYTE to listOf(GenomeCellSpecificParameter.SWIM_FORCE),
            CellType.PHOTOCYTE to emptyList(),
            CellType.DEVOROCYTE to emptyList(),
            CellType.LIPOCYTE to emptyList(),
            CellType.KERATINOCYTE to emptyList(),
            CellType.BUOYOCYTE to listOf(GenomeCellSpecificParameter.DENSITY),
            CellType.GLUEOCYTE to emptyList(),
            CellType.VIROCYTE to listOf(
                GenomeCellSpecificParameter.VIRUS_COPY_FROM,
                GenomeCellSpecificParameter.VIRUS_COPY_TO
            ),
            CellType.NITROCYTE to emptyList(),
            CellType.STEREOCYTE to emptyList(),
            CellType.SENSEOCYTE to emptyList(),
            CellType.MYOCYTE to listOf(
                GenomeCellSpecificParameter.MYOCYTE_CONTRACTION,
                GenomeCellSpecificParameter.MYOCYTE_BENDING,
                GenomeCellSpecificParameter.MYOCYTE_LIFTING
            ),
            CellType.NEUROCYTE to emptyList(),
            CellType.SECROCYTE to emptyList(),
            CellType.STEMOCYTE to listOf(
                GenomeCellSpecificParameter.STEM_MODE_1,
                GenomeCellSpecificParameter.STEM_THRESHOLD_1,
                GenomeCellSpecificParameter.STEM_MODE_2,
                GenomeCellSpecificParameter.STEM_THRESHOLD_2
            ),
            CellType.GAMETE to listOf(
                GenomeCellSpecificParameter.GAMETE_COMPATIBLE_MODE,
                GenomeCellSpecificParameter.GAMETE_MODE_AFTER_FERTILIZATION,
                GenomeCellSpecificParameter.GAMETE_TELOMERASE_LENGTH
            ),
            CellType.CILIOCYTE to listOf(
                GenomeCellSpecificParameter.CILIOCYTE_LEFT_SPEED,
                GenomeCellSpecificParameter.CILIOCYTE_RIGHT_SPEED
            )
        )
        val initial = GenomeEditorState.initial(testGenome())

        CellType.entries.forEach { cellType ->
            val state = reduce(initial, GenomeEditorAction.SetCellType(cellType))
            assertEquals(
                expected.getValue(cellType),
                GenomeEditorSections.cellSpecific(state).parameters,
                cellType.name
            )
        }
    }

    @Test
    fun reusedParametricSlotsKeepCellSpecificMeaning() {
        val initial = GenomeEditorState.initial(testGenome())
        val myocyte = GenomeEditorSections.cellSpecific(
            reduce(initial, GenomeEditorAction.SetCellType(CellType.MYOCYTE))
        )
        val ciliocyte = GenomeEditorSections.cellSpecific(
            reduce(initial, GenomeEditorAction.SetCellType(CellType.CILIOCYTE))
        )

        assertEquals(
            listOf(
                GenomeContract.MYOCYTE_CONTRACTION_VALUE_INDEX,
                GenomeContract.MYOCYTE_BENDING_VALUE_INDEX,
                GenomeContract.MYOCYTE_LIFTING_VALUE_INDEX
            ),
            myocyte.parametricIds()
        )
        assertEquals(
            listOf(
                GenomeContract.MYOCYTE_BENDING_VALUE_INDEX,
                GenomeContract.MYOCYTE_LIFTING_VALUE_INDEX
            ),
            ciliocyte.parametricIds()
        )
        assertEquals(
            listOf(
                GenomeCellSpecificParameter.CILIOCYTE_LEFT_SPEED,
                GenomeCellSpecificParameter.CILIOCYTE_RIGHT_SPEED
            ),
            ciliocyte.parameters
        )
    }

    private fun reduce(state: GenomeEditorState, action: GenomeEditorAction): GenomeEditorState =
        GenomeEditorReducer.reduce(state, action).state

    private fun GenomeCellSpecificSectionState.parametricIds(): List<Int> =
        values.filterIsInstance<GenomeCellSpecificValue.Parametric>().map { it.value.id.value }

    private fun testGenome(): GenomeDraft = GenomeDraft.editorDefaults {
        listOf(1.0f, 0.0f, 0.0f)
    }
}
