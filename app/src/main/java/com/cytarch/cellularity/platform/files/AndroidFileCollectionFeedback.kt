package com.cytarch.cellularity.platform.files

import android.content.Context
import android.widget.Toast
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionNotice
import com.cytarch.cellularity.shared.ui.main_menu.messageResource
import org.jetbrains.compose.resources.getString

/** Resolves shared product messages and renders them with an Android-native transient surface. */
internal suspend fun showFileCollectionNotice(
    context: Context,
    notice: FileCollectionNotice
) {
    notice.storageError?.let { error ->
        AppLog.error(
            "Stored-file ${notice.operation} failed: ${error.code} ${error.detail}"
        )
    }
    Toast.makeText(
        context,
        getString(notice.feedback.messageResource()),
        Toast.LENGTH_LONG
    ).show()
}
