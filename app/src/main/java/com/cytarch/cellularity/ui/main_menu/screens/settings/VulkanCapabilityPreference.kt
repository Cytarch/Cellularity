package com.cytarch.cellularity.ui.main_menu.screens.settings

import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.platform.render.vulkan.AndroidVulkanCapabilityRepository
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityProbeState
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityReport
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityStatus
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_ok_action
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_checking
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_failed
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_run
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_retry_action
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_supported
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_title
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_capability_unsupported
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsInfoPreference
import org.jetbrains.compose.resources.stringResource

@Composable
internal fun VulkanCapabilityPreference(
    repository: AndroidVulkanCapabilityRepository,
    probeAutomatically: Boolean
) {
    val state by repository.state.collectAsStateWithLifecycle()
    var showDetails by remember { mutableStateOf(false) }
    var probeRequested by remember { mutableStateOf(probeAutomatically) }

    LaunchedEffect(probeAutomatically) {
        if (probeAutomatically) probeRequested = true
    }

    if (
        state !is VulkanCapabilityProbeState.Complete &&
        (probeRequested || state == VulkanCapabilityProbeState.Running)
    ) {
        VulkanProbeSurface(repository = repository, state = state)
    }

    val complete = state as? VulkanCapabilityProbeState.Complete
    SettingsInfoPreference(
        title = stringResource(Res.string.settings_vulkan_capability_title),
        summary = if (
            complete == null &&
            !probeRequested &&
            state == VulkanCapabilityProbeState.AwaitingSurface
        ) {
            stringResource(Res.string.settings_vulkan_capability_run)
        } else {
            capabilitySummary(complete?.report)
        },
        onClick = when {
            complete != null -> ({ showDetails = true })
            state == VulkanCapabilityProbeState.AwaitingSurface -> ({ probeRequested = true })
            else -> null
        }
    )

    if (showDetails && complete != null) {
        CellularityAlertDialog(
            onDismissRequest = { showDetails = false },
            title = { Text(stringResource(Res.string.settings_vulkan_capability_title)) },
            text = {
                SelectionContainer {
                    Text(
                        text = complete.report.raw.trimEnd(),
                        modifier = Modifier
                            .heightIn(max = 420.dp)
                            .verticalScroll(rememberScrollState()),
                        fontFamily = FontFamily.Monospace
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = { showDetails = false }) {
                    Text(stringResource(Res.string.common_ok_action))
                }
            },
            dismissButton = {
                TextButton(onClick = {
                    showDetails = false
                    probeRequested = true
                    repository.retry()
                }) {
                    Text(
                        stringResource(
                            Res.string.settings_vulkan_capability_retry_action
                        )
                    )
                }
            }
        )
    }
}

@Composable
private fun capabilitySummary(report: VulkanCapabilityReport?): String {
    if (report == null) {
        return stringResource(Res.string.settings_vulkan_capability_checking)
    }
    return when (report.status) {
        VulkanCapabilityStatus.SUPPORTED -> {
            val device = report.devices.firstOrNull { it.compatible }
                ?: report.devices.firstOrNull()
            stringResource(
                Res.string.settings_vulkan_capability_supported,
                device?.name.orEmpty(),
                device?.apiVersion ?: report.instanceApiVersion
            )
        }
        VulkanCapabilityStatus.UNSUPPORTED -> stringResource(
            Res.string.settings_vulkan_capability_unsupported
        )
        VulkanCapabilityStatus.FAILED -> stringResource(
            Res.string.settings_vulkan_capability_failed
        )
    }
}

@Composable
internal fun VulkanProbeSurface(
    repository: AndroidVulkanCapabilityRepository,
    state: VulkanCapabilityProbeState
) {
    AndroidView(
        modifier = Modifier
            .size(1.dp)
            .alpha(0f),
        factory = { context ->
            TextureView(context).apply {
                isOpaque = false
                surfaceTextureListener = object : TextureView.SurfaceTextureListener {
                    override fun onSurfaceTextureAvailable(
                        surfaceTexture: SurfaceTexture,
                        width: Int,
                        height: Int
                    ) {
                        repository.submitSurface(surfaceTexture)
                    }

                    override fun onSurfaceTextureSizeChanged(
                        surfaceTexture: SurfaceTexture,
                        width: Int,
                        height: Int
                    ) = Unit

                    override fun onSurfaceTextureDestroyed(
                        surfaceTexture: SurfaceTexture
                    ): Boolean = true

                    override fun onSurfaceTextureUpdated(
                        surfaceTexture: SurfaceTexture
                    ) = Unit
                }
            }
        },
        update = { textureView ->
            if (state == VulkanCapabilityProbeState.AwaitingSurface
                && textureView.isAvailable
            ) {
                textureView.surfaceTexture?.let(repository::submitSurface)
            }
        }
    )
}

private fun AndroidVulkanCapabilityRepository.submitSurface(
    surfaceTexture: SurfaceTexture
) {
    val surface = runCatching {
        surfaceTexture.setDefaultBufferSize(1, 1)
        Surface(surfaceTexture)
    }.getOrElse { failure ->
        reportSurfaceFailure(failure)
        return
    }
    if (!requestProbe(surface)) {
        runCatching(surface::release)
    }
}
