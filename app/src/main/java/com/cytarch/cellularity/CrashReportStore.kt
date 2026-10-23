package com.cytarch.cellularity

import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream

internal data class StoredCrashReport(
    val fileName: String,
    val lastModified: Long,
    val sizeBytes: Long
)

internal class CrashReportStore(
    private val directory: File
) {
    fun prepare() {
        check(directory.isDirectory || directory.mkdirs()) {
            "Could not create private diagnostics directory"
        }
        File(directory, PENDING_JAVA_REPORT_FILE_NAME).delete()
        pruneJavaReports()
    }

    fun writeJavaReport(fileName: String, report: String) {
        require(JAVA_REPORT_FILE_PATTERN.matches(fileName)) { "Invalid diagnostics file name" }
        prepare()

        val reportBytes = boundedUtf8(report)
        val pendingFile = File(directory, PENDING_JAVA_REPORT_FILE_NAME)
        FileOutputStream(pendingFile, false).use { output ->
            output.write(reportBytes)
            output.fd.sync()
        }
        restrictToOwner(pendingFile)

        val reportFile = File(directory, fileName)
        if (!pendingFile.renameTo(reportFile)) {
            FileOutputStream(reportFile, false).use { output ->
                output.write(reportBytes)
                output.fd.sync()
            }
            pendingFile.delete()
        }
        restrictToOwner(reportFile)
        pruneJavaReports()
    }

    fun listReports(): List<StoredCrashReport> {
        prepare()
        return reportFiles()
            .sortedByDescending(File::lastModified)
            .map { file ->
                StoredCrashReport(
                    fileName = file.name,
                    lastModified = file.lastModified(),
                    sizeBytes = file.length().coerceAtMost(MAX_REPORT_BYTES.toLong())
                )
            }
    }

    fun writeExport(destination: File, runtimeDiagnostics: String = "") {
        prepare()
        val reports = reportFiles().sortedByDescending(File::lastModified)
        require(reports.isNotEmpty()) { "No diagnostics reports to export" }
        check(destination.parentFile?.let { it.isDirectory || it.mkdirs() } == true) {
            "Could not create diagnostics export directory"
        }

        FileOutputStream(destination, false).buffered().use { output ->
            output.writeUtf8("Cellularity diagnostics export\n")
            output.writeUtf8("reports=${reports.size}\n")
            if (runtimeDiagnostics.isNotBlank()) {
                output.writeUtf8("\n===== runtime diagnostics =====\n")
                output.write(boundedUtf8(runtimeDiagnostics))
                output.writeUtf8("\n")
            }
            reports.forEach { report ->
                output.writeUtf8("\n===== ${report.name} =====\n")
                copyBounded(report, output)
                output.writeUtf8("\n")
            }
        }
        restrictToOwner(destination)
    }

    private fun reportFiles(): List<File> {
        return directory.listFiles { file ->
            file.isFile && (
                JAVA_REPORT_FILE_PATTERN.matches(file.name) ||
                    file.name == NATIVE_REPORT_FILE_NAME
                )
        }?.toList().orEmpty()
    }

    private fun pruneJavaReports() {
        val javaReports = directory.listFiles { file ->
            file.isFile && JAVA_REPORT_FILE_PATTERN.matches(file.name)
        }?.sortedByDescending(File::lastModified).orEmpty()
        javaReports.drop(MAX_JAVA_REPORTS).forEach { report ->
            report.delete()
        }
    }

    private fun boundedUtf8(report: String): ByteArray {
        val bytes = report.toByteArray(Charsets.UTF_8)
        if (bytes.size <= MAX_REPORT_BYTES) return bytes

        val marker = REPORT_TRUNCATED_MARKER.toByteArray(Charsets.UTF_8)
        var prefixLength = MAX_REPORT_BYTES - marker.size
        while (prefixLength > 0 && bytes[prefixLength].toInt() and 0xC0 == 0x80) {
            prefixLength--
        }
        return bytes.copyOf(prefixLength) + marker
    }

    private fun copyBounded(source: File, destination: java.io.OutputStream) {
        FileInputStream(source).use { input ->
            val buffer = ByteArray(COPY_BUFFER_BYTES)
            var remaining = MAX_REPORT_BYTES
            while (remaining > 0) {
                val read = input.read(buffer, 0, minOf(buffer.size, remaining))
                if (read < 0) return
                destination.write(buffer, 0, read)
                remaining -= read
            }
            if (input.read() >= 0) {
                destination.writeUtf8(REPORT_TRUNCATED_MARKER)
            }
        }
    }

    private fun java.io.OutputStream.writeUtf8(value: String) {
        write(value.toByteArray(Charsets.UTF_8))
    }

    private fun restrictToOwner(file: File) {
        file.setReadable(false, false)
        file.setWritable(false, false)
        file.setExecutable(false, false)
        file.setReadable(true, true)
        file.setWritable(true, true)
    }

    companion object {
        const val MAX_JAVA_REPORTS = 4
        const val MAX_REPORT_BYTES = 1024 * 1024
        const val NATIVE_REPORT_FILE_NAME = "native-crash.txt"
        private const val PENDING_JAVA_REPORT_FILE_NAME = ".java-crash-pending.tmp"
        private const val COPY_BUFFER_BYTES = 8 * 1024
        private const val REPORT_TRUNCATED_MARKER = "\n[diagnostics truncated]\n"
        private val JAVA_REPORT_FILE_PATTERN =
            Regex("^java-crash-[0-9]{8}-[0-9]{6}-[0-9]{3}\\.txt$")
    }
}
