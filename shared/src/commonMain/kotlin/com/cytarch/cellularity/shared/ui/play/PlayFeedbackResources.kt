package com.cytarch.cellularity.shared.ui.play

import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.file_operation_invalid_name_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_load_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_newer_version_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_save_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_saved_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_size_limit_exceeded_message
import com.cytarch.cellularity.shared.generated.resources.freezer_save_name_already_exists_message
import org.jetbrains.compose.resources.StringResource

/**
 * The words for what a play operation did.
 *
 * It lives beside the feedback it names rather than in each platform's interface code, because the
 * words are the shared screen's: a save that says one thing on a phone and another on a desktop is
 * the same failure described two ways, and the person reading either of them has no way to know
 * that. Android shows these as a toast and the desktop as a snackbar -- the surface differs, the
 * sentence does not.
 */
fun PlayFeedback.messageResource(): StringResource = when (this) {
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
