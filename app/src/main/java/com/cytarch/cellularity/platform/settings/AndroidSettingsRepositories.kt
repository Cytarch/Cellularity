package com.cytarch.cellularity.platform.settings

import android.content.Context
import com.cytarch.cellularity.platform.files.AtomicFileStore
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

internal class AndroidCellularitySettingsRepository private constructor(
    private val store: AtomicJsonSettingsStore<CellularitySettings>
) : CellularitySettingsRepository {
    constructor(context: Context) : this(
        settingsFile = File(context.applicationContext.filesDir, SETTINGS_FILE_NAME)
    )

    internal constructor(
        settingsFile: File,
        atomicFiles: AtomicFileStore = AtomicFileStore(),
        dispatcher: CoroutineDispatcher = Dispatchers.IO
    ) : this(
        AtomicJsonSettingsStore(
            target = settingsFile,
            defaultValue = CellularitySettings.Default,
            normalize = CellularitySettings::normalized,
            encode = CellularitySettingsJson::encode,
            decode = CellularitySettingsJson::decode,
            atomicFiles = atomicFiles,
            dispatcher = dispatcher
        )
    )

    override val settings: StateFlow<CellularitySettings> = store.state
    override val recovery: StateFlow<SettingsRecoveryState?> = store.recovery

    override suspend fun initialize(): SettingsResult<CellularitySettings> = store.initialize()

    override suspend fun writeSettings(
        settings: CellularitySettings
    ): SettingsResult<CellularitySettings> = store.write(settings)

    override suspend fun resetSettings(): SettingsResult<CellularitySettings> = store.reset()

    private companion object {
        const val SETTINGS_FILE_NAME = "settings.json"
    }
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
        atomicFiles: AtomicFileStore = AtomicFileStore(),
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
        atomicFiles: AtomicFileStore = AtomicFileStore(),
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
