package com.cytarch.cellularity.ui.vulkan

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.CellularityApplication
import com.cytarch.cellularity.platform.render.vulkan.VulkanClearFramePhase
import com.cytarch.cellularity.platform.render.vulkan.VulkanClearFrameStatus
import com.cytarch.cellularity.platform.render.vulkan.VulkanClearFrameView
import com.cytarch.cellularity.platform.render.vulkan.VulkanRuntimeDiagnostics
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_back_action
import com.cytarch.cellularity.shared.generated.resources.common_ok_action
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_failed
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_paused
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_presenting
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_starting
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_surface_lost
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_title
import com.cytarch.cellularity.shared.generated.resources.settings_vulkan_clear_frame_waiting
import com.cytarch.cellularity.shared.generated.resources.settings_performance_hud
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme
import com.cytarch.cellularity.ui.RecoverableAppCompatActivity
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.jetbrains.compose.resources.stringResource

class VulkanClearFrameActivity : RecoverableAppCompatActivity() {
    private var clearFrameView: VulkanClearFrameView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (redirectedToRecovery) return
        enableEdgeToEdge()

        val application = application as CellularityApplication
        setContent {
            val settings by application.settingsRepository.settings.collectAsStateWithLifecycle()
            CellularityTheme(
                themeColors = settings.theme,
                themeComponents = settings.themeComponents
            ) {
                VulkanClearFrameScreen(
                    performanceHudEnabled = settings.performanceHudEnabled,
                    onBack = onBackPressedDispatcher::onBackPressed,
                    registerView = { view ->
                        clearFrameView = view
                        if (lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
                            view.onHostResume()
                        }
                    }
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        clearFrameView?.onHostResume()
    }

    override fun onPause() {
        clearFrameView?.onHostPause()
        super.onPause()
    }

    override fun onDestroy() {
        clearFrameView?.close()
        clearFrameView = null
        super.onDestroy()
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun VulkanClearFrameScreen(
    performanceHudEnabled: Boolean,
    onBack: () -> Unit,
    registerView: (VulkanClearFrameView) -> Unit
) {
    var status by remember { mutableStateOf(VulkanClearFrameStatus()) }
    var showDiagnostics by remember { mutableStateOf(false) }
    var diagnostics by remember { mutableStateOf("") }
    var showPerformanceHud by remember {
        mutableStateOf(performanceHudEnabled)
    }
    val registeredView = remember { AtomicReference<VulkanClearFrameView?>() }
    val coroutineScope = rememberCoroutineScope()
    val performanceHudDescription =
        stringResource(Res.string.settings_performance_hud)

    LaunchedEffect(performanceHudEnabled) {
        showPerformanceHud = performanceHudEnabled
    }

    DisposableEffect(Unit) {
        onDispose {
            registeredView.getAndSet(null)?.close()
        }
    }

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        contentWindowInsets = WindowInsets(0),
        topBar = {
            TopAppBar(
                modifier = Modifier.statusBarsPadding(),
                title = { Text(stringResource(Res.string.settings_vulkan_clear_frame_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(Res.string.common_back_action)
                        )
                    }
                },
                actions = {
                    Switch(
                        checked = showPerformanceHud,
                        onCheckedChange = { showPerformanceHud = it },
                        modifier = Modifier.semantics {
                            contentDescription = performanceHudDescription
                        }
                    )
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f)
                )
            )
        },
        bottomBar = {
            Card(
                onClick = {
                    coroutineScope.launch {
                        diagnostics = withContext(Dispatchers.IO) {
                            VulkanRuntimeDiagnostics.snapshot()
                        }
                        showDiagnostics = diagnostics.isNotBlank()
                    }
                },
                modifier = Modifier
                    .navigationBarsPadding()
                    .padding(12.dp)
                    .fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f)
                )
            ) {
                SelectionContainer {
                    Text(
                        text = statusText(status),
                        modifier = Modifier
                            .padding(horizontal = 16.dp, vertical = 12.dp)
                            .heightIn(max = 160.dp)
                            .verticalScroll(rememberScrollState()),
                        color = MaterialTheme.colorScheme.onSurface,
                        fontFamily = if (status.error.isNotBlank()) {
                            FontFamily.Monospace
                        } else {
                            FontFamily.Default
                        }
                    )
                }
            }
        }
    ) { padding ->
        Box(modifier = Modifier.fillMaxSize()) {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    VulkanClearFrameView(context).also { view ->
                        registeredView.set(view)
                        view.statusReceiver = { status = it }
                        registerView(view)
                    }
                },
                update = { view ->
                    view.performanceHudEnabled = showPerformanceHud
                }
            )
            // Reserve overlay insets without shrinking the Vulkan surface.
            Box(modifier = Modifier.padding(padding))
        }
    }

    if (showDiagnostics) {
        CellularityAlertDialog(
            onDismissRequest = { showDiagnostics = false },
            title = { Text(stringResource(Res.string.settings_vulkan_clear_frame_title)) },
            text = {
                SelectionContainer {
                    Text(
                        text = diagnostics.trimEnd(),
                        modifier = Modifier
                            .heightIn(max = 420.dp)
                            .verticalScroll(rememberScrollState()),
                        fontFamily = FontFamily.Monospace
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = { showDiagnostics = false }) {
                    Text(stringResource(Res.string.common_ok_action))
                }
            }
        )
    }
}

@Composable
private fun statusText(status: VulkanClearFrameStatus): String {
    val summary = when (status.phase) {
        VulkanClearFramePhase.STARTING -> stringResource(
            Res.string.settings_vulkan_clear_frame_starting
        )
        VulkanClearFramePhase.WAITING_FOR_SURFACE -> stringResource(
            Res.string.settings_vulkan_clear_frame_waiting
        )
        VulkanClearFramePhase.READY -> stringResource(
            Res.string.settings_vulkan_clear_frame_presenting,
            status.presentedFrames,
            status.swapchainRecreations
        )
        VulkanClearFramePhase.PAUSED -> stringResource(
            Res.string.settings_vulkan_clear_frame_paused
        )
        VulkanClearFramePhase.SURFACE_LOST -> stringResource(
            Res.string.settings_vulkan_clear_frame_surface_lost
        )
        VulkanClearFramePhase.FAILED -> stringResource(
            Res.string.settings_vulkan_clear_frame_failed
        )
        VulkanClearFramePhase.STOPPED -> stringResource(
            Res.string.settings_vulkan_clear_frame_paused
        )
    }
    return if (status.error.isBlank()) summary else "$summary\n${status.error}"
}
