package com.cytarch.cellularity

import java.io.File
import java.nio.file.Files
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class CrashRecoveryStoreTest {
    private lateinit var root: File
    private lateinit var stateFile: File
    private lateinit var store: CrashRecoveryStore

    @Before
    fun setUp() {
        root = Files.createTempDirectory("cellularity-recovery-test").toFile()
        stateFile = File(root, "diagnostics/${CrashRecoveryStore.STATE_FILE_NAME}")
        store = CrashRecoveryStore(stateFile)
    }

    @After
    fun tearDown() {
        root.deleteRecursively()
    }

    @Test
    fun reportsArePendingUntilTheyAreAcknowledged() {
        val reports = listOf(javaReport("001", 1_000L), nativeReport(2_000L, 700L))

        assertEquals(reports, store.pendingReports(reports))

        store.acknowledge(reports)

        assertTrue(store.pendingReports(reports).isEmpty())
        assertTrue(stateFile.isFile)
    }

    @Test
    fun aNewJavaReportBecomesPendingAfterEarlierReportsWereAcknowledged() {
        val acknowledged = listOf(javaReport("001", 1_000L))
        store.acknowledge(acknowledged)
        val newReport = javaReport("002", 2_000L)

        assertEquals(listOf(newReport), store.pendingReports(acknowledged + newReport))
    }

    @Test
    fun aReplacedNativeReportBecomesPendingAgain() {
        val original = nativeReport(lastModified = 1_000L, sizeBytes = 700L)
        store.acknowledge(listOf(original))
        val replacement = nativeReport(lastModified = 2_000L, sizeBytes = 700L)

        assertEquals(listOf(replacement), store.pendingReports(listOf(replacement)))
    }

    @Test
    fun oversizedRecoveryStateIsIgnored() {
        stateFile.parentFile?.mkdirs()
        stateFile.writeText("x".repeat(9 * 1_024))
        val report = javaReport("001", 1_000L)

        assertEquals(listOf(report), store.pendingReports(listOf(report)))
    }

    private fun javaReport(suffix: String, lastModified: Long): StoredCrashReport {
        return StoredCrashReport(
            fileName = "java-crash-20260908-120000-$suffix.txt",
            lastModified = lastModified,
            sizeBytes = 1_024L
        )
    }

    private fun nativeReport(lastModified: Long, sizeBytes: Long): StoredCrashReport {
        return StoredCrashReport(
            fileName = CrashReportStore.NATIVE_REPORT_FILE_NAME,
            lastModified = lastModified,
            sizeBytes = sizeBytes
        )
    }
}
