package com.cytarch.cellularity.platform.files

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InterruptedIOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import kotlinx.coroutines.CancellationException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AtomicFileStoreTest {
    @Test
    fun successfulReplacePublishesCompleteFileAndRemovesTemporaryFile() = withDirectory { directory ->
        val target = File(directory, "world.substrate").apply { writeText("old") }
        val operations = JvmDurableFileOperations()
        val store = AtomicFileStore(operations)

        store.replace(target, maximumBytes = 3, produce = { it.writeText("new") })

        assertEquals("new", target.readText())
        assertEquals(listOf("sync-file", "replace", "sync-directory"), operations.calls)
        assertTrue(temporaryFiles(directory).isEmpty())
    }

    @Test
    fun producerFailureKeepsOldTargetAndCleansTemporaryFile() = withDirectory { directory ->
        val target = File(directory, "world.substrate").apply { writeText("old") }
        val store = AtomicFileStore(JvmDurableFileOperations())

        assertThrows(IOException::class.java) {
            store.replace(target, 32, produce = { temporary ->
                temporary.writeText("partial")
                throw IOException("stopped")
            })
        }

        assertEquals("old", target.readText())
        assertTrue(temporaryFiles(directory).isEmpty())
    }

    @Test
    fun validationAndSyncFailuresNeverReplaceOldTarget() = withDirectory { directory ->
        val target = File(directory, "world.substrate").apply { writeText("old") }
        val store = AtomicFileStore(JvmDurableFileOperations())
        assertThrows(IllegalArgumentException::class.java) {
            store.replace(
                target,
                32,
                produce = { it.writeText("new") },
                validate = { throw IllegalArgumentException("invalid") }
            )
        }
        assertEquals("old", target.readText())
        assertTrue(temporaryFiles(directory).isEmpty())

        val failingSync = JvmDurableFileOperations(failFileSync = true)
        assertThrows(IOException::class.java) {
            AtomicFileStore(failingSync).replace(
                target,
                32,
                produce = { it.writeText("new") }
            )
        }
        assertEquals("old", target.readText())
        assertTrue(temporaryFiles(directory).isEmpty())
    }

    @Test
    fun interruptionBeforeCommitKeepsOldTargetAndCleansTemporaryFile() = withDirectory { directory ->
        val target = File(directory, "world.substrate").apply { writeText("old") }
        try {
            assertThrows(InterruptedIOException::class.java) {
                AtomicFileStore(JvmDurableFileOperations()).replace(
                    target,
                    32,
                    produce = { temporary ->
                        temporary.writeText("new")
                        Thread.currentThread().interrupt()
                    }
                )
            }
        } finally {
            Thread.interrupted()
        }
        assertEquals("old", target.readText())
        assertTrue(temporaryFiles(directory).isEmpty())
    }

    @Test
    fun coroutineCancellationBeforeCommitKeepsOldTargetAndCleansTemporaryFile() =
        withDirectory { directory ->
            val target = File(directory, "world.substrate").apply { writeText("old") }
            var checks = 0
            assertThrows(CancellationException::class.java) {
                AtomicFileStore(JvmDurableFileOperations()).replace(
                    target = target,
                    maximumBytes = 32,
                    checkCancellation = {
                        checks++
                        if (checks == 2) throw CancellationException("cancelled")
                    },
                    produce = { it.writeText("new") }
                )
            }
            assertEquals("old", target.readText())
            assertTrue(temporaryFiles(directory).isEmpty())
        }

    @Test
    fun exactLimitIsAcceptedAndLimitPlusOneIsRejected() = withDirectory { directory ->
        val target = File(directory, "world.substrate")
        val store = AtomicFileStore(JvmDurableFileOperations())
        store.replace(target, 4, produce = { it.writeBytes(byteArrayOf(1, 2, 3, 4)) })
        assertEquals(4, target.length())

        assertThrows(FileSizeLimitExceededException::class.java) {
            store.replace(target, 4, produce = { it.writeBytes(byteArrayOf(1, 2, 3, 4, 5)) })
        }
        assertEquals(4, target.length())
        assertTrue(temporaryFiles(directory).isEmpty())
    }

    @Test
    fun nonReplacingTransactionDoesNotOverwriteConcurrentTarget() = withDirectory { directory ->
        val target = File(directory, "world.substrate")
        assertThrows(DestinationExistsException::class.java) {
            AtomicFileStore(JvmDurableFileOperations()).replace(
                target,
                32,
                replaceExisting = false,
                produce = { temporary ->
                    temporary.writeText("incoming")
                    target.writeText("existing")
                }
            )
        }
        assertEquals("existing", target.readText())
        assertTrue(temporaryFiles(directory).isEmpty())
    }

    @Test
    fun renameIsAtomicAndNeverOverwritesAnExistingTarget() = withDirectory { directory ->
        val source = File(directory, "old.genome").apply { writeText("source") }
        val target = File(directory, "new.genome")
        val operations = JvmDurableFileOperations()
        val store = AtomicFileStore(operations)

        store.rename(source, target)

        assertTrue(!source.exists())
        assertEquals("source", target.readText())
        assertEquals(listOf("replace", "sync-directory"), operations.calls)

        val secondSource = File(directory, "second.genome").apply { writeText("second") }
        assertThrows(DestinationExistsException::class.java) {
            store.rename(secondSource, target)
        }
        assertEquals("second", secondSource.readText())
        assertEquals("source", target.readText())
    }

    @Test
    fun cancelledRenameLeavesTheSourceAndTargetUntouched() = withDirectory { directory ->
        val source = File(directory, "old.genome").apply { writeText("source") }
        val target = File(directory, "new.genome")

        assertThrows(CancellationException::class.java) {
            AtomicFileStore(JvmDurableFileOperations()).rename(
                source = source,
                target = target,
                checkCancellation = { throw CancellationException("cancelled") }
            )
        }

        assertEquals("source", source.readText())
        assertTrue(!target.exists())
    }

    @Test
    fun unknownSizeStreamAcceptsExactLimitAndRejectsNextByte() {
        val exactOutput = ByteArrayOutputStream()
        assertEquals(
            4,
            copyWithLimit(ByteArrayInputStream(byteArrayOf(1, 2, 3, 4)), exactOutput, 4)
        )
        assertEquals(4, exactOutput.size())

        assertThrows(FileSizeLimitExceededException::class.java) {
            copyWithLimit(
                ByteArrayInputStream(byteArrayOf(1, 2, 3, 4, 5)),
                ByteArrayOutputStream(),
                4
            )
        }
    }

    @Test
    fun declaredSizeUsesCheckedTwoGibBoundaryWithoutAllocatingPayload() {
        val twoGib = 2L * 1024L * 1024L * 1024L

        requireDeclaredSizeWithinLimit(twoGib, twoGib)
        requireDeclaredSizeWithinLimit(null, twoGib)
        requireDeclaredSizeWithinLimit(-1L, twoGib)
        assertThrows(FileSizeLimitExceededException::class.java) {
            requireDeclaredSizeWithinLimit(twoGib + 1L, twoGib)
        }
    }

    @Test
    fun staleCleanupRemovesOrphansWithoutTouchingAnActiveTransaction() =
        withDirectory { directory ->
            val stale = File(directory, ".cellularity-transaction-stale.tmp")
                .apply { writeText("stale") }
            val target = File(directory, "world.substrate")
            val store = AtomicFileStore(JvmDurableFileOperations())

            store.replace(
                target,
                32,
                produce = { activeTemporaryFile ->
                    activeTemporaryFile.writeText("new")
                    store.cleanupTemporaryFiles(directory)
                    assertTrue(activeTemporaryFile.isFile)
                    assertTrue(!stale.exists())
                }
            )

            assertEquals("new", target.readText())
            assertTrue(temporaryFiles(directory).isEmpty())
        }

    private fun temporaryFiles(directory: File): List<File> =
        directory.listFiles().orEmpty().filter { it.name.startsWith(".cellularity-") }

    private fun withDirectory(block: (File) -> Unit) {
        val directory = Files.createTempDirectory("cellularity-atomic-test-").toFile()
        try {
            block(directory)
        } finally {
            assertTrue(directory.deleteRecursively())
        }
    }
}

private class JvmDurableFileOperations(
    private val failFileSync: Boolean = false
) : DurableFileOperations {
    val calls = mutableListOf<String>()

    override fun syncFile(file: File) {
        calls += "sync-file"
        if (failFileSync) throw IOException("sync failed")
        FileOutputStream(file, true).use { it.fd.sync() }
    }

    override fun replace(source: File, target: File) {
        calls += "replace"
        Files.move(
            source.toPath(),
            target.toPath(),
            StandardCopyOption.REPLACE_EXISTING,
            StandardCopyOption.ATOMIC_MOVE
        )
    }

    override fun syncDirectory(directory: File) {
        calls += "sync-directory"
        assertTrue(directory.isDirectory)
    }
}
