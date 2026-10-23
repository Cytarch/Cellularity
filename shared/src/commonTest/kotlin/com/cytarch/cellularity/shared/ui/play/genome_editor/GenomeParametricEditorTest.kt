package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.ParametricValueDraft
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class GenomeParametricEditorTest {
    @Test
    fun decodeAndEncodeRoundTripEveryFormula() {
        val values = listOf(
            ParametricValueDraft(0.0f, 0.25f, 0.0f, 2, 0),
            ParametricValueDraft(1.5f, -0.5f, 0.2f, 2, 1),
            ParametricValueDraft(-0.5f, 0.75f, -0.2f, 2, 2),
            ParametricValueDraft(0.5f, -2.0f, 3.0f, 2, 3)
        )

        values.forEach { wireValue ->
            val editorValue = GenomeParametricEditor.decode(wireValue, -3.0f, 1.0f)
            val encoded = GenomeParametricEditor.encode(
                wireValue.formulaType.toInt(),
                wireValue.signalSubstanceChannelIndex.toInt(),
                editorValue,
                -3.0f,
                1.0f
            )

            assertEquals(wireValue, encoded)
        }
    }

    @Test
    fun boundsUsePhysicalUnitsForLinearAndHeavisideModes() {
        val linear = GenomeParametricEditor.bounds(1, -3.0f, 1.0f)
        val heaviside = GenomeParametricEditor.bounds(2, -3.0f, 1.0f)

        assertEquals(-16.0f, linear.variableA.min)
        assertEquals(16.0f, linear.variableA.max)
        assertEquals(-17.0f, linear.variableB.min)
        assertEquals(15.0f, linear.variableB.max)
        assertEquals(-3.0f, heaviside.variableA.min)
        assertEquals(1.0f, heaviside.variableA.max)
        assertEquals(-3.0f, heaviside.variableB.min)
        assertEquals(1.0f, heaviside.variableB.max)
    }

    @Test
    fun previewEvaluationReturnsPhysicalRangeValues() {
        val values = GenomeParametricEditorValues(
            fixedValue = 0.0f,
            variableA = -3.0f,
            variableB = 1.0f,
            variableC = 0.5f
        )

        assertEquals(-3.0f, GenomeParametricEditor.evaluatePhysical(2, 0.25f, values, -3.0f, 1.0f))
        assertEquals(1.0f, GenomeParametricEditor.evaluatePhysical(2, 0.75f, values, -3.0f, 1.0f))
        val sineValue = GenomeParametricEditor.evaluatePhysical(
            3,
            0.5f,
            values.copy(variableA = 1.0f, variableB = 0.0f, variableC = 3.14f),
            -3.0f,
            1.0f
        )
        assertTrue(sineValue in -3.0f..1.0f)
    }
}
