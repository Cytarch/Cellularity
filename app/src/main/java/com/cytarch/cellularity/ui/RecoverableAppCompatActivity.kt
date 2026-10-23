package com.cytarch.cellularity.ui

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.cytarch.cellularity.CellularityApplication
import com.cytarch.cellularity.CrashLogger
import com.cytarch.cellularity.ui.recovery.RecoveryActivity
import kotlinx.coroutines.runBlocking

abstract class RecoverableAppCompatActivity : AppCompatActivity() {
    protected var redirectedToRecovery: Boolean = false
        private set

    override fun onCreate(savedInstanceState: Bundle?) {
        val pendingReports = CrashLogger.listPendingRecoveryReports(applicationContext)
        if (pendingReports.isNotEmpty()) {
            super.onCreate(savedInstanceState)
            redirectedToRecovery = true
            startActivity(recoveryIntent())
            finish()
            return
        }

        runBlocking {
            (application as CellularityApplication).initializeSettings()
        }
        CellularityApplication.applyStoredLanguage(applicationContext)
        super.onCreate(savedInstanceState)
    }

    private fun recoveryIntent(): Intent {
        return RecoveryActivity.createRecoveryIntent(this, intent)
    }
}
