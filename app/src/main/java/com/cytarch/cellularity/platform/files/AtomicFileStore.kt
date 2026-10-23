package com.cytarch.cellularity.platform.files

import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.IOException
import java.io.InterruptedIOException

internal class FileSizeLimitExceededException : IOException()

internal class DestinationExistsException : IOException()

internal interface DurableFileOperations {
    fun syncFile(file: File)
    fun replace(source: File, target: File)
    fun syncDirectory(directory: File)
}

internal object AndroidDurableFileOperations : DurableFileOperations {
    override fun syncFile(file: File) {
        FileOutputStream(file, true).use { it.fd.sync() }
    }

    override fun replace(source: File, target: File) {
        Os.rename(source.absolutePath, target.absolutePath)
    }

    override fun syncDirectory(directory: File) {
        val descriptor = Os.open(directory.absolutePath, OsConstants.O_RDONLY, 0)
        try {
            Os.fsync(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }
}

/** Produces a sibling temporary file and exposes the target only after a durable write. */
internal class AtomicFileStore(
    private val operations: DurableFileOperations = AndroidDurableFileOperations
) {
    fun cleanupTemporaryFiles(directory: File) {
        val canonicalDirectory = directory.canonicalFile
        if (!canonicalDirectory.isDirectory) return
        synchronized(activeTemporaryFiles) {
            canonicalDirectory.listFiles().orEmpty()
                .filter {
                    it.name.startsWith(TEMPORARY_FILE_PREFIX) && it.name.endsWith(".tmp")
                }
                .filterNot { it.canonicalPath in activeTemporaryFiles }
                .forEach(File::delete)
        }
    }

    fun replace(
        target: File,
        maximumBytes: Long,
        replaceExisting: Boolean = true,
        checkCancellation: () -> Unit = ::throwIfThreadInterrupted,
        produce: (File) -> Unit,
        validate: (File) -> Unit = {}
    ) {
        require(maximumBytes > 0L) { "File size limit must be positive" }
        val targetDirectory = requireNotNull(target.parentFile)
        val directory = targetDirectory.canonicalFile
        check(directory.isDirectory) { "Target directory does not exist" }
        require(target.canonicalFile.parentFile == directory) { "Invalid target path" }

        val temporaryFile = synchronized(activeTemporaryFiles) {
            File.createTempFile(
                "$TEMPORARY_FILE_PREFIX${target.name.take(40)}-",
                ".tmp",
                directory
            ).also { activeTemporaryFiles += it.canonicalPath }
        }
        var replaced = false
        try {
            checkCancellation()
            produce(temporaryFile)
            checkCancellation()
            check(temporaryFile.isFile) { "Temporary file was not produced" }
            if (temporaryFile.length() > maximumBytes) {
                throw FileSizeLimitExceededException()
            }
            validate(temporaryFile)
            checkCancellation()
            if (temporaryFile.length() > maximumBytes) {
                throw FileSizeLimitExceededException()
            }
            operations.syncFile(temporaryFile)
            checkCancellation()
            synchronized(commitLock) {
                if (!replaceExisting && target.exists()) {
                    throw DestinationExistsException()
                }
                operations.replace(temporaryFile, target)
                replaced = true
            }
            operations.syncDirectory(directory)
        } finally {
            synchronized(activeTemporaryFiles) {
                if (!replaced || temporaryFile.exists()) {
                    temporaryFile.delete()
                }
                activeTemporaryFiles -= temporaryFile.canonicalPath
            }
        }
    }

    /** Moves one application-owned file without exposing a replace race. */
    fun rename(
        source: File,
        target: File,
        replaceExisting: Boolean = false,
        checkCancellation: () -> Unit = ::throwIfThreadInterrupted
    ) {
        val sourceDirectory = requireNotNull(source.parentFile).canonicalFile
        val targetDirectory = requireNotNull(target.parentFile).canonicalFile
        require(sourceDirectory == targetDirectory) { "Rename must stay in one directory" }
        require(source.canonicalFile.parentFile == sourceDirectory) { "Invalid source path" }
        require(target.canonicalFile.parentFile == targetDirectory) { "Invalid target path" }

        checkCancellation()
        synchronized(commitLock) {
            checkCancellation()
            if (!source.isFile) throw FileNotFoundException("Source file does not exist")
            if (!replaceExisting && target.exists()) throw DestinationExistsException()
            operations.replace(source, target)
        }
        operations.syncDirectory(sourceDirectory)
    }

    private companion object {
        const val TEMPORARY_FILE_PREFIX = ".cellularity-transaction-"
        val activeTemporaryFiles = mutableSetOf<String>()
        val commitLock = Any()
    }
}

internal fun throwIfThreadInterrupted() {
    if (Thread.currentThread().isInterrupted) {
        throw InterruptedIOException("File operation was interrupted")
    }
}
