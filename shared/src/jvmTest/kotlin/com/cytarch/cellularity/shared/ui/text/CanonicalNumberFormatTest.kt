/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.text

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/**
 * Pins the formatter that the interface's quantities are read back through.
 *
 * The fields that use it show and accept the same text, and that text is a physical
 * quantity with a fixed number of places rather than a user-facing number. So the cases
 * that matter are the ones a locale-aware formatter would have answered differently: the
 * separator, the absence of grouping, and the exact count of places including the trailing
 * zeros. The last one is not cosmetic -- a field that showed fewer places than it accepts
 * would round the value on the round trip without anyone typing anything.
 */
class CanonicalNumberFormatTest {

    @Test
    fun sixPlacesAreAlwaysWrittenIncludingTrailingZeros() {
        assertEquals("2.520000", formatCanonicalQuantity(2.52))
        assertEquals("0.000000", formatCanonicalQuantity(0.0))
        assertEquals("50.000000", formatCanonicalQuantity(50.0))
        assertEquals("1.000000", formatCanonicalQuantity(1.0))
    }

    @Test
    fun theSeparatorIsAlwaysADotAndThereIsNoGrouping() {
        val large = formatCanonicalQuantity(1234567.5)
        assertEquals("1234567.500000", large)
        assertTrue('.' in large, "the decimal separator must be a dot: $large")
        assertTrue(',' !in large, "there must be no grouping separator: $large")
    }

    @Test
    fun negativeValuesKeepTheirSign() {
        assertEquals("-2.520000", formatCanonicalQuantity(-2.52))
        assertEquals("-0.500000", formatCanonicalQuantity(-0.5))
    }

    @Test
    fun roundingIsToSixPlacesAndNotTruncation() {
        // 1e-7 above a representable value must round up, not be dropped.
        assertEquals("0.000000", formatCanonicalQuantity(0.0000004))
        assertEquals("0.000001", formatCanonicalQuantity(0.0000006))
        assertEquals("1.234568", formatCanonicalQuantity(1.2345675))
    }

    @Test
    fun everyValueSurvivesBeingWrittenAndReadBack() {
        // What is written has to be what the field accepts, on any machine. Parsing the
        // text back is the only way to establish that rather than assert it.
        val values = listOf(0.0, 2.52, 50.0, 1.234567, 1234567.5, -2.52, 1e-6)
        values.forEach { value ->
            val written = formatCanonicalQuantity(value)
            val readBack = written.toDoubleOrNull()
            assertTrue(
                readBack != null,
                "the field's own text is not a number the field can read: $written"
            )
            assertEquals(
                value,
                readBack!!,
                5e-7,
                "a value changed by being displayed and read back: $value -> $written"
            )
        }
    }

    @Test
    fun nonFiniteValuesAreNamedRatherThanRenderedAsGarbage() {
        assertEquals("NaN", formatCanonicalQuantity(Double.NaN))
        assertEquals("Infinity", formatCanonicalQuantity(Double.POSITIVE_INFINITY))
        assertEquals("-Infinity", formatCanonicalQuantity(Double.NEGATIVE_INFINITY))
    }

    @Test
    fun theNumberOfPlacesIsPartOfTheRequest() {
        assertEquals("0.50", formatCanonicalNumber(0.5, 2))
        assertEquals("0", formatCanonicalNumber(0.5, 0))
        assertEquals("1", formatCanonicalNumber(0.6, 0))
        assertEquals("0.000000000001", formatCanonicalNumber(1e-12, 12))
    }

    @Test
    fun anImpossibleNumberOfPlacesIsRefusedRatherThanGuessed() {
        assertFailsWith<IllegalArgumentException> { formatCanonicalNumber(1.0, -1) }
        assertFailsWith<IllegalArgumentException> { formatCanonicalNumber(1.0, 13) }
    }
}
