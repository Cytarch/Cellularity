/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.settings.storage

import com.cytarch.cellularity.shared.files.storage.AtomicFileStore
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsJson
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.SettingsRecoveryState
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.io.File
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow

/**
 * The application settings document, the same implementation on Android and the desktop:
 * schema-versioned JSON, durable atomic commits, and a recovery copy of an unreadable file.
 * A platform supplies only the file and its durable file primitives.
 */
class FileCellularitySettingsRepository(
    settingsFile: File,
    atomicFiles: AtomicFileStore,
    dispatcher: CoroutineDispatcher = Dispatchers.IO
) : CellularitySettingsRepository {
    private val store = AtomicJsonSettingsStore(
        target = settingsFile,
        defaultValue = CellularitySettings.Default,
        normalize = CellularitySettings::normalized,
        encode = CellularitySettingsJson::encode,
        decode = CellularitySettingsJson::decode,
        atomicFiles = atomicFiles,
        dispatcher = dispatcher
    )

    override val settings: StateFlow<CellularitySettings> = store.state
    override val recovery: StateFlow<SettingsRecoveryState?> = store.recovery

    override suspend fun initialize(): SettingsResult<CellularitySettings> = store.initialize()

    override suspend fun writeSettings(
        settings: CellularitySettings
    ): SettingsResult<CellularitySettings> = store.write(settings)

    override suspend fun resetSettings(): SettingsResult<CellularitySettings> = store.reset()

    companion object {
        const val SETTINGS_FILE_NAME = "settings.json"
    }
}
