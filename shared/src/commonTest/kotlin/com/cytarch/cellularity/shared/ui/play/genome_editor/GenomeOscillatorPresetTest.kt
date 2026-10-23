package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotSame
import kotlin.test.assertTrue

class GenomeOscillatorPresetTest {
    @Test
    fun unconfiguredGeneUsesTheLegacyDefaultSelection() {
        val configuration = GenomeOscillatorPreset.configurationFor(testGenome().genes[0])

        assertEquals(1, configuration.adhesinConnections)
        assertEquals(0, configuration.firstChannel)
        assertEquals(1, configuration.secondChannel)
        assertEquals(GenomeOscillatorPreset.DEFAULT_PERIOD_HOURS, configuration.periodHours)
    }

    @Test
    fun applyingPresetUpdatesExactlyTheFourNeuralOutputs() {
        val source = testGenome().genes[0].copy(cellType = CellType.NEUROCYTE)
        val configuration = GenomeOscillatorPresetConfiguration(
            adhesinConnections = 3,
            firstChannel = 2,
            secondChannel = 0,
            periodHours = 4.0f
        )

        val result = GenomeOscillatorPreset.applyTo(source, configuration)

        assertNotSame(source, result)
        assertEquals(
            listOf(2, 0, 2, 0),
            result.integerValues.subList(
                GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX,
                GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + 4
            )
        )
        val outputs = result.parametricValues.subList(
            GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX,
            GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 4
        )
        assertEquals(listOf(0, 2, 2, 0), outputs.map {
            it.signalSubstanceChannelIndex.toInt()
        })
        assertEquals(listOf<Short>(1, 1, 1, 1), outputs.map { it.formulaType })
        assertEquals(listOf(0.0f, 0.0f, 0.0f, 0.0f), outputs.map { it.variableB })
        assertTrue(outputs[0].variableA > 0.0f)
        assertEquals(-outputs[0].variableA, outputs[1].variableA)
        assertEquals(outputs[2].variableA, outputs[3].variableA)
        assertEquals(source.parametricValues[0], result.parametricValues[0])
        assertEquals(source.integerValues[0], result.integerValues[0])
    }

    @Test
    fun generatedPresetCanBeReadBackWithinOneSliderStep() {
        val requested = GenomeOscillatorPresetConfiguration(
            adhesinConnections = 2,
            firstChannel = 3,
            secondChannel = 1,
            periodHours = 6.25f
        )
        val result = GenomeOscillatorPreset.applyTo(testGenome().genes[0], requested)

        val restored = GenomeOscillatorPreset.configurationFor(result)

        assertEquals(requested.adhesinConnections, restored.adhesinConnections)
        assertEquals(requested.firstChannel, restored.firstChannel)
        assertEquals(requested.secondChannel, restored.secondChannel)
        val sliderStep = (GenomeOscillatorPreset.MAX_PERIOD_HOURS -
            GenomeOscillatorPreset.MIN_PERIOD_HOURS) / GenomeEditorConfig.OSCILLATOR_PRESET_STEPS
        assertTrue(abs(requested.periodHours - restored.periodHours) <= sliderStep)
    }

    @Test
    fun reducerAppliesPresetAsOneImmutableValidatedEdit() {
        val initial = GenomeEditorState.initial(testGenome())
        val configuration = GenomeOscillatorPresetConfiguration(0, 1, 3, 1.5f)

        val result = GenomeEditorReducer.reduce(
            initial,
            GenomeEditorAction.ApplyOscillatorPreset(configuration)
        ).state

        assertNotSame(initial.genome, result.genome)
        assertEquals(1, result.selectedGeneDraft.integerValues[5])
        assertEquals(3, result.selectedGeneDraft.integerValues[6])
        assertTrue(result.dirty)
        assertTrue(result.validation.isValid)
        assertEquals(result.genome, result.preview.snapshot)
    }

    private fun testGenome(): GenomeDraft = GenomeDraft.editorDefaults {
        listOf(1.0f, 0.0f, 0.0f)
    }
}
