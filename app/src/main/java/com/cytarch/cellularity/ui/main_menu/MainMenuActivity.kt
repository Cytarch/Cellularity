package com.cytarch.cellularity.ui.main_menu

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.cytarch.cellularity.CellularityApplication
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.file_operation_invalid_name_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_load_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_newer_version_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_saved_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_save_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_size_limit_exceeded_message
import com.cytarch.cellularity.shared.generated.resources.freezer_import_name_already_exists_message
import com.cytarch.cellularity.shared.generated.resources.gene_bank_import_name_already_exists_message
import com.cytarch.cellularity.shared.ui.CellularityApp
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuEffect
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFileKind
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuPage
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.SettingsResult
import com.cytarch.cellularity.ui.RecoverableAppCompatActivity
import com.cytarch.cellularity.ui.main_menu.screens.about.AboutScreen
import com.cytarch.cellularity.ui.main_menu.screens.experiments.FreezerScreen
import com.cytarch.cellularity.ui.main_menu.screens.gene_bank.GeneBankScreen
import com.cytarch.cellularity.ui.main_menu.screens.settings.SettingsScreen
import com.cytarch.cellularity.ui.play_menu.PlayActivity
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.getString

class MainMenuActivity : RecoverableAppCompatActivity() {
    private val settingsWriteMutex = Mutex()
    private val viewModel: MainMenuViewModel by viewModels {
        MainMenuViewModel.Factory(applicationContext)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (redirectedToRecovery) return
        enableEdgeToEdge()
        configureSystemBars()

        viewModel.restoreCompletedCommandIds(
            savedInstanceState?.getStringArrayList(STATE_COMPLETED_IMPORT_COMMAND_IDS).orEmpty()
        )
        collectPlatformEffects()

        val cellularityApplication = application as CellularityApplication
        val settingsRepository = cellularityApplication.settingsRepository
        val displaySettingsRepository = cellularityApplication.displaySettingsRepository
        setContent {
            val mainMenuState by viewModel.state.collectAsStateWithLifecycle()
            val settings by settingsRepository.settings.collectAsStateWithLifecycle()
            val displaySettings by displaySettingsRepository.settings.collectAsStateWithLifecycle()

            LaunchedEffect(settings.language) {
                CellularityApplication.setApplicationLanguage(settings.language)
            }

            CellularityApp(
                mainMenuState = mainMenuState,
                onMainMenuAction = viewModel::dispatch,
                avoidCameraCutout = displaySettings.avoidCameraCutout,
                themeColors = settings.theme,
                themeComponents = settings.themeComponents
            ) { page, collectionRevision ->
                when (page) {
                    MainMenuPage.EXPERIMENTS -> FreezerScreen(
                        externalRefreshRevision = collectionRevision,
                        collectionPreferences = settings.experimentCollection,
                        onCollectionPreferencesChange = { updatePreferences ->
                            persistSettings(settingsRepository) { current ->
                                current.copy(
                                    experimentCollection = updatePreferences(
                                        current.experimentCollection
                                    )
                                )
                            }
                        },
                        onStartPlayActivity = ::startActivity
                    )
                    MainMenuPage.GENE_BANK -> GeneBankScreen(
                        externalRefreshRevision = collectionRevision,
                        collectionPreferences = settings.geneBankCollection,
                        onCollectionPreferencesChange = { updatePreferences ->
                            persistSettings(settingsRepository) { current ->
                                current.copy(
                                    geneBankCollection = updatePreferences(
                                        current.geneBankCollection
                                    )
                                )
                            }
                        }
                    )
                    MainMenuPage.SETTINGS -> SettingsScreen()
                    MainMenuPage.ABOUT -> AboutScreen()
                }
            }
        }

        viewModel.acceptExternalIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        viewModel.acceptExternalIntent(intent)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putStringArrayList(
            STATE_COMPLETED_IMPORT_COMMAND_IDS,
            ArrayList(viewModel.completedCommandIds())
        )
        super.onSaveInstanceState(outState)
    }

    private fun persistSettings(
        repository: CellularitySettingsRepository,
        update: (CellularitySettings) -> CellularitySettings
    ) {
        lifecycleScope.launch {
            settingsWriteMutex.withLock {
                when (val result = repository.writeSettings(update(repository.settings.value))) {
                    is SettingsResult.Success -> Unit
                    is SettingsResult.Failure -> {
                        AppLog.error(
                            "Failed to save collection preferences: ${result.error.code} " +
                                result.error.detail
                        )
                        Toast.makeText(
                            this@MainMenuActivity,
                            getString(Res.string.file_operation_save_failed_message),
                            Toast.LENGTH_LONG
                        ).show()
                    }
                }
            }
        }
    }

    private fun configureSystemBars() {
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    private fun collectPlatformEffects() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.effects.collect(::handlePlatformEffect)
            }
        }
    }

    private suspend fun handlePlatformEffect(effect: MainMenuEffect) {
        when (effect) {
            MainMenuEffect.ImportSucceeded -> {
                AppLog.debug("Imported external Cellularity file")
                Toast.makeText(
                    this,
                    getString(Res.string.file_operation_saved_message),
                    Toast.LENGTH_LONG
                ).show()
            }
            is MainMenuEffect.ImportFailed -> {
                AppLog.error(
                    "Failed to import external Cellularity file: " +
                        "${effect.error.code} ${effect.error.detail}"
                )
                Toast.makeText(
                    this,
                    getString(importFailureMessage(effect.error.code, effect.fileKind)),
                    Toast.LENGTH_LONG
                ).show()
            }
            is MainMenuEffect.OpenSubstrate -> {
                startActivity(Intent(this, PlayActivity::class.java).apply {
                    putExtra("old", true)
                    putExtra("name", effect.substrateId.value)
                })
                finish()
            }
        }
    }

    private fun importFailureMessage(
        code: StorageErrorCode,
        fileKind: MainMenuImportedFileKind?
    ): StringResource = when (code) {
        StorageErrorCode.NAME_EXISTS -> if (
            fileKind == MainMenuImportedFileKind.SUBSTRATE
        ) {
            Res.string.freezer_import_name_already_exists_message
        } else {
            Res.string.gene_bank_import_name_already_exists_message
        }
        StorageErrorCode.BAD_NAME -> Res.string.file_operation_invalid_name_message
        StorageErrorCode.TOO_LARGE -> Res.string.file_operation_size_limit_exceeded_message
        StorageErrorCode.NEWER_VERSION -> Res.string.file_operation_newer_version_message
        StorageErrorCode.NOT_FOUND,
        StorageErrorCode.UNSUPPORTED_FORMAT,
        StorageErrorCode.CORRUPTED,
        StorageErrorCode.PERMISSION_REVOKED,
        StorageErrorCode.IO_FAILURE -> Res.string.file_operation_load_failed_message
    }

    private companion object {
        const val STATE_COMPLETED_IMPORT_COMMAND_IDS =
            "main_menu.completed_import_command_ids"
    }
}
