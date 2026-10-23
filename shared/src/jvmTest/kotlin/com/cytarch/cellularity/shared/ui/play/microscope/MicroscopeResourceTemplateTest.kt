package com.cytarch.cellularity.shared.ui.play.microscope

import androidx.compose.ui.text.AnnotatedString
import kotlin.test.Test
import kotlin.test.assertEquals

class MicroscopeResourceTemplateTest {
    @Test
    fun annotatedArgumentsCanBeReorderedAndRepeated() {
        val first = "\uFDD00\uFDD1"
        val second = "\uFDD01\uFDD1"

        assertEquals(
            "second / first / second",
            interpolateMicroscopeTemplate(
                formatted = "$second / $first / $second",
                markers = listOf(first, second),
                arguments = listOf(AnnotatedString("first"), AnnotatedString("second"))
            ).text
        )
    }
}
