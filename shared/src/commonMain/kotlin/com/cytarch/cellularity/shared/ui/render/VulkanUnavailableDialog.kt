package com.cytarch.cellularity.shared.ui.render

import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_cancel_action
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend_use_gles_action
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend_vulkan_unavailable_message
import com.cytarch.cellularity.shared.generated.resources.settings_render_backend_vulkan_unavailable_title
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import org.jetbrains.compose.resources.stringResource

/** Displays the platform's renderer-availability decision without owning backend selection. */
@Composable
fun VulkanUnavailableDialog(
    detail: String,
    onUseOpenGlEs: () -> Unit,
    onCancel: () -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = onCancel,
        title = {
            Text(stringResource(Res.string.settings_render_backend_vulkan_unavailable_title))
        },
        text = {
            Text(
                stringResource(
                    Res.string.settings_render_backend_vulkan_unavailable_message,
                    detail
                )
            )
        },
        confirmButton = {
            TextButton(onClick = onUseOpenGlEs) {
                Text(stringResource(Res.string.settings_render_backend_use_gles_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onCancel) {
                Text(stringResource(Res.string.common_cancel_action))
            }
        }
    )
}
