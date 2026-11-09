/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.files.storage

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption

/**
 * [DurableFileOperations] on plain java.nio, for JVMs without Android's POSIX bindings
 * (the desktop). It keeps Android's guarantees: the file is fsynced before an atomic rename
 * replaces the target, and the directory entry is fsynced afterwards where the platform
 * allows opening a directory (Windows does not; NTFS journals the rename).
 */
object NioDurableFileOperations : DurableFileOperations {
    override fun syncFile(file: File) {
        FileOutputStream(file, true).use { it.fd.sync() }
    }

    override fun replace(source: File, target: File) {
        Files.move(
            source.toPath(),
            target.toPath(),
            StandardCopyOption.REPLACE_EXISTING,
            StandardCopyOption.ATOMIC_MOVE
        )
    }

    override fun syncDirectory(directory: File) {
        try {
            FileChannel.open(directory.toPath(), StandardOpenOption.READ).use { it.force(true) }
        } catch (ignored: IOException) {
            // Best effort; see the class comment.
        }
    }
}
