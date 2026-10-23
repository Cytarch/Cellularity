package com.cytarch.cellularity

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HeadTailTextWriterTest {
    @Test
    fun preservesCompleteTextAtTheLimit() {
        val writer = HeadTailTextWriter(characterLimit = 10, truncationMarker = "<cut>")

        writer.write("01234")
        writer.write("56789")

        assertEquals("0123456789", writer.result())
    }

    @Test
    fun preservesBeginningAndEndWhenMiddleIsTruncated() {
        val writer = HeadTailTextWriter(characterLimit = 10, truncationMarker = "<cut>")

        writer.write("0123456789ABCDE")

        assertEquals("01234<cut>ABCDE", writer.result())
    }

    @Test
    fun keepsTheDeepestCauseAtTheEndOfAChunkedTrace() {
        val writer = HeadTailTextWriter(characterLimit = 32, truncationMarker = "<cut>")
        val trace = "outer failure\n" + "middle frame\n".repeat(8) + "Caused by: root failure\n"

        trace.chunked(7).forEach(writer::write)

        val result = writer.result()
        assertTrue(result.startsWith("outer failure"))
        assertTrue(result.contains("<cut>"))
        assertTrue(result.endsWith("root failure\n"))
    }
}
