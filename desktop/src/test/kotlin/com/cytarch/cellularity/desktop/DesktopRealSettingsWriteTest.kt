/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.settings.DesktopCellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Switches a setting in the directory the application actually uses.
 *
 * The switch is drawn correctly -- its two states differ -- and a press on it reaches the card
 * and reports the action. What has never been checked is the last step: the store's state comes
 * from the repository, not from memory, so a setting only changes if the document is accepted.
 *
 *     override val state: StateFlow<CellularitySettings> = repository.settings
 *
 * The repository this build uses is not the one the earlier tests used. Those wrote to a
 * temporary directory, which is the right way to test a repository and the wrong way to test
 * this: the question is not whether the code can write a file, it is whether it can write the
 * file the application uses, in the directory the application chose. A path that cannot be
 * created, a directory that is read-only, or a document that fails to decode all look identical
 * from the interface -- the switch moves under the finger, the callback fires, the action is
 * reduced, and the state never changes because the write was refused.
 *
 * The user's own document is backed up and put back, because a test that damages the settings it
 * is checking would be a worse problem than the one it is looking for.
 */
class DesktopRealSettingsWriteTest {

    @Test
    fun aSwitchSurvivesBeingWrittenToTheDirectoryTheApplicationUses() {
        val environment = DesktopEnvironment.create()
        val settingsFile = environment.dataDirectory.resolve("settings.json")
        println("SETTINGS the application writes to $settingsFile")
        println("SETTINGS that directory exists: ${Files.isDirectory(environment.dataDirectory)}")

        val backup = if (Files.exists(settingsFile)) Files.readAllBytes(settingsFile) else null
        try {
            val repository = DesktopCellularitySettingsRepository(settingsFile)

            val initialized = runBlocking { repository.initialize() }
            println("SETTINGS initialize returned $initialized")
            assertTrue(
                "the settings document in the application's own directory could not be read: " +
                    "$initialized",
                initialized is SettingsResult.Success
            )

            val before = repository.settings.value
            val wanted = before.copy(keepOn = !before.keepOn)
            val written = runBlocking { repository.writeSettings(wanted) }
            println("SETTINGS writeSettings returned $written")

            assertTrue(
                "writing a setting to the application's own directory was refused, so a switch " +
                    "in the launcher can never appear to work: $written",
                written is SettingsResult.Success
            )
            assertTrue(
                "the repository accepted the write but still publishes ${repository.settings.value.keepOn} " +
                    "instead of ${wanted.keepOn}, so the interface has nothing new to draw",
                repository.settings.value.keepOn == wanted.keepOn
            )

            // Read back through a second repository, so the check is about the document rather
            // than about what the first one remembers.
            val reloaded = DesktopCellularitySettingsRepository(settingsFile)
            runBlocking { reloaded.initialize() }
            assertTrue(
                "the setting was accepted and published but was not in the document: a fresh " +
                    "read gives ${reloaded.settings.value.keepOn} instead of ${wanted.keepOn}",
                reloaded.settings.value.keepOn == wanted.keepOn
            )
        } finally {
            if (backup != null) {
                Files.write(settingsFile, backup)
            } else {
                Files.deleteIfExists(settingsFile)
            }
        }
    }
}
