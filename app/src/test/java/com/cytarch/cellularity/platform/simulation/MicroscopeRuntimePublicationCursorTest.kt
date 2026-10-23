package com.cytarch.cellularity.platform.simulation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopeRuntimePublicationCursorTest {
    @Test
    fun controllerReplacementResetsEveryNativePublicationCursor() {
        val cursor = MicroscopeRuntimePublicationCursor(4L)
        cursor.runtimeRevision = 91L
        cursor.worldEpoch = 13L
        cursor.environmentRevision = 27L

        assertFalse(cursor.resetIfSourceChanged(4L))
        assertEquals(91L, cursor.runtimeRevision)

        assertTrue(cursor.resetIfSourceChanged(5L))
        assertEquals(5L, cursor.sourceGeneration)
        assertEquals(0L, cursor.runtimeRevision)
        assertEquals(0L, cursor.worldEpoch)
        assertEquals(Long.MIN_VALUE, cursor.environmentRevision)
    }
}
