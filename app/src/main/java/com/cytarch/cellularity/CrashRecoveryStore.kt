package com.cytarch.cellularity

import java.io.File
import java.io.FileOutputStream

internal class CrashRecoveryStore(
    private val stateFile: File
) {
    fun pendingReports(reports: List<StoredCrashReport>): List<StoredCrashReport> {
        if (reports.isEmpty()) return emptyList()
        val acknowledged = readAcknowledgedIds()
        return reports.filterNot { identityOf(it) in acknowledged }
    }

    fun acknowledge(reports: List<StoredCrashReport>) {
        if (reports.isEmpty()) return
        val parent = requireNotNull(stateFile.parentFile) { "Recovery state has no parent directory" }
        check(parent.isDirectory || parent.mkdirs()) {
            "Could not create private diagnostics directory"
        }

        val content = reports.joinToString(separator = "\n", postfix = "\n", transform = ::identityOf)
        val pending = File(parent, PENDING_STATE_FILE_NAME)
        FileOutputStream(pending, false).use { output ->
            output.write(content.toByteArray(Charsets.UTF_8))
            output.fd.sync()
        }
        restrictToOwner(pending)

        if (!pending.renameTo(stateFile)) {
            FileOutputStream(stateFile, false).use { output ->
                output.write(content.toByteArray(Charsets.UTF_8))
                output.fd.sync()
            }
            pending.delete()
        }
        restrictToOwner(stateFile)
    }

    private fun readAcknowledgedIds(): Set<String> {
        if (!stateFile.isFile || stateFile.length() > MAX_STATE_BYTES) return emptySet()
        return runCatching {
            stateFile.readLines(Charsets.UTF_8)
                .asSequence()
                .filter(String::isNotBlank)
                .take(MAX_TRACKED_REPORTS)
                .toSet()
        }.getOrDefault(emptySet())
    }

    private fun restrictToOwner(file: File) {
        file.setReadable(false, false)
        file.setWritable(false, false)
        file.setExecutable(false, false)
        file.setReadable(true, true)
        file.setWritable(true, true)
    }

    companion object {
        const val STATE_FILE_NAME = ".recovery-acknowledged-reports"
        private const val PENDING_STATE_FILE_NAME = ".recovery-state-pending.tmp"
        private const val MAX_STATE_BYTES = 8 * 1024L
        private const val MAX_TRACKED_REPORTS = 16

        fun identityOf(report: StoredCrashReport): String {
            return "${report.fileName}\t${report.lastModified}\t${report.sizeBytes}"
        }
    }
}
