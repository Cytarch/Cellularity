/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import java.nio.file.Files
import java.nio.file.Path

/**
 * Starts the native simulation, which renders through Vulkan in its own window.
 *
 * The renderer is a native program rather than a library the launcher calls into. That
 * is a deliberate boundary: it owns the Vulkan instance, the swapchain and the
 * simulation thread, and it presents into a window it created, so there is exactly one
 * owner of the surface and no cross-language lifetime to get wrong. The launcher passes
 * the run configuration on the command line and otherwise leaves it alone.
 *
 * A later step embeds that window in the launcher instead of floating beside it; the
 * seam for it is that the host already takes its configuration as arguments and owns
 * nothing the launcher needs to reach into.
 */
class DesktopNativeHost(
    private val executable: Path? = findExecutable()
) {
    private var session: Session? = null

    /** A running simulation. Closing it stops the process. */
    class Session internal constructor(private val process: Process) {
        val isAlive: Boolean
            get() = process.isAlive

        fun close() {
            if (process.isAlive) {
                process.destroy()
                // The host stops its own simulation thread and closes the session on
                // the way out, so it is given a moment to do that before it is killed.
                if (!process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS)) {
                    process.destroyForcibly()
                }
            }
        }
    }

    val isAvailable: Boolean
        get() = executable != null

    val executablePath: Path?
        get() = executable

    fun start(
        configuration: SimulationConfiguration = SimulationContract.defaultConfiguration
    ): Result<Session> {
        session?.let { existing ->
            if (existing.isAlive) {
                return Result.success(existing)
            }
        }
        val path = executable
            ?: return Result.failure(
                IllegalStateException(
                    "The native host executable was not found. Build it with " +
                        "scripts/native/build-windows-host, or point " +
                        "CELLULARITY_HOST_EXE at it."
                )
            )
        return try {
            val process = ProcessBuilder(
                path.toString(),
                "--cells", configuration.maxCellCount.toString(),
                "--food", configuration.maxFoodCount.toString(),
                "--radius", configuration.radius.toString()
            )
                .directory(path.parent.toFile())
                .redirectErrorStream(true)
                .start()
            val started = Session(process)
            session = started
            Result.success(started)
        } catch (failure: Exception) {
            Result.failure(failure)
        }
    }

    fun close() {
        session?.close()
        session = null
    }

    private companion object {
        /**
         * Locates the host.
         *
         * The environment variable comes first so a build directory anywhere on the
         * machine can be used without rebuilding the launcher, which matters while the
         * native side is still moving.
         */
        fun findExecutable(): Path? {
            val override = System.getenv("CELLULARITY_HOST_EXE")
            if (!override.isNullOrBlank()) {
                val candidate = Path.of(override)
                if (Files.isRegularFile(candidate)) return candidate
            }
            val executableName = "cellularity_win32_host.exe"
            // Walk up from the working directory: the launcher runs from the
            // repository root under Gradle, and from an installed layout otherwise.
            var directory: Path? = Path.of("").toAbsolutePath()
            while (directory != null) {
                for (relative in CANDIDATE_LOCATIONS) {
                    val candidate = directory.resolve(relative).resolve(executableName)
                    if (Files.isRegularFile(candidate)) return candidate
                }
                directory = directory.parent
            }
            return null
        }

        // The standalone host is a Windows executable; on Linux nothing is found and the
        // launcher falls back to the in-process renderer, which is the normal path anyway.
        val CANDIDATE_LOCATIONS = DesktopPlatform.developmentNativeDirectories
    }
}
