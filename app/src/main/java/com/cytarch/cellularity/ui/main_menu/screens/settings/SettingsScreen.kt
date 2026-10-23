package com.cytarch.cellularity.ui.main_menu.screens.settings

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.FileProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.CellularityApplication
import com.cytarch.cellularity.CrashLogger
import com.cytarch.cellularity.DiagnosticsShare
import com.cytarch.cellularity.StoredCrashReport
import com.cytarch.cellularity.platform.localization.activeResourceLocale
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.diagnostics_export_failed_message
import com.cytarch.cellularity.shared.generated.resources.diagnostics_file_size_bytes_format
import com.cytarch.cellularity.shared.generated.resources.diagnostics_file_size_kilobytes_format
import com.cytarch.cellularity.shared.generated.resources.diagnostics_file_size_megabytes_format
import com.cytarch.cellularity.shared.generated.resources.file_operation_save_failed_message
import com.cytarch.cellularity.shared.generated.resources.settings_avoid_camera_cutout
import com.cytarch.cellularity.shared.generated.resources.settings_avoid_camera_cutout_summary
import com.cytarch.cellularity.shared.generated.resources.settings_theme_import_failed_message
import com.cytarch.cellularity.shared.generated.resources.settings_theme_share_failed_message
import com.cytarch.cellularity.shared.generated.resources.sharing_email_chooser_title
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import com.cytarch.cellularity.shared.settings.CellularityThemeFileJson
import com.cytarch.cellularity.shared.settings.SettingsResult
import com.cytarch.cellularity.shared.ui.main_menu.settings.CrashReportItem
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsEffect
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsSwitchPreference
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsStore
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen as SharedSettingsScreen
import java.io.File
import java.text.DateFormat
import java.util.Date
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.jetbrains.compose.resources.stringResource

@Composable
fun SettingsScreen() {
    val context = LocalContext.current
    val activity = remember(context) { context.findActivity() }
    val application = remember(context) {
        context.applicationContext as CellularityApplication
    }
    val store = remember(application.settingsRepository) {
        SettingsStore(application.settingsRepository)
    }
    val displaySettingsRepository = application.displaySettingsRepository
    val renderBackendSettingsRepository = application.renderBackendSettingsRepository
    val settings by store.state.collectAsStateWithLifecycle()
    val displaySettings by displaySettingsRepository.settings.collectAsStateWithLifecycle()
    val renderBackendSettings by renderBackendSettingsRepository.settings
        .collectAsStateWithLifecycle()
    val coroutineScope = rememberCoroutineScope()
    val storedCrashReports = remember(context) {
        CrashLogger.listReports(context.applicationContext)
    }
    val crashReportDateFormatter = remember(context) {
        DateFormat.getDateTimeInstance(
            DateFormat.MEDIUM,
            DateFormat.MEDIUM,
            context.activeResourceLocale()
        )
    }
    val crashReports = storedCrashReports.map { report ->
        CrashReportItem(
            fileName = report.fileName,
            dateTime = crashReportDateFormatter.format(Date(report.lastModified)),
            sizeText = crashReportFileSize(report)
        )
    }
    val themeImportFailedMessage = stringResource(
        Res.string.settings_theme_import_failed_message
    )
    val saveFailedMessage = stringResource(Res.string.file_operation_save_failed_message)
    val diagnosticsExportFailedMessage = stringResource(
        Res.string.diagnostics_export_failed_message
    )
    val themeShareFailedMessage = stringResource(
        Res.string.settings_theme_share_failed_message
    )
    val sharingChooserTitle = stringResource(Res.string.sharing_email_chooser_title)
    val themeImportLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) {
            return@rememberLauncherForActivityResult
        }
        coroutineScope.launch {
            runCatching {
                withContext(Dispatchers.IO) {
                    val text = context.contentResolver.openInputStream(uri)?.use { input ->
                        input.bufferedReader().readText()
                    } ?: error("Theme file is empty")
                    CellularityThemeFileJson.decode(text)
                }
            }.onSuccess { theme ->
                store.dispatch(SettingsAction.SetTheme(theme.colors, theme.components))
            }.onFailure {
                Toast.makeText(
                    context,
                    themeImportFailedMessage,
                    Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    val dispatchSettings: (SettingsAction) -> Unit = { action ->
        coroutineScope.launch {
            store.dispatch(action)
        }
    }

    LaunchedEffect(store) {
        store.effects.collect { effect ->
            when (effect) {
                is SettingsEffect.SaveFailed -> Toast.makeText(
                    context,
                    saveFailedMessage,
                    Toast.LENGTH_LONG
                ).show()
                SettingsEffect.RecreateUi -> activity?.recreate()
            }
        }
    }

    SharedSettingsScreen(
        state = settings,
        onAction = dispatchSettings,
        crashReports = crashReports,
        onExportDiagnostics = {
            runCatching {
                context.startActivity(DiagnosticsShare.createChooserIntent(context))
            }.onFailure {
                Toast.makeText(
                    context,
                    diagnosticsExportFailedMessage,
                    Toast.LENGTH_LONG
                ).show()
            }
        },
        onExportTheme = {
            coroutineScope.launch {
                runCatching {
                    val file = withContext(Dispatchers.IO) {
                        context.writeCurrentThemeFile(
                            store.state.value.theme,
                            store.state.value.themeComponents
                        )
                    }
                    val intent = Intent(Intent.ACTION_SEND).apply {
                        type = "application/octet-stream"
                        putExtra(
                            Intent.EXTRA_STREAM,
                            FileProvider.getUriForFile(
                                context,
                                "${context.packageName}.fileprovider",
                                file
                            )
                        )
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    }
                    context.startActivity(
                        Intent.createChooser(intent, sharingChooserTitle)
                    )
                }.onFailure {
                    Toast.makeText(
                        context,
                        themeShareFailedMessage,
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        },
        onImportTheme = {
            themeImportLauncher.launch(arrayOf("*/*"))
        },
        additionalGeneralContent = {
            SettingsSwitchPreference(
                title = stringResource(Res.string.settings_avoid_camera_cutout),
                summary = stringResource(Res.string.settings_avoid_camera_cutout_summary),
                checked = displaySettings.avoidCameraCutout,
                onCheckedChange = { avoidCameraCutout ->
                    coroutineScope.launch {
                        val updated = displaySettings.copy(
                            avoidCameraCutout = avoidCameraCutout
                        )
                        when (displaySettingsRepository.writeSettings(updated)) {
                            is SettingsResult.Success -> activity?.recreate()
                            is SettingsResult.Failure -> Toast.makeText(
                                context,
                                saveFailedMessage,
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    }
                }
            )
        },
        additionalExperimentalContent = {
            RenderBackendPreference(renderBackendSettingsRepository)
            VulkanCapabilityPreference(
                repository = application.vulkanCapabilityRepository,
                probeAutomatically =
                    renderBackendSettings.backend != AndroidRenderBackendPreference.OPEN_GL_ES
            )
            VulkanClearFramePreference(application.vulkanCapabilityRepository)
        }
    )
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

@Composable
private fun crashReportFileSize(report: StoredCrashReport): String {
    val bytes = report.sizeBytes
    return when {
        bytes < 1024 -> stringResource(
            Res.string.diagnostics_file_size_bytes_format,
            bytes
        )
        bytes < 1024 * 1024 -> stringResource(
            Res.string.diagnostics_file_size_kilobytes_format,
            formatLocalizedNumber(
                bytes / 1024.0,
                minimumFractionDigits = 1,
                maximumFractionDigits = 1
            )
        )
        else -> stringResource(
            Res.string.diagnostics_file_size_megabytes_format,
            formatLocalizedNumber(
                bytes / (1024.0 * 1024.0),
                minimumFractionDigits = 1,
                maximumFractionDigits = 1
            )
        )
    }
}

private fun Context.writeCurrentThemeFile(
    theme: com.cytarch.cellularity.shared.settings.CellularityThemeColors,
    components: com.cytarch.cellularity.shared.settings.CellularityThemeComponents
): File {
    val safeName = theme.name
        .ifBlank { "cellularity_theme" }
        .replace(Regex("[^A-Za-z0-9._-]+"), "_")
        .trim('_')
        .ifBlank { "cellularity_theme" }
    val exportDirectory = File(cacheDir, "exports").apply {
        check(isDirectory || mkdirs()) { "Could not create export directory" }
    }
    val file = File(exportDirectory, "$safeName.cltheme")
    file.writeText(CellularityThemeFileJson.encode(theme, components))
    return file
}
