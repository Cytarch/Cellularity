package com.cytarch.cellularity.ui.main_menu.screens.settings

import android.content.Intent
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.platform.render.vulkan.AndroidVulkanCapabilityRepository
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityProbeState
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityStatus
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_summary
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_unavailable
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_title
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsInfoPreference
import com.cytarch.cellularity.ui.vulkan.VulkanClearFrameActivity
import org.jetbrains.compose.resources.stringResource

@Composable
internal fun VulkanClearFramePreference(
    repository: AndroidVulkanCapabilityRepository
) {
    val context = LocalContext.current
    val state by repository.state.collectAsStateWithLifecycle()
    val report = (state as? VulkanCapabilityProbeState.Complete)?.report
    val supported = report?.status == VulkanCapabilityStatus.SUPPORTED

    SettingsInfoPreference(
        title = stringResource(Res.string.settings_vulkan_clear_frame_title),
        summary = stringResource(
            if (supported) {
                Res.string.settings_vulkan_clear_frame_summary
            } else {
                Res.string.settings_vulkan_clear_frame_unavailable
            }
        ),
        onClick = if (supported) {
            {
                context.startActivity(Intent(context, VulkanClearFrameActivity::class.java))
            }
        } else {
            null
        }
    )
}
