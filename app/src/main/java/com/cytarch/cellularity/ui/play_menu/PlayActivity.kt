package com.cytarch.cellularity.ui.play_menu

import android.os.Build
import android.os.Bundle
import android.view.Surface as AndroidSurface
import android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
import android.widget.Toast
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.cytarch.cellularity.CellularityApplication
import com.cytarch.cellularity.CrashLogger
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.render.RenderBackendDecision
import com.cytarch.cellularity.platform.render.RenderBackendDiagnostics
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderBackendResolver
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityProbeState
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.file_operation_save_failed_message
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import com.cytarch.cellularity.shared.settings.SettingsResult
import com.cytarch.cellularity.shared.ui.play.PlayEffect
import com.cytarch.cellularity.shared.ui.play.PlayFinishReason
import com.cytarch.cellularity.shared.ui.play.PlayPhase
import com.cytarch.cellularity.shared.ui.render.VulkanUnavailableDialog
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme
import com.cytarch.cellularity.ui.RecoverableAppCompatActivity
import com.cytarch.cellularity.ui.main_menu.screens.settings.VulkanProbeSurface
import com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeEditorViewModel
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.getString

/** Android window and lifecycle adapter for the retained Play store/session. */
class PlayActivity : RecoverableAppCompatActivity() {
    private val viewModel: PlayActivityViewModel by viewModels()
    private val genomeEditorViewModel: GenomeEditorViewModel by viewModels()
    private val fileRepository: AndroidCellularityFileRepository by lazy {
        AndroidCellularityFileRepository(applicationContext)
    }
    private val cellularityApplication: CellularityApplication
        get() = application as CellularityApplication
    private val settingsRepository
        get() = cellularityApplication.settingsRepository
    private val displaySettingsRepository
        get() = cellularityApplication.displaySettingsRepository
    private val renderBackendSettingsRepository
        get() = cellularityApplication.renderBackendSettingsRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (redirectedToRecovery) return

        configureWindow()
        val parsed = when (val result = PlayLaunchParser.parse(intent, savedInstanceState)) {
            is PlayLaunchParseResult.Valid -> result.launch
            is PlayLaunchParseResult.Invalid -> {
                lifecycleScope.launch {
                    showPlayFeedback(this@PlayActivity, result.feedback)
                    finish()
                }
                return
            }
        }
        val store = viewModel.initialize(
            context = applicationContext,
            fileRepository = fileRepository,
            source = parsed.source,
            restoredUi = parsed.restoredUi
        )

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                store.effects.collect(::handleEffect)
            }
        }

        val phoneWidthMeter = calculatePhoneWidthMeters()
        setContentView(ComposeView(this).apply {
            setContent {
                val settings by settingsRepository.settings.collectAsStateWithLifecycle()
                val displaySettings by displaySettingsRepository.settings.collectAsStateWithLifecycle()
                val renderBackendSettings by renderBackendSettingsRepository.settings
                    .collectAsStateWithLifecycle()
                val capabilityState by cellularityApplication.vulkanCapabilityRepository.state
                    .collectAsStateWithLifecycle()
                val sessionVulkanFailure by cellularityApplication.renderBackendSessionState
                    .vulkanFailure
                    .collectAsStateWithLifecycle()
                val state by store.state.collectAsStateWithLifecycle()
                val capability = (capabilityState as? VulkanCapabilityProbeState.Complete)?.report
                val backendDecision = remember(
                    renderBackendSettings.backend,
                    capability,
                    sessionVulkanFailure
                ) {
                    RenderBackendResolver.resolve(
                        preference = renderBackendSettings.backend,
                        capability = capability,
                        sessionVulkanFailure = sessionVulkanFailure
                    )
                }
                val onVulkanRuntimeFailure = remember(cellularityApplication) {
                    cellularityApplication.renderBackendSessionState::recordVulkanFailure
                }
                var presentedRenderBackend by remember {
                    mutableStateOf<RenderBackendKind?>(null)
                }

                LaunchedEffect(backendDecision) {
                    RenderBackendDiagnostics.recordDecision(backendDecision)
                    CrashLogger.updateNativeRenderBackendDiagnostics(backendDecision)
                    val target = (backendDecision as? RenderBackendDecision.Selected)?.backend
                    if (target == null) {
                        presentedRenderBackend = null
                    } else if (presentedRenderBackend != target) {
                        // Dispose the old SurfaceView in its own composition
                        // frame. Android can otherwise attach the replacement
                        // GLSurfaceView before the old Vulkan surface has left
                        // the window, and never issue surfaceCreated for it.
                        presentedRenderBackend = null
                        withFrameNanos { }
                        presentedRenderBackend = target
                    }
                }
                CellularityTheme(
                    themeColors = settings.theme,
                    themeComponents = settings.themeComponents
                ) {
                    Box(modifier = Modifier.fillMaxSize()) {
                        if (backendDecision is RenderBackendDecision.AwaitingCapability) {
                            VulkanProbeSurface(
                                repository = cellularityApplication.vulkanCapabilityRepository,
                                state = capabilityState
                            )
                        }
                        when (state.phase) {
                            PlayPhase.READY -> when (backendDecision) {
                                is RenderBackendDecision.Selected -> PlayContent(
                                    state = state,
                                    viewModel = viewModel,
                                    genomeEditorViewModel = genomeEditorViewModel,
                                    fileRepository = fileRepository,
                                    settings = settings,
                                    displaySettings = displaySettings,
                                    phoneWidthMeter = phoneWidthMeter,
                                    renderBackendKind = presentedRenderBackend,
                                    onRenderBackendFailure = onVulkanRuntimeFailure
                                )
                                is RenderBackendDecision.AwaitingCapability -> LoadingContent()
                                is RenderBackendDecision.VulkanUnavailable -> {
                                    VulkanUnavailableDialog(
                                        detail = backendDecision.detail,
                                        onUseOpenGlEs = ::selectOpenGlEs,
                                        onCancel = ::finish
                                    )
                                }
                            }
                            PlayPhase.NOT_STARTED,
                            PlayPhase.OPENING,
                            PlayPhase.FAILED -> LoadingContent()
                        }
                    }
                }
            }
        })
    }

    override fun onResume() {
        super.onResume()
        requestMaximumWindowRefreshRate()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        viewModel.persistenceState()?.let { PlayLaunchParser.save(outState, it) }
        super.onSaveInstanceState(outState)
    }

    private fun configureWindow() {
        enableEdgeToEdge()
        requestMaximumWindowRefreshRate()
        WindowCompat.getInsetsController(window, window.decorView).apply {
            systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
        if (settingsRepository.settings.value.keepOn) {
            window.addFlags(FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun calculatePhoneWidthMeters(): Float {
        val metrics = resources.displayMetrics
        val dotsPerInch = metrics.density * 160.0
        return (0.0254 * metrics.widthPixels / dotsPerInch).toFloat()
    }

    private suspend fun handleEffect(effect: PlayEffect) {
        when (effect) {
            is PlayEffect.ShowNotice -> showPlayNotice(this, effect.notice)
            is PlayEffect.Finish -> {
                if (effect.reason == PlayFinishReason.NEWER_VERSION) {
                    setResult(RESULT_OK)
                }
                finish()
            }
        }
    }

    private fun selectOpenGlEs() {
        lifecycleScope.launch {
            val current = renderBackendSettingsRepository.settings.value
            when (
                renderBackendSettingsRepository.writeSettings(
                    current.copy(backend = AndroidRenderBackendPreference.OPEN_GL_ES)
                )
            ) {
                is SettingsResult.Success -> RenderBackendDiagnostics.recordPreference(
                    AndroidRenderBackendPreference.OPEN_GL_ES
                )
                is SettingsResult.Failure -> Toast.makeText(
                    this@PlayActivity,
                    getString(Res.string.file_operation_save_failed_message),
                    Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    private fun requestMaximumWindowRefreshRate() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        val maxRefreshRate = display?.supportedModes
            ?.maxOfOrNull { it.refreshRate }
            ?: display?.refreshRate
            ?: return
        val setFrameRateApplied = runCatching {
            window.javaClass
                .getMethod(
                    "setFrameRate",
                    Float::class.javaPrimitiveType,
                    Int::class.javaPrimitiveType
                )
                .invoke(
                    window,
                    maxRefreshRate,
                    AndroidSurface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE
                )
        }.isSuccess
        if (!setFrameRateApplied) {
            window.attributes = window.attributes.apply {
                preferredRefreshRate = maxRefreshRate
            }
        }
    }
}

@Composable
private fun LoadingContent() {
    Box(
        modifier = Modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        CircularProgressIndicator()
    }
}
