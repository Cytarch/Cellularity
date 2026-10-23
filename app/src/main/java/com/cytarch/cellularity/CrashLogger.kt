package com.cytarch.cellularity

import android.content.Context
import android.os.Build
import com.cytarch.cellularity.platform.render.RenderBackendDiagnostics
import com.cytarch.cellularity.platform.render.RenderBackendDecision
import com.cytarch.cellularity.platform.render.RenderBackendFallbackReason
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityDiagnostics
import com.cytarch.cellularity.platform.render.vulkan.VulkanRuntimeDiagnostics
import java.io.File
import java.io.PrintWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import kotlin.system.exitProcess

object CrashLogger {
    private const val DIAGNOSTICS_DIRECTORY_NAME = "diagnostics"
    private const val EXPORT_DIRECTORY_NAME = "exports"
    private const val EXPORT_FILE_NAME = "cellularity-diagnostics.txt"
    private const val STACK_TRACE_CHARACTER_LIMIT = 256_000
    private val processAcknowledgedReportIds = mutableSetOf<String>()

    @Synchronized
    fun install(context: Context) {
        val appContext = context.applicationContext
        val store = CrashReportStore(diagnosticsDirectory(appContext))
        runCatching { store.prepare() }

        val previousHandler = Thread.getDefaultUncaughtExceptionHandler()
        if (previousHandler !is CrashExceptionHandler) {
            Thread.setDefaultUncaughtExceptionHandler(
                CrashExceptionHandler(
                    previousHandler = previousHandler,
                    reportWriter = { thread, throwable ->
                        writeCrashReport(appContext, thread, throwable)
                    },
                    terminateProcess = { code -> exitProcess(code) }
                )
            )
        }

        runCatching {
            val directory = diagnosticsDirectory(appContext)
            check(directory.isDirectory || directory.mkdirs())
            System.loadLibrary("cellularity_diagnostics")
            nativeInstallNativeCrashHandler(
                directory.absolutePath,
                appVersion(appContext),
                deviceDescription(),
                Build.SUPPORTED_ABIS.joinToString()
            )
        }
    }

    internal fun listReports(context: Context): List<StoredCrashReport> {
        return runCatching {
            CrashReportStore(diagnosticsDirectory(context.applicationContext)).listReports()
        }.getOrDefault(emptyList())
    }

    @Synchronized
    internal fun listPendingRecoveryReports(context: Context): List<StoredCrashReport> {
        val appContext = context.applicationContext
        return runCatching {
            val reports = CrashReportStore(diagnosticsDirectory(appContext)).listReports()
            recoveryStore(appContext).pendingReports(reports).filterNot { report ->
                CrashRecoveryStore.identityOf(report) in processAcknowledgedReportIds
            }
        }.getOrDefault(emptyList())
    }

    @Synchronized
    internal fun acknowledgeRecoveryReports(
        context: Context,
        reports: List<StoredCrashReport>
    ) {
        if (reports.isEmpty()) return
        processAcknowledgedReportIds += reports.map(CrashRecoveryStore::identityOf)

        val appContext = context.applicationContext
        val currentReports = CrashReportStore(diagnosticsDirectory(appContext)).listReports()
        recoveryStore(appContext).acknowledge(currentReports)
    }

    internal fun createDiagnosticsExport(context: Context): File {
        val appContext = context.applicationContext
        val exportFile = File(
            File(appContext.cacheDir, EXPORT_DIRECTORY_NAME),
            EXPORT_FILE_NAME
        )
        CrashReportStore(diagnosticsDirectory(appContext)).writeExport(
            destination = exportFile,
            runtimeDiagnostics = buildString {
                append(RenderBackendDiagnostics.snapshot())
                appendLine()
                append(VulkanCapabilityDiagnostics.snapshot())
                appendLine()
                append(VulkanRuntimeDiagnostics.snapshot())
            }
        )
        return exportFile
    }

    internal fun diagnosticsDirectory(context: Context): File {
        return File(context.noBackupFilesDir, DIAGNOSTICS_DIRECTORY_NAME)
    }

    internal fun updateNativeRenderBackendDiagnostics(decision: RenderBackendDecision) {
        val backend = when (decision) {
            is RenderBackendDecision.Selected -> when (decision.backend) {
                RenderBackendKind.OPEN_GL_ES -> 1
                RenderBackendKind.VULKAN -> 2
            }
            is RenderBackendDecision.AwaitingCapability,
            is RenderBackendDecision.VulkanUnavailable -> 0
        }
        val fallback = when (decision) {
            is RenderBackendDecision.Selected -> decision.fallbackReason
            is RenderBackendDecision.VulkanUnavailable -> decision.reason
            is RenderBackendDecision.AwaitingCapability -> null
        }.toNativeCode()
        runCatching { nativeUpdateRenderBackendDiagnostics(backend, fallback) }
    }

    private fun recoveryStore(context: Context): CrashRecoveryStore {
        return CrashRecoveryStore(
            File(diagnosticsDirectory(context), CrashRecoveryStore.STATE_FILE_NAME)
        )
    }

    private fun writeCrashReport(context: Context, thread: Thread, throwable: Throwable) {
        val timestamp = utcTimestamp()
        val report = buildCrashReport(context, thread, throwable, timestamp)
        CrashReportStore(diagnosticsDirectory(context)).writeJavaReport(
            fileName = "java-crash-$timestamp.txt",
            report = report
        )
    }

    private fun buildCrashReport(
        context: Context,
        thread: Thread,
        throwable: Throwable,
        timestamp: String
    ): String = buildString {
        appendLine("Cellularity Java/Kotlin diagnostics report")
        appendLine("timeUtc=${timestamp}Z")
        appendLine("package=${safeLine(context.packageName)}")
        appendLine("version=${safeLine(appVersion(context))}")
        appendLine("android=${safeLine(Build.VERSION.RELEASE)} sdk=${Build.VERSION.SDK_INT}")
        appendLine("device=${safeLine(deviceDescription())}")
        appendLine("abi=${safeLine(Build.SUPPORTED_ABIS.joinToString())}")
        appendLine("thread=${safeLine(thread.name)} id=${threadIdentifier(thread)}")
        appendLine()
        append(RenderBackendDiagnostics.snapshot())
        appendLine()
        append(VulkanCapabilityDiagnostics.snapshot())
        appendLine()
        append(VulkanRuntimeDiagnostics.snapshot())
        appendLine()
        append(boundedStackTrace(throwable))
    }

    private fun appVersion(context: Context): String {
        return try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            val code = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                info.longVersionCode.toString()
            } else {
                @Suppress("DEPRECATION")
                info.versionCode.toString()
            }
            "${info.versionName} ($code)"
        } catch (_: Throwable) {
            "unknown"
        }
    }

    private fun deviceDescription(): String {
        return "${Build.MANUFACTURER} ${Build.MODEL}"
    }

    private fun boundedStackTrace(throwable: Throwable): String {
        val writer = HeadTailTextWriter(STACK_TRACE_CHARACTER_LIMIT)
        PrintWriter(writer).use { printWriter -> throwable.printStackTrace(printWriter) }
        return writer.result()
    }

    private fun safeLine(value: String): String {
        return value.replace(Regex("[\\r\\n\\t]+"), " ").take(512)
    }

    @Suppress("DEPRECATION")
    private fun threadIdentifier(thread: Thread): Long = thread.id

    private fun utcTimestamp(): String {
        return SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.US).apply {
            timeZone = TimeZone.getTimeZone("UTC")
        }.format(Date())
    }

    private external fun nativeInstallNativeCrashHandler(
        diagnosticsDirectoryPath: String,
        appVersion: String,
        device: String,
        supportedAbis: String
    )

    private external fun nativeUpdateRenderBackendDiagnostics(
        backend: Int,
        fallbackReason: Int
    )
}

private fun RenderBackendFallbackReason?.toNativeCode(): Int = when (this) {
    null -> 0
    RenderBackendFallbackReason.CAPABILITY_UNSUPPORTED -> 1
    RenderBackendFallbackReason.CAPABILITY_PROBE_FAILED -> 2
    RenderBackendFallbackReason.VULKAN_RUNTIME_FAILED -> 3
}
