package com.cytarch.cellularity.ui.play_menu

import android.content.Context
import android.widget.Toast
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.file_operation_invalid_name_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_load_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_newer_version_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_save_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_saved_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_size_limit_exceeded_message
import com.cytarch.cellularity.shared.generated.resources.freezer_save_name_already_exists_message
import com.cytarch.cellularity.shared.ui.play.PlayFailureCause
import com.cytarch.cellularity.shared.ui.play.PlayFeedback
import com.cytarch.cellularity.shared.ui.play.PlayNotice
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.getString

internal suspend fun showPlayNotice(context: Context, notice: PlayNotice) {
    when (val cause = notice.cause) {
        is PlayFailureCause.Storage -> AppLog.error(
            "Play ${notice.operation} failed: ${cause.error.code} ${cause.error.detail}"
        )
        is PlayFailureCause.Simulation -> AppLog.error(
            "Play ${notice.operation} failed: ${cause.error.code} ${cause.error.message}"
        )
        null -> Unit
    }
    showPlayFeedback(context, notice.feedback)
}

internal suspend fun showPlayFeedback(context: Context, feedback: PlayFeedback) {
    Toast.makeText(context, getString(feedback.messageResource()), Toast.LENGTH_LONG).show()
}

private fun PlayFeedback.messageResource(): StringResource = when (this) {
    PlayFeedback.BAD_NAME -> Res.string.file_operation_invalid_name_message
    PlayFeedback.NAME_EXISTS -> Res.string.freezer_save_name_already_exists_message
    PlayFeedback.FILE_TOO_LARGE -> Res.string.file_operation_size_limit_exceeded_message
    PlayFeedback.NEWER_VERSION -> Res.string.file_operation_newer_version_message
    PlayFeedback.LOAD_FAILED,
    PlayFeedback.RELOAD_FAILED,
    PlayFeedback.RESET_FAILED -> Res.string.file_operation_load_failed_message
    PlayFeedback.SAVE_FAILED -> Res.string.file_operation_save_failed_message
    PlayFeedback.SAVED -> Res.string.file_operation_saved_message
}
