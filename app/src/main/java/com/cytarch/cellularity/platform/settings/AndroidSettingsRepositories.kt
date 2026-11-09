package com.cytarch.cellularity.platform.settings

import com.cytarch.cellularity.platform.files.AndroidDurableFileOperations
import com.cytarch.cellularity.shared.files.storage.AtomicFileStore
import com.cytarch.cellularity.shared.settings.storage.AtomicJsonSettingsStore
import com.cytarch.cellularity.shared.settings.storage.FileCellularitySettingsRepository
import android.content.Context
import com.cytarch.cellularity.shared.settings.AndroidDisplaySettings
import com.cytarch.cellularity.shared.settings.AndroidDisplaySettingsJson
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendSettings
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendSettingsJson
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsJson
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.SettingsRecoveryState
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.io.File
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow

/** Android's settings document in the app's private files directory (shared implementation). */
internal class AndroidCellularitySettingsRepository private constructor(
    private val repository: FileCellularitySettingsRepository
) : CellularitySettingsRepository by repository {
    constructor(context: Context) : this(
        settingsFile = File(
            context.applicationContext.filesDir,
            FileCellularitySettingsRepository.SETTINGS_FILE_NAME
        )
    )

    internal constructor(
        settingsFile: File,
        atomicFiles: AtomicFileStore = AtomicFileStore(AndroidDurableFileOperations),
        dispatcher: CoroutineDispatcher = Dispatchers.IO
    ) : this(FileCellularitySettingsRepository(settingsFile, atomicFiles, dispatcher))
}

internal class AndroidDisplaySettingsRepository private constructor(
    private val store: AtomicJsonSettingsStore<AndroidDisplaySettings>
) {
    constructor(context: Context) : this(
        settingsFile = File(
            context.applicationContext.filesDir,
            DISPLAY_SETTINGS_FILE_NAME
        )
    )

    internal constructor(
        settingsFile: File,
        atomicFiles: AtomicFileStore = AtomicFileStore(AndroidDurableFileOperations),
        dispatcher: CoroutineDispatcher = Dispatchers.IO
    ) : this(
        AtomicJsonSettingsStore(
            target = settingsFile,
            defaultValue = AndroidDisplaySettings.Default,
            normalize = { it },
            encode = AndroidDisplaySettingsJson::encode,
            decode = AndroidDisplaySettingsJson::decode,
            atomicFiles = atomicFiles,
            dispatcher = dispatcher
        )
    )

    val settings: StateFlow<AndroidDisplaySettings> = store.state
    val recovery: StateFlow<SettingsRecoveryState?> = store.recovery

    suspend fun initialize(): SettingsResult<AndroidDisplaySettings> = store.initialize()

    suspend fun writeSettings(
        settings: AndroidDisplaySettings
    ): SettingsResult<AndroidDisplaySettings> = store.write(settings)

    suspend fun resetSettings(): SettingsResult<AndroidDisplaySettings> = store.reset()

    private companion object {
        const val DISPLAY_SETTINGS_FILE_NAME = "android-display-settings.json"
    }
}

internal class AndroidRenderBackendSettingsRepository private constructor(
    private val store: AtomicJsonSettingsStore<AndroidRenderBackendSettings>
) {
    constructor(context: Context) : this(
        settingsFile = File(
            context.applicationContext.filesDir,
            RENDER_BACKEND_SETTINGS_FILE_NAME
        )
    )

    internal constructor(
        settingsFile: File,
        atomicFiles: AtomicFileStore = AtomicFileStore(AndroidDurableFileOperations),
        dispatcher: CoroutineDispatcher = Dispatchers.IO
    ) : this(
        AtomicJsonSettingsStore(
            target = settingsFile,
            defaultValue = AndroidRenderBackendSettings.Default,
            normalize = { it },
            encode = AndroidRenderBackendSettingsJson::encode,
            decode = AndroidRenderBackendSettingsJson::decode,
            atomicFiles = atomicFiles,
            dispatcher = dispatcher
        )
    )

    val settings: StateFlow<AndroidRenderBackendSettings> = store.state
    val recovery: StateFlow<SettingsRecoveryState?> = store.recovery

    suspend fun initialize(): SettingsResult<AndroidRenderBackendSettings> = store.initialize()

    suspend fun writeSettings(
        settings: AndroidRenderBackendSettings
    ): SettingsResult<AndroidRenderBackendSettings> = store.write(settings)

    suspend fun resetSettings(): SettingsResult<AndroidRenderBackendSettings> = store.reset()

    private companion object {
        const val RENDER_BACKEND_SETTINGS_FILE_NAME = "android-render-backend-settings.json"
    }
}
