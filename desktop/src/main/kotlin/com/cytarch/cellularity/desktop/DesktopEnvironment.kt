/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.settings.DesktopCellularitySettingsRepository
import com.cytarch.cellularity.shared.files.CellularityFileRepository
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.runBlocking

/**
 * The process-owned services the desktop app needs.
 *
 * The Android app reaches these through an Application subclass. A desktop process has
 * no such lifecycle hook, so the launcher builds them explicitly and owns their
 * lifetime -- which is also what makes it obvious that closing the window has to stop
 * the simulation rather than leaving a thread running.
 */
class DesktopEnvironment private constructor(
    val dataDirectory: Path,
    val settingsRepository: DesktopCellularitySettingsRepository,
    /**
     * The desktop implementation rather than the shared contract.
     *
     * Sharing the interface would be more general and is what callers that only list and
     * read need. The genome editor needs the concrete type, because saving from the editor
     * and drafting a genome for a new plate are desktop capabilities the contract does not
     * describe -- the phone reaches them through its own adapter. Declaring the real type
     * here keeps that a compile-time fact rather than a cast at the call site.
     */
    val fileRepository: DesktopCellularityFileRepository,
    private val nativeHost: DesktopNativeHost
) {
    /** Loads persisted state. Safe to call more than once. */
    suspend fun initialize() {
        settingsRepository.initialize()
    }

    /**
     * Starts the native simulation in its own window.
     *
     * Returns the process so the caller can report a failure the user can act on rather
     * than a window that silently never appears.
     */
    fun startSimulation(): Result<DesktopNativeHost.Session> = nativeHost.start()

    fun close() {
        nativeHost.close()
    }

    companion object {
        fun create(): DesktopEnvironment {
            val dataDirectory = resolveDataDirectory()
            return DesktopEnvironment(
                dataDirectory = dataDirectory,
                settingsRepository = DesktopCellularitySettingsRepository(
                    dataDirectory.resolve("settings.json")
                ),
                fileRepository = DesktopCellularityFileRepository(
                    dataDirectory.resolve("substrates")
                ),
                nativeHost = DesktopNativeHost()
            )
        }

        /**
         * Where user data lives: see [DesktopPlatform.defaultDataDirectory].
         * CELLULARITY_DATA_DIR overrides it on every platform.
         */
        private fun resolveDataDirectory(): Path {
            val override = System.getenv("CELLULARITY_DATA_DIR")
            val root = if (!override.isNullOrBlank()) {
                Path.of(override)
            } else {
                DesktopPlatform.defaultDataDirectory()
            }
            // Created eagerly so a later failure names a permissions problem rather
            // than a missing directory.
            Files.createDirectories(root)
            return root
        }
    }
}
