/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import java.nio.file.Path

/**
 * The operating system the desktop application is running on, and the few places where the
 * Windows and Linux builds differ: where user data lives and where a development build keeps
 * the native library.
 *
 * Everything else -- the interface, the renderer, the file formats -- is the same on both, so
 * the differences are collected here rather than spread as `os.name` checks.
 */
object DesktopPlatform {

    val isWindows: Boolean =
        System.getProperty("os.name").orEmpty().startsWith("Windows", ignoreCase = true)

    /**
     * The CMake build trees a development run looks in for the native library, relative to a
     * repository root. `build/desktop-cmake` is the shared name; `build/win-cmake` is still
     * accepted on Windows for trees configured before the Linux port.
     */
    val developmentNativeDirectories: List<String> =
        if (isWindows) {
            listOf(
                "build/desktop-cmake",
                "build/desktop-cmake/Release",
                "build/desktop-cmake/Debug",
                "build/win-cmake",
                "build/win-cmake/Release",
                "build/win-cmake/Debug"
            )
        } else {
            listOf("build/desktop-cmake")
        }

    /**
     * The per-user directory documents and settings are kept in.
     *
     * Windows: the roaming profile, so substrates and genomes follow the user between
     * machines. Linux: `$XDG_DATA_HOME/cellularity`, falling back to
     * `~/.local/share/cellularity` as the XDG base directory specification asks.
     */
    fun defaultDataDirectory(): Path {
        val home = System.getProperty("user.home")
        if (isWindows) {
            val appData = System.getenv("APPDATA")
            return if (appData.isNullOrBlank()) {
                Path.of(home, ".cellularity")
            } else {
                Path.of(appData, "Cellularity")
            }
        }
        val dataHome = System.getenv("XDG_DATA_HOME")
            ?.takeIf { it.isNotBlank() && Path.of(it).isAbsolute }
            ?.let { Path.of(it) }
            ?: Path.of(home, ".local", "share")
        return dataHome.resolve("cellularity")
    }
}
