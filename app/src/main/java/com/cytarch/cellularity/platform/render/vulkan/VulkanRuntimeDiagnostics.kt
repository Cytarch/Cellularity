package com.cytarch.cellularity.platform.render.vulkan

import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

/**
 * Process diagnostics owner.
 *
 * Compact state is updated without asking native code to serialize a report.
 * A full backend report is requested only by [snapshot] or captured once on a
 * failure path.
 */
internal object VulkanRuntimeDiagnostics {
    private val providerLock = Any()
    private var nextProviderToken = 1L
    private var providerToken = 0L
    private var fullReportProvider: (() -> String?)? = null

    @Volatile
    private var compactState = VulkanClearFrameStatus()
    private val latestFrameOutcome = AtomicInteger(-1)
    private val latestPresentedFrames = AtomicLong()
    private val surfaceRedrawTimeouts = AtomicLong()
    private val surfaceRedrawCounterExhaustions = AtomicLong()

    @Volatile
    private var latestFullReport = "schema=1\nstatus=not_started\nerror=\n"

    fun installFullReportProvider(provider: () -> String?): Long =
        synchronized(providerLock) {
            val token = nextProviderToken
            nextProviderToken = if (token == Long.MAX_VALUE) 1L else token + 1L
            providerToken = token
            fullReportProvider = provider
            token
        }

    fun removeFullReportProvider(token: Long) {
        synchronized(providerLock) {
            if (providerToken != token) return
            providerToken = 0L
            fullReportProvider = null
        }
    }

    fun updateCompact(status: VulkanClearFrameStatus) {
        compactState = status
        latestFrameOutcome.set(status.frameOutcome)
        latestPresentedFrames.set(status.presentedFrames)
    }

    fun recordFrameOutcome(frameOutcome: Int, presentedFrames: Long) {
        latestFrameOutcome.set(frameOutcome)
        latestPresentedFrames.set(presentedFrames)
    }

    fun recordSurfaceRedrawTimeout() {
        surfaceRedrawTimeouts.incrementAndGet()
    }

    fun recordSurfaceRedrawCounterExhausted() {
        surfaceRedrawCounterExhaustions.incrementAndGet()
    }

    fun updateFullReport(report: String) {
        latestFullReport = normalizeReport(report)
    }

    fun snapshot(): String {
        val provider = synchronized(providerLock) { fullReportProvider }
        provider?.let { current ->
            runCatching(current).getOrNull()?.let(::updateFullReport)
        }
        val compact = compactState
        val report = latestFullReport
        return buildString {
            appendLine("Vulkan prepared-frame runtime")
            appendLine("compact_phase=${compact.phase.name.lowercase()}")
            appendLine("compact_frame_outcome=${latestFrameOutcome.get()}")
            appendLine("compact_presented_frames=${latestPresentedFrames.get()}")
            appendLine("compact_swapchain_recreations=${compact.swapchainRecreations}")
            appendLine("compact_failure_code=${compact.failureCode.code}")
            appendLine("compact_diagnostics_revision=${compact.diagnosticsRevision}")
            appendLine("surface_redraw_timeout_count=${surfaceRedrawTimeouts.get()}")
            appendLine(
                "surface_redraw_counter_exhausted_count=" +
                    surfaceRedrawCounterExhaustions.get()
            )
            appendLine("backend_abi_version=${compact.backendAbiVersion}")
            appendLine("backend_api_required_size=${compact.backendApiRequiredSize}")
            appendLine("backend_abi_status=${compact.backendAbiStatus.name.lowercase()}")
            appendLine("backend_report:")
            append(report)
        }
    }

    internal fun resetForTesting() {
        synchronized(providerLock) {
            providerToken = 0L
            fullReportProvider = null
            nextProviderToken = 1L
        }
        compactState = VulkanClearFrameStatus()
        latestFrameOutcome.set(-1)
        latestPresentedFrames.set(0L)
        surfaceRedrawTimeouts.set(0L)
        surfaceRedrawCounterExhaustions.set(0L)
        latestFullReport = "schema=1\nstatus=not_started\nerror=\n"
    }

    private fun normalizeReport(report: String): String = buildString {
        append(report.trimEnd())
        appendLine()
    }
}
