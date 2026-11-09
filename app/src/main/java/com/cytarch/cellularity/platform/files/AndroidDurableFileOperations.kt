package com.cytarch.cellularity.platform.files

import android.system.Os
import android.system.OsConstants
import com.cytarch.cellularity.shared.files.storage.DurableFileOperations
import java.io.File
import java.io.FileOutputStream

/** Android's durable rename and fsync, through the platform's POSIX bindings. */
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
