package com.cytarch.cellularity.shared.ui.text

/** Formats presentation-only numbers without leaking a JVM locale API into common code. */
expect fun formatLocalizedNumber(
    value: Double,
    minimumFractionDigits: Int,
    maximumFractionDigits: Int,
    useGrouping: Boolean = false
): String

/** Applies the active locale's decimal separator to an already rounded canonical number. */
expect fun localizeCanonicalNumber(value: String): String

/** Formats a saved-at instant as a compact calendar date in the active locale. */
expect fun formatLocalizedDate(unixMillis: Long): String
