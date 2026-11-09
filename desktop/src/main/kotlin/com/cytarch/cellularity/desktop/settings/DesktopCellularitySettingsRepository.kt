/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.settings

import com.cytarch.cellularity.shared.files.storage.AtomicFileStore
import com.cytarch.cellularity.shared.files.storage.NioDurableFileOperations
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.storage.FileCellularitySettingsRepository
import java.nio.file.Path

/**
 * The desktop's settings document: Android's implementation (the shared
 * [FileCellularitySettingsRepository]) at a path in the user's data directory, with java.nio's
 * durable file primitives in place of Android's POSIX bindings.
 */
class DesktopCellularitySettingsRepository private constructor(
    repository: FileCellularitySettingsRepository
) : CellularitySettingsRepository by repository {
    constructor(settingsFile: Path) : this(
        FileCellularitySettingsRepository(
            settingsFile = settingsFile.toAbsolutePath().also { path ->
                path.parent?.let { java.nio.file.Files.createDirectories(it) }
            }.toFile(),
            atomicFiles = AtomicFileStore(NioDurableFileOperations)
        )
    )
}
