package com.cytarch.cellularity

import android.app.Application
import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.RenderBackendDiagnostics
import com.cytarch.cellularity.platform.render.RenderBackendSessionState
import com.cytarch.cellularity.platform.render.vulkan.AndroidVulkanCapabilityRepository
import com.cytarch.cellularity.platform.settings.AndroidCellularitySettingsRepository
import com.cytarch.cellularity.platform.settings.AndroidDisplaySettingsRepository
import com.cytarch.cellularity.platform.settings.AndroidRenderBackendSettingsRepository
import com.cytarch.cellularity.shared.settings.CellularityLanguage
import com.cytarch.cellularity.shared.settings.SettingsResult
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

class CellularityApplication : Application() {
    private val settingsInitializationMutex = Mutex()
    private var settingsInitializationAttempted = false

    internal val settingsRepository: AndroidCellularitySettingsRepository by lazy {
        AndroidCellularitySettingsRepository(this)
    }

    internal val displaySettingsRepository: AndroidDisplaySettingsRepository by lazy {
        AndroidDisplaySettingsRepository(this)
    }

    internal val renderBackendSettingsRepository: AndroidRenderBackendSettingsRepository by lazy {
        AndroidRenderBackendSettingsRepository(this)
    }

    internal val renderBackendSessionState = RenderBackendSessionState()

    internal val vulkanCapabilityRepository: AndroidVulkanCapabilityRepository by lazy {
        AndroidVulkanCapabilityRepository()
    }

    override fun onCreate() {
        super.onCreate()
        CrashLogger.install(this)
    }

    /** Called only after the recovery-first launcher boundary has accepted normal startup. */
    internal suspend fun initializeSettings() {
        settingsInitializationMutex.withLock {
            if (settingsInitializationAttempted) return@withLock
            logInitialization("common", settingsRepository.initialize())
            logInitialization("Android display", displaySettingsRepository.initialize())
            logInitialization(
                "Android render backend",
                renderBackendSettingsRepository.initialize()
            )
            RenderBackendDiagnostics.recordPreference(
                renderBackendSettingsRepository.settings.value.backend
            )
            settingsInitializationAttempted = true
        }
    }

    companion object {
        @JvmStatic
        fun applyStoredLanguage(context: Context) {
            val application = context.applicationContext as? CellularityApplication ?: return
            setApplicationLanguage(application.settingsRepository.settings.value.language)
        }

        @JvmStatic
        fun setApplicationLanguage(language: CellularityLanguage) {
            AppCompatDelegate.setApplicationLocales(
                LocaleListCompat.forLanguageTags(language.languageTag)
            )
        }

        private fun logInitialization(
            documentName: String,
            result: SettingsResult<*>
        ) {
            if (result is SettingsResult.Failure) {
                AppLog.error(
                    "Settings initialization issue for $documentName: " +
                        "${result.error.code} ${result.error.detail}"
                )
            }
        }
    }
}
