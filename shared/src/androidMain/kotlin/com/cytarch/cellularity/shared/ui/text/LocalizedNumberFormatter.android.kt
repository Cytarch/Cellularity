package com.cytarch.cellularity.shared.ui.text

import java.math.RoundingMode
import java.text.DecimalFormat
import java.text.DecimalFormatSymbols
import java.text.DateFormat
import java.text.NumberFormat
import java.util.Date
import java.util.Locale

actual fun formatLocalizedNumber(
    value: Double,
    minimumFractionDigits: Int,
    maximumFractionDigits: Int,
    useGrouping: Boolean
): String = localizedNumberFormat(
    minimumFractionDigits,
    maximumFractionDigits,
    useGrouping
).format(value)

actual fun localizeCanonicalNumber(value: String): String {
    val separator = DecimalFormatSymbols.getInstance(Locale.getDefault()).decimalSeparator
    return if (separator == '.') value else value.replace('.', separator)
}

actual fun formatLocalizedDate(unixMillis: Long): String =
    DateFormat.getDateInstance(DateFormat.SHORT, Locale.getDefault())
        .format(Date(unixMillis))

private fun localizedNumberFormat(
    minimumFractionDigits: Int,
    maximumFractionDigits: Int,
    useGrouping: Boolean
): NumberFormat = NumberFormat.getNumberInstance(Locale.getDefault()).apply {
    isGroupingUsed = useGrouping
    this.minimumFractionDigits = minimumFractionDigits
    this.maximumFractionDigits = maximumFractionDigits
    if (this is DecimalFormat) roundingMode = RoundingMode.HALF_UP
}
