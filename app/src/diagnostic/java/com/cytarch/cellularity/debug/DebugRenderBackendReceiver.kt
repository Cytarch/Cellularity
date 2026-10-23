package com.cytarch.cellularity.debug

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.cytarch.cellularity.CellularityApplication
import com.cytarch.cellularity.platform.render.RenderBackendDiagnostics
import com.cytarch.cellularity.platform.render.RenderPerformanceTelemetry
import com.cytarch.cellularity.platform.render.vulkan.VulkanRuntimeDiagnostics
import com.cytarch.cellularity.platform.simulation.MicroscopeRuntimeDiagnostics
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import com.cytarch.cellularity.shared.settings.SettingsResult
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/** Local-only ADB hook for backend recreation, parity checks, and profiling. */
internal class DebugRenderBackendReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == ACTION_DUMP_RENDER_DIAGNOSTICS) {
            val pendingResult = goAsync()
            CoroutineScope(SupervisorJob() + Dispatchers.IO).launch {
                try {
                    Log.i(
                        LOG_TAG,
                        "${RenderBackendDiagnostics.snapshot()}\n" +
                            "${RenderPerformanceTelemetry.snapshot()}\n" +
                            "${MicroscopeRuntimeDiagnostics.snapshot()}\n" +
                            VulkanRuntimeDiagnostics.snapshot()
                    )
                } finally {
                    pendingResult.finish()
                }
            }
            return
        }
        if (intent.action == ACTION_START_RENDER_PROFILE) {
            MicroscopeRuntimeDiagnostics.startProfile()
            RenderPerformanceTelemetry.setEnabled(enabled = true, reset = true)
            Log.i(LOG_TAG, "Started render performance profile")
            return
        }
        if (intent.action == ACTION_STOP_RENDER_PROFILE) {
            RenderPerformanceTelemetry.setEnabled(enabled = false)
            Log.i(LOG_TAG, "Stopped render performance profile")
            return
        }
        if (intent.action == ACTION_RESET_RENDER_PROFILE) {
            MicroscopeRuntimeDiagnostics.resetProfile()
            RenderPerformanceTelemetry.reset()
            Log.i(LOG_TAG, "Reset render performance profile")
            return
        }
        if (intent.action != ACTION_SET_RENDER_BACKEND) return
        val preference = when (intent.getStringExtra(EXTRA_BACKEND)) {
            VALUE_OPEN_GL_ES -> AndroidRenderBackendPreference.OPEN_GL_ES
            VALUE_AUTO -> AndroidRenderBackendPreference.AUTO
            VALUE_VULKAN -> AndroidRenderBackendPreference.VULKAN
            else -> {
                Log.e(LOG_TAG, "Rejected unknown render backend")
                return
            }
        }

        val pendingResult = goAsync()
        CoroutineScope(SupervisorJob() + Dispatchers.IO).launch {
            try {
                val application = context.applicationContext as CellularityApplication
                val repository = application.renderBackendSettingsRepository
                val result = repository.writeSettings(
                    repository.settings.value.copy(backend = preference)
                )
                when (result) {
                    is SettingsResult.Success -> {
                        RenderBackendDiagnostics.recordPreference(preference)
                        Log.i(LOG_TAG, "Applied render backend: ${preference.name}")
                    }
                    is SettingsResult.Failure -> Log.e(
                        LOG_TAG,
                        "Could not persist render backend: " +
                            "${result.error.code} ${result.error.detail}"
                    )
                }
            } finally {
                pendingResult.finish()
            }
        }
    }

    private companion object {
        const val LOG_TAG = "CellularityRenderDebug"
        const val ACTION_SET_RENDER_BACKEND =
            "com.cytarch.cellularity.debug.SET_RENDER_BACKEND"
        const val ACTION_DUMP_RENDER_DIAGNOSTICS =
            "com.cytarch.cellularity.debug.DUMP_RENDER_DIAGNOSTICS"
        const val ACTION_START_RENDER_PROFILE =
            "com.cytarch.cellularity.debug.START_RENDER_PROFILE"
        const val ACTION_STOP_RENDER_PROFILE =
            "com.cytarch.cellularity.debug.STOP_RENDER_PROFILE"
        const val ACTION_RESET_RENDER_PROFILE =
            "com.cytarch.cellularity.debug.RESET_RENDER_PROFILE"
        const val EXTRA_BACKEND = "backend"
        const val VALUE_OPEN_GL_ES = "open_gl_es"
        const val VALUE_AUTO = "auto"
        const val VALUE_VULKAN = "vulkan"
    }
}
