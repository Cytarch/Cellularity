package com.cytarch.cellularity.ui.main_menu.screens.settings

import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.platform.render.RenderBackendDiagnostics
import com.cytarch.cellularity.platform.settings.AndroidRenderBackendSettingsRepository
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_cancel_action
import com.cytarch.cellularity.shared.generated.resources.file_operation_save_failed_message
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend_auto
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend_open_gl_es
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend_vulkan
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import com.cytarch.cellularity.shared.settings.SettingsResult
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsListPreference
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsOption
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.stringResource

@Composable
internal fun RenderBackendPreference(
    repository: AndroidRenderBackendSettingsRepository
) {
    val context = LocalContext.current
    val settings by repository.settings.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val saveFailedMessage = stringResource(Res.string.file_operation_save_failed_message)
    val options = listOf(
        SettingsOption(
            value = AndroidRenderBackendPreference.OPEN_GL_ES.name,
            label = stringResource(Res.string.settings_render_backend_open_gl_es)
        ),
        SettingsOption(
            value = AndroidRenderBackendPreference.AUTO.name,
            label = stringResource(Res.string.settings_render_backend_auto)
        ),
        SettingsOption(
            value = AndroidRenderBackendPreference.VULKAN.name,
            label = stringResource(Res.string.settings_render_backend_vulkan)
        )
    )

    SettingsListPreference(
        title = stringResource(Res.string.settings_render_backend),
        currentValue = settings.backend.name,
        options = options,
        cancel = stringResource(Res.string.common_cancel_action),
        onValueChange = { value ->
            val backend = AndroidRenderBackendPreference.valueOf(value)
            scope.launch {
                when (repository.writeSettings(settings.copy(backend = backend))) {
                    is SettingsResult.Success -> {
                        RenderBackendDiagnostics.recordPreference(backend)
                    }
                    is SettingsResult.Failure -> Toast.makeText(
                        context,
                        saveFailedMessage,
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        }
    )
}
