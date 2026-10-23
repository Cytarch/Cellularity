package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomeEditorUnitPresentationTest {
    @Test
    fun displayBoundariesDiscardOnlyFloatRepresentationNoise() {
        assertEquals(
            0.648,
            canonicalizeGenomeEditorDisplayBoundary(
                scaleGenomeEditorValue(
                    CellDataConstants.MIN_SPLIT_MASS.toDouble(),
                    NANOGRAMS_PER_INTERNAL_MASS_UNIT
                )
            ),
            0.0
        )
        assertEquals(
            3.672,
            canonicalizeGenomeEditorDisplayBoundary(
                scaleGenomeEditorValue(
                    CellDataConstants.MAX_SPLIT_MASS.toDouble(),
                    NANOGRAMS_PER_INTERNAL_MASS_UNIT
                )
            ),
            0.0
        )
        assertEquals(
            90.0,
            canonicalizeGenomeEditorDisplayBoundary(
                scaleGenomeEditorValue(
                    (1.0f - CellDataConstants.MIN_RATIO).toDouble(),
                    PERCENT_PER_FRACTION
                )
            ),
            0.0
        )
        assertEquals(
            10.0,
            canonicalizeGenomeEditorDisplayBoundary(
                scaleGenomeEditorValue(
                    GenomeContract.floatRange(GeneFloatType.ADHESIN_LENGTH).max.toDouble(),
                    MICROMETRES_PER_MILLIMETRE
                )
            ),
            0.0
        )
        assertEquals(
            350.0,
            canonicalizeGenomeEditorDisplayBoundary(
                scaleGenomeEditorValue(35.0, NANOGRAMS_PER_INTERNAL_STIFFNESS_UNIT)
            ),
            0.0
        )
    }

    @Test
    fun scaledUnitsRoundTripWithoutReducingEditorPrecision() {
        val valuesAndScales = listOf(
            0.252 to NANOGRAMS_PER_INTERNAL_MASS_UNIT,
            17.123456 to NANOGRAMS_PER_INTERNAL_STIFFNESS_UNIT,
            0.37512345 to PERCENT_PER_FRACTION,
            0.006123456 to MICROMETRES_PER_MILLIMETRE
        )

        valuesAndScales.forEach { (value, scale) ->
            assertEquals(
                value,
                unscaleGenomeEditorValue(scaleGenomeEditorValue(value, scale), scale),
                1e-12
            )
        }
    }

    @Test
    fun unitFormattingKeepsSixVisibleFractionDigits() {
        assertTrue(formatGenomeUnitValue(2.52, "ng", true).endsWith("2.520000 ng"))
        assertTrue(formatGenomeUnitValue(50.0, "%", false).endsWith("50.000000%"))
        assertEquals("ng/h²", formatGenomeAngularStiffnessUnit("ng", "h"))
        assertEquals(
            "\u043d\u0433/\u0447²",
            formatGenomeAngularStiffnessUnit("\u043d\u0433", "\u0447")
        )
    }

    @Test
    fun splitMassDisplayMaximumNeverRoundsIntoNoSplitState() {
        val maximumDisplayValue = floorGenomeEditorDisplayBoundary(
            scaleGenomeEditorValue(
                CellDataConstants.MAX_SPLITTABLE_MASS.toDouble(),
                NANOGRAMS_PER_INTERNAL_MASS_UNIT
            )
        )

        assertEquals(3.599999, maximumDisplayValue, 0.0)
        assertTrue(
            unscaleGenomeEditorValue(
                maximumDisplayValue,
                NANOGRAMS_PER_INTERNAL_MASS_UNIT
            ) < CellDataConstants.MAX_MASS
        )
        assertEquals("3.599999", formatGenomeSliderInput(maximumDisplayValue))
    }
}
