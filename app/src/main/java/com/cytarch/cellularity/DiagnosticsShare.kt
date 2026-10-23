package com.cytarch.cellularity

import android.content.ClipData
import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider

internal object DiagnosticsShare {
    fun createChooserIntent(context: Context): Intent {
        val exportFile = CrashLogger.createDiagnosticsExport(context.applicationContext)
        val exportUri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            exportFile
        )
        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_STREAM, exportUri)
            clipData = ClipData.newRawUri(exportFile.name, exportUri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        return Intent.createChooser(
            shareIntent,
            context.getString(R.string.diagnostics_export_chooser_title)
        )
    }
}
