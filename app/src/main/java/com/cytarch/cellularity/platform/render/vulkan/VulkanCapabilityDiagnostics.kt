package com.cytarch.cellularity.platform.render.vulkan

internal object VulkanCapabilityDiagnostics {
    private const val NOT_PROBED = "Vulkan capability probe\nstatus=not_probed\n"

    @Volatile
    private var latest = NOT_PROBED

    fun update(report: VulkanCapabilityReport) {
        latest = buildString {
            appendLine("Vulkan capability probe")
            append(report.raw.trimEnd())
            appendLine()
        }
    }

    fun snapshot(): String = latest
}
