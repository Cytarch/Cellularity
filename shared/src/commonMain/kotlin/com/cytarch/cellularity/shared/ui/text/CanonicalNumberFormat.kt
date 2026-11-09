/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.ui.text

import kotlin.math.abs
import kotlin.math.round

/**
 * A physical quantity written to a fixed number of places, with a dot.
 *
 * This exists because the interface has two kinds of number and only one of them is
 * [formatLocalizedNumber]. A user-facing count may group its thousands and follow the
 * machine's decimal separator, because nobody types it back. A quantity in a text field
 * may not: the same box that shows the text parses it, and a parser that had to accept
 * whatever the locale writes would need a second implementation of the number grammar in
 * every field, with the failure mode being a value that is displayed as 2,520000 and
 * rejected the moment it is edited. A text field is a view of one number, so the number has
 * one spelling.
 *
 * The fixed places are part of the same decision rather than a separate style choice. Six
 * places is the resolution these quantities are edited at, so a shorter form would lose the
 * end of the value on the round trip through the field even though nothing was typed.
 *
 * Written out rather than delegated to a platform formatter: every formatter the platforms
 * offer is locale-aware, and reaching one from common code would put a platform dependency
 * in the shared module for the sake of padding a string.
 */
fun formatCanonicalNumber(value: Double, fractionDigits: Int): String {
    require(fractionDigits in 0..12) { "A number can be written to 0..12 places" }
    if (!value.isFinite()) return value.toString()

    val scale = POWERS_OF_TEN[fractionDigits]
    val negative = value < 0.0
    val scaled = round(abs(value) * scale)
    val whole = (scaled / scale).toLong()
    val fraction = (scaled - whole * scale).toLong()

    return buildString {
        if (negative) append('-')
        append(whole)
        if (fractionDigits == 0) return@buildString
        append('.')
        val digits = fraction.toString()
        repeat(fractionDigits - digits.length) { append('0') }
        append(digits)
    }
}

/** Six places, which is the resolution the interface's quantities are edited at. */
fun formatCanonicalQuantity(value: Double): String = formatCanonicalNumber(value, 6)

private val POWERS_OF_TEN = doubleArrayOf(
    1.0,
    10.0,
    100.0,
    1_000.0,
    10_000.0,
    100_000.0,
    1_000_000.0,
    10_000_000.0,
    100_000_000.0,
    1_000_000_000.0,
    10_000_000_000.0,
    100_000_000_000.0,
    1_000_000_000_000.0
)
