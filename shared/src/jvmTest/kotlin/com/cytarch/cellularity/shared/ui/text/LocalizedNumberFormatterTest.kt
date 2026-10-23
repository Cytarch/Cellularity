package com.cytarch.cellularity.shared.ui.text

import java.util.Locale
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class LocalizedNumberFormatterTest {
    @Test
    fun presentationNumbersUseTheActiveLocaleAndOptionalGrouping() {
        val original = Locale.getDefault()
        try {
            Locale.setDefault(Locale.GERMANY)

            assertEquals(
                "1234,5",
                formatLocalizedNumber(
                    value = 1234.46,
                    minimumFractionDigits = 1,
                    maximumFractionDigits = 1
                )
            )
            assertEquals(
                "1.234,5",
                formatLocalizedNumber(
                    value = 1234.46,
                    minimumFractionDigits = 1,
                    maximumFractionDigits = 1,
                    useGrouping = true
                )
            )
            assertEquals("-1,25", localizeCanonicalNumber("-1.25"))
            assertTrue(formatLocalizedDate(0L).isNotBlank())
        } finally {
            Locale.setDefault(original)
        }
    }
}
