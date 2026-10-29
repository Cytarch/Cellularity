/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.files

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue
import kotlinx.coroutines.runBlocking

/**
 * That a stored plate can be named well enough for the renderer to open it.
 *
 * This is the seam between two halves that do not share a vocabulary: the application calls
 * a plate by its display name, and the native renderer decodes a file from a path. Something
 * has to turn one into the other, and doing it here rather than at the call site means the
 * name is checked by the same resolution that guards reading, writing, renaming and
 * deleting -- so a name that would escape the storage directory is refused for the same
 * reason in all five places, and there is no way to open a file the rest of the application
 * would not touch.
 *
 * The refusal is asserted as a storage failure rather than as an exception, because that is
 * what the caller has to handle: a plate whose file has been deleted since the list was read
 * is an ordinary outcome of a person using a file manager, not a crash.
 */
class DesktopSubstratePathTest {

    @Test
    fun `a stored plate resolves to the file the repository would read`() = runBlocking {
        val storage = Files.createTempDirectory("cellularity-substrate-path")
        try {
            val repository = DesktopCellularityFileRepository(storage)
            val fileName = CellularityFileNames.substrateFileName("Dish")
            val written = Files.write(storage.resolve(fileName), byteArrayOf(1, 2, 3))

            val resolved = repository.substrateFilePath(StoredSubstrateId("Dish"))

            assertTrue(
                resolved is StorageResult.Success,
                "a plate whose file is present must resolve, not report $resolved"
            )
            assertEquals(
                written.toAbsolutePath(),
                (resolved as StorageResult.Success).value,
                "the renderer must be pointed at the file the repository itself would read"
            )
        } finally {
            deleteRecursively(storage)
        }
    }

    @Test
    fun `a plate whose file is gone does not resolve`() = runBlocking {
        val storage = Files.createTempDirectory("cellularity-substrate-path")
        try {
            val repository = DesktopCellularityFileRepository(storage)

            val resolved = repository.substrateFilePath(StoredSubstrateId("Absent"))

            assertTrue(
                resolved is StorageResult.Failure,
                "a plate with no file must not resolve to a path, but reported $resolved"
            )
            assertEquals(
                StorageErrorCode.NOT_FOUND,
                (resolved as StorageResult.Failure).error.code,
                "a missing file is a missing file, and saying so is what the caller shows"
            )
        } finally {
            deleteRecursively(storage)
        }
    }

    /**
     * A name that is not a safe single path element never reaches the storage layer.
     *
     * The refusal happens in the identifier's own constructor, which is worth asserting here
     * rather than assuming: it is the reason this entry point does not need to re-check the
     * name, and if the identifier ever stopped checking, this is where that would have to be
     * noticed. The exception is deliberate -- a name like this is a programming mistake, not
     * a condition a person can produce, because nothing in the interface can build one.
     */
    @Test
    fun `a name that would leave the storage directory cannot be constructed`() {
        val refused = try {
            StoredSubstrateId("../escaped")
            null
        } catch (expected: IllegalArgumentException) {
            expected
        }

        assertNotNull(
            refused,
            "a name containing a path must be refused where the identifier is built"
        )
    }

    private fun deleteRecursively(root: Path) {
        if (!Files.exists(root)) return
        Files.walk(root).use { stream ->
            stream.sorted(Comparator.reverseOrder()).forEach { Files.deleteIfExists(it) }
        }
    }
}
