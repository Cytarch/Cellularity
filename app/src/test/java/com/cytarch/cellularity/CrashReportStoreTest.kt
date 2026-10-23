package com.cytarch.cellularity

import java.io.File
import java.nio.file.Files
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class CrashReportStoreTest {
    private lateinit var root: File
    private lateinit var reportsDirectory: File
    private lateinit var store: CrashReportStore

    @Before
    fun setUp() {
        root = Files.createTempDirectory("cellularity-diagnostics-test").toFile()
        reportsDirectory = File(root, "private")
        store = CrashReportStore(reportsDirectory)
    }

    @After
    fun tearDown() {
        root.deleteRecursively()
    }

    @Test
    fun retainsFourJavaReportsAndOneFixedNativeReport() {
        repeat(7) { index ->
            val fileName = "java-crash-20260908-12000$index-00$index.txt"
            store.writeJavaReport(fileName, "report-$index")
            File(reportsDirectory, fileName).setLastModified(1_000L + index)
        }
        File(reportsDirectory, CrashReportStore.NATIVE_REPORT_FILE_NAME).apply {
            writeText("native report")
            setLastModified(2_000L)
        }

        val reports = store.listReports()

        assertEquals(CrashReportStore.MAX_JAVA_REPORTS + 1, reports.size)
        assertEquals(1, reports.count { it.fileName == CrashReportStore.NATIVE_REPORT_FILE_NAME })
        assertEquals(
            CrashReportStore.MAX_JAVA_REPORTS,
            reports.count { it.fileName.startsWith("java-crash-") }
        )
        assertFalse(reports.any { it.fileName.contains("120000") })
    }

    @Test
    fun limitsOneReportToConfiguredByteCount() {
        val oversized = "x".repeat(CrashReportStore.MAX_REPORT_BYTES + 1_024)
        val fileName = "java-crash-20260908-120000-000.txt"

        store.writeJavaReport(fileName, oversized)

        val reportFile = File(reportsDirectory, fileName)
        assertTrue(reportFile.length() <= CrashReportStore.MAX_REPORT_BYTES)
        assertTrue(reportFile.readText().endsWith("[diagnostics truncated]\n"))
        assertEquals(reportFile.length(), store.listReports().single().sizeBytes)
    }

    @Test
    fun exportsLargeReportWithoutAdditionalTruncation() {
        val report = "stack-line\n".repeat(64_000)
        val fileName = "java-crash-20260908-120000-000.txt"
        store.writeJavaReport(fileName, report)
        val exportFile = File(root, "cache/exports/cellularity-diagnostics.txt")

        store.writeExport(exportFile)

        val exported = exportFile.readText()
        assertTrue(exported.contains(report))
        assertFalse(exported.contains("[diagnostics truncated]"))
    }

    @Test
    fun exportContainsOnlyRecognizedBoundedReports() {
        store.writeJavaReport(
            "java-crash-20260908-120000-000.txt",
            "version=0.1.0-alpha.1\ndevice=Test\nthread=main\nstack trace"
        )
        File(reportsDirectory, "unrelated.txt").writeText("must not be exported")
        val exportFile = File(root, "cache/exports/cellularity-diagnostics.txt")

        store.writeExport(exportFile)

        val exported = exportFile.readText()
        assertTrue(exported.startsWith("Cellularity diagnostics export\nreports=1\n"))
        assertTrue(exported.contains("version=0.1.0-alpha.1"))
        assertTrue(exported.contains("device=Test"))
        assertTrue(exported.contains("thread=main"))
        assertTrue(exported.contains("stack trace"))
        assertFalse(exported.contains("must not be exported"))
    }

    @Test
    fun exportIncludesRuntimeCapabilityDiagnosticsWhenProvided() {
        store.writeJavaReport(
            "java-crash-20260908-120000-000.txt",
            "stack trace"
        )
        val exportFile = File(root, "cache/exports/cellularity-diagnostics.txt")

        store.writeExport(
            destination = exportFile,
            runtimeDiagnostics = "Vulkan capability probe\nstatus=supported\nvendor_id=1234\n"
        )

        val exported = exportFile.readText()
        assertTrue(exported.contains("===== runtime diagnostics ====="))
        assertTrue(exported.contains("Vulkan capability probe"))
        assertTrue(exported.contains("status=supported"))
        assertTrue(exported.contains("vendor_id=1234"))
    }

    @Test
    fun rejectsUnexpectedReportFileName() {
        assertThrows(IllegalArgumentException::class.java) {
            store.writeJavaReport("../outside.txt", "report")
        }
        assertFalse(File(root, "outside.txt").exists())
    }
}
