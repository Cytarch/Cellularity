/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.settings

import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Checks what happens when the file system refuses the temporary settings file.
 *
 * This is the shape of a real report: `IO_FAILURE (AccessDeniedException: ...settings.jsonNNN.tmp)`
 * -- a refusal that named the temporary file rather than the document, which means it happened at
 * the create or the write and not at the move that publishes it.
 *
 * The first answer to that report was to try again, on the theory that a filter driver scanning a
 * file that had just appeared was holding it. Measuring it found something else: the process was
 * running at low integrity, so every write into the profile was refused, permanently. Retrying
 * could never have helped, and it made the report worse -- "after 3 attempts" describes the
 * retry rather than the refusal.
 *
 * What is checked here is the answer that replaced it. The refusal still cannot be produced on
 * demand, so a document name the file system always refuses is used, and the two things that
 * matter are asserted instead: that the failure is reported once, naming what the file system
 * said, and that nothing half-written is left in the settings directory.
 */
class DesktopSettingsRefusedWriteTest {

    @get:Rule
    val folder = TemporaryFolder()

    /**
     * A name longer than any file system accepts as one path component.
     *
     * The refusal has to be permanent rather than transient, or the test would measure a
     * successful second attempt rather than the report. The name has to be long enough to be
     * refused when the temporary file is created, and it is the temporary file that inherits it:
     * the repository builds the temporary name from the document's own.
     */
    private fun impossibleSettingsFile(): Path =
        folder.root.toPath().resolve("s".repeat(300) + ".json")

    @Test
    fun aRefusedWriteIsReportedWithWhatTheFileSystemSaid() = runBlocking {
        val repository = DesktopCellularitySettingsRepository(impossibleSettingsFile())

        val outcome = repository.writeSettings(CellularitySettings())

        assertTrue(
            "a document that cannot be created has to be reported as a failure",
            outcome is SettingsResult.Failure
        )
        val detail = (outcome as SettingsResult.Failure).error.detail
        assertTrue(
            "the report has to carry what actually refused the write; it said: $detail",
            detail.isNotBlank()
        )
        assertFalse(
            "the report must not talk about how many times the write was tried: the retry is " +
                "gone and the number would describe the code rather than the refusal. It said: " +
                detail,
            detail.contains("attempts")
        )
    }

    /**
     * A refused write must not leave its half-written attempt in the settings directory.
     *
     * The temporary file can be created and then refused the bytes, and a directory that fills
     * with abandoned settings documents is a worse fault than the refusal itself, because the
     * documents are named like the real one and nothing points at them.
     */
    @Test
    fun aRefusedWriteLeavesNoTemporaryFileBehind() = runBlocking {
        val repository = DesktopCellularitySettingsRepository(impossibleSettingsFile())

        repository.writeSettings(CellularitySettings())

        val leftovers = Files.list(folder.root.toPath()).use { listing ->
            listing.filter { it.fileName.toString().endsWith(".tmp") }.toList()
        }
        assertEquals(
            "no temporary document may survive a refused write",
            emptyList<Path>(),
            leftovers
        )
    }
}
