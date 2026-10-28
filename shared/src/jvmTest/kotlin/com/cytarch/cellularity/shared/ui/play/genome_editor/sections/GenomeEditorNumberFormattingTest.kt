/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.play.genome_editor.sections

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * Pins the one number formatter in the editor, because it replaced a platform call.
 *
 * These fields show and accept the same text, and the text is a physical quantity with a
 * fixed number of places rather than a user-facing number. So the cases that matter are
 * the ones a locale-aware formatter would have answered differently: the separator, the
 * absence of grouping, and the exact count of places including the trailing zeros.
 */
class GenomeEditorNumberFormattingTest {

    @Test
    fun sixPlacesAreAlwaysWrittenIncludingTrailingZeros() {
        assertEquals("2.520000", formatCanonicalGenomeNumber(2.52))
        assertEquals("0.000000", formatCanonicalGenomeNumber(0.0))
        assertEquals("50.000000", formatCanonicalGenomeNumber(50.0))
        assertEquals("1.000000", formatCanonicalGenomeNumber(1.0))
    }

    @Test
    fun theSeparatorIsAlwaysADotAndThereIsNoGrouping() {
        val large = formatCanonicalGenomeNumber(1234567.5)
        assertEquals("1234567.500000", large)
        assertTrue('.' in large, "the decimal separator must be a dot: $large")
        assertTrue(',' !in large, "there must be no grouping separator: $large")
    }

    @Test
    fun negativeValuesKeepTheirSign() {
        assertEquals("-2.520000", formatCanonicalGenomeNumber(-2.52))
        assertEquals("-0.500000", formatCanonicalGenomeNumber(-0.5))
    }

    @Test
    fun roundingIsToSixPlacesAndNotTruncation() {
        // 1e-7 above a representable value must round up, not be dropped.
        assertEquals("0.000000", formatCanonicalGenomeNumber(0.0000004))
        assertEquals("0.000001", formatCanonicalGenomeNumber(0.0000006))
        assertEquals("1.234568", formatCanonicalGenomeNumber(1.2345675))
    }

    @Test
    fun theDisplayedTextAndTheTypedTextAreTheSameText() {
        // The whole point of the formatter: what is shown can be typed back and read the
        // same way, on a machine whose locale writes a comma.
        val values = listOf(0.0, 2.52, 50.0, 1.234567, 1234567.5, -2.52)
        values.forEach { value ->
            assertEquals(
                formatCanonicalGenomeNumber(value),
                formatGenomeSliderValue(value),
                "the slider display and the canonical form disagree for $value"
            )
            assertEquals(
                formatGenomeSliderValue(value),
                formatGenomeSliderInput(value),
                "the display and the input disagree for $value"
            )
        }
    }

    @Test
    fun nonFiniteValuesAreNamedRatherThanRenderedAsGarbage() {
        assertEquals("NaN", formatCanonicalGenomeNumber(Double.NaN))
        assertEquals("Infinity", formatCanonicalGenomeNumber(Double.POSITIVE_INFINITY))
        assertEquals("-Infinity", formatCanonicalGenomeNumber(Double.NEGATIVE_INFINITY))
    }

    @Test
    fun anglesCarryTheirUnitAndUseTheSamePlaces() {
        assertEquals("90.000000°", formatGenomeAngleValue(90.0))
        assertEquals("90.000000", formatGenomeAngleInput(90.0))
    }
}
