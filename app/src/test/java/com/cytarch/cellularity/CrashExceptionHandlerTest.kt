package com.cytarch.cellularity

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CrashExceptionHandlerTest {
    @Test
    fun failedReportWriteStillDelegatesExactlyOnce() {
        var delegated = 0
        var terminated = false
        val handler = CrashExceptionHandler(
            previousHandler = Thread.UncaughtExceptionHandler { _, _ -> delegated++ },
            reportWriter = { _, _ -> throw AssertionError("storage unavailable") },
            terminateProcess = { terminated = true }
        )

        handler.uncaughtException(Thread.currentThread(), IllegalStateException("test"))

        assertEquals(1, delegated)
        assertFalse(terminated)
    }

    @Test
    fun recursiveEntryDoesNotRunReportWriterAgain() {
        var writes = 0
        var delegated = 0
        var terminated = false
        lateinit var handler: CrashExceptionHandler
        handler = CrashExceptionHandler(
            previousHandler = Thread.UncaughtExceptionHandler { _, _ -> delegated++ },
            reportWriter = { thread, throwable ->
                writes++
                handler.uncaughtException(thread, throwable)
            },
            terminateProcess = { terminated = true }
        )

        handler.uncaughtException(Thread.currentThread(), IllegalStateException("test"))

        assertEquals(1, writes)
        assertEquals(1, delegated)
        assertTrue(terminated)
    }

    @Test
    fun missingOrBrokenDelegateTerminatesDeterministically() {
        val terminationCodes = mutableListOf<Int>()
        val noDelegate = CrashExceptionHandler(
            previousHandler = null,
            reportWriter = { _, _ -> },
            terminateProcess = terminationCodes::add
        )
        val brokenDelegate = CrashExceptionHandler(
            previousHandler = Thread.UncaughtExceptionHandler { _, _ -> error("broken delegate") },
            reportWriter = { _, _ -> },
            terminateProcess = terminationCodes::add
        )

        noDelegate.uncaughtException(Thread.currentThread(), IllegalStateException("first"))
        brokenDelegate.uncaughtException(Thread.currentThread(), IllegalStateException("second"))

        assertEquals(listOf(10, 10), terminationCodes)
        assertTrue(terminationCodes.all { it == 10 })
    }
}
