package com.cytarch.cellularity.ui.recovery

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import com.cytarch.cellularity.CrashLogger
import com.cytarch.cellularity.DiagnosticsShare
import com.cytarch.cellularity.R
import com.cytarch.cellularity.StoredCrashReport
import com.cytarch.cellularity.platform.localization.activeResourceLocale
import java.util.Date

class RecoveryActivity : Activity() {
    private var pendingReports: List<StoredCrashReport> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        pendingReports = CrashLogger.listPendingRecoveryReports(applicationContext)
        if (pendingReports.isEmpty()) {
            openMainApplication()
            return
        }
        setContentView(createRecoveryContent())
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
    }

    private fun createRecoveryContent(): ScrollView {
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(24), dp(48), dp(24), dp(32))
        }

        content.addView(TextView(this).apply {
            setText(R.string.recovery_title)
            textSize = 24f
            setTypeface(typeface, Typeface.BOLD)
        }, matchWidth())

        content.addView(TextView(this).apply {
            text = resources.getQuantityString(
                R.plurals.recovery_report_count,
                pendingReports.size,
                pendingReports.size
            )
            textSize = 17f
        }, matchWidth(topMargin = 20))

        content.addView(TextView(this).apply {
            text = getString(
                R.string.recovery_latest_report_format,
                formatTimestamp(pendingReports.first().lastModified)
            )
            textSize = 14f
        }, matchWidth(topMargin = 8))

        content.addView(TextView(this).apply {
            setText(R.string.recovery_description)
            textSize = 16f
        }, matchWidth(topMargin = 20))

        content.addView(TextView(this).apply {
            setText(R.string.recovery_privacy_notice)
            textSize = 14f
        }, matchWidth(topMargin = 12))

        content.addView(Button(this).apply {
            setText(R.string.recovery_export_diagnostics_action)
            isAllCaps = false
            setOnClickListener { exportDiagnostics() }
        }, matchWidth(topMargin = 28))

        content.addView(Button(this).apply {
            setText(R.string.recovery_continue_to_app_action)
            isAllCaps = false
            setOnClickListener { continueToApplication() }
        }, matchWidth(topMargin = 12))

        return ScrollView(this).apply {
            isFillViewport = true
            addView(
                content,
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                )
            )
        }
    }

    private fun exportDiagnostics() {
        runCatching {
            startActivity(DiagnosticsShare.createChooserIntent(this))
        }.onFailure {
            Toast.makeText(
                this,
                R.string.recovery_diagnostics_export_failed_message,
                Toast.LENGTH_LONG
            ).show()
        }
    }

    private fun continueToApplication() {
        runCatching {
            CrashLogger.acknowledgeRecoveryReports(applicationContext, pendingReports)
        }.onFailure {
            Toast.makeText(this, R.string.recovery_acknowledge_failed_message, Toast.LENGTH_LONG).show()
        }
        openMainApplication()
    }

    private fun openMainApplication() {
        startActivity(
            createForwardIntent(this, intent, MAIN_ACTIVITY_CLASS_NAME).addFlags(
                Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
            )
        )
        finish()
    }

    private fun formatTimestamp(timestamp: Long): String {
        return java.text.DateFormat.getDateTimeInstance(
            java.text.DateFormat.MEDIUM,
            java.text.DateFormat.MEDIUM,
            activeResourceLocale()
        ).format(Date(timestamp))
    }

    private fun matchWidth(topMargin: Int = 0): LinearLayout.LayoutParams {
        return LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply {
            this.topMargin = dp(topMargin)
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    companion object {
        fun createRecoveryIntent(context: Context, source: Intent): Intent {
            return createForwardIntent(context, source, RecoveryActivity::class.java.name)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        }

        private fun createForwardIntent(
            context: Context,
            source: Intent,
            destinationClassName: String
        ): Intent {
            val incomingUri = source.data
            val forwarded = if (
                source.action == Intent.ACTION_VIEW &&
                incomingUri?.scheme == ContentResolver.SCHEME_CONTENT
            ) {
                Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(incomingUri, source.type)
                    clipData = source.clipData
                    addFlags(source.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
            } else {
                Intent(Intent.ACTION_MAIN)
            }
            return forwarded.setClassName(context.packageName, destinationClassName)
        }

        private const val MAIN_ACTIVITY_CLASS_NAME =
            "com.cytarch.cellularity.ui.main_menu.MainMenuActivity"
    }
}
