/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionNotice
import com.cytarch.cellularity.shared.ui.main_menu.messageResource
import org.jetbrains.compose.resources.getString

/**
 * Something the launcher has to say, from whichever part of it asked.
 *
 * A file operation has words of its own in the shared resources, resolved from its reason; a
 * theme export has only the sentence the file operation produced, because there is no
 * failure taxonomy for "the dialog was cancelled" and inventing one would be worse than
 * saying it plainly. Both are shown by the same host so there is one place that decides how
 * a message appears and one queue that decides when.
 */
sealed interface DesktopNotice

/** The outcome of a theme import or export, already in words. */
data class DesktopThemeNotice(val message: String) : DesktopNotice

/** What a stored-file operation did, in the terms the collection screens use. */
data class DesktopFileNotice(val notice: FileCollectionNotice) : DesktopNotice

/**
 * Where the launcher says what it just did.
 *
 * Android shows these as a toast. A desktop window has no toast, so this is a snackbar at
 * the bottom of the same content -- the same words through the same resolver, on the surface
 * the platform does have.
 *
 * It exists because the alternative was already happening and was wrong: the launcher
 * received these reports and discarded them. Renaming a plate, deleting one, importing a
 * theme, or failing to do any of those would then complete with nothing said, which reads as
 * the action having been ignored. The message is not decoration; on the failure paths it is
 * the only thing that distinguishes "that name is taken" from "nothing happened".
 *
 * The cause is logged as well as shown, matching the phone, because the message is written
 * for a person and the storage code is what a report needs.
 */
@Composable
fun DesktopNoticeHost(
    notice: DesktopNotice?,
    onNoticeShown: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    val hostState = remember { SnackbarHostState() }

    LaunchedEffect(notice) {
        val pending = notice ?: return@LaunchedEffect
        val message = when (pending) {
            is DesktopThemeNotice -> pending.message

            is DesktopFileNotice -> {
                pending.notice.storageError?.let { error ->
                    System.err.println(
                        "Stored-file ${pending.notice.operation} failed: " +
                            "${error.code} ${error.detail}"
                    )
                }
                getString(pending.notice.feedback.messageResource())
            }
        }
        hostState.showSnackbar(message = message, duration = SnackbarDuration.Short)
        onNoticeShown()
    }

    Box(modifier = modifier) {
        content()
        SnackbarHost(
            hostState = hostState,
            modifier = Modifier.align(Alignment.BottomCenter),
            snackbar = { data -> Snackbar(snackbarData = data) }
        )
    }
}

/**
 * Remembers the last notice until something has shown it.
 *
 * Kept in one place because every page needs it and none owns another. The notice is cleared
 * by the host rather than by a timer, so a message is never replaced by a later one before it
 * has been read.
 */
class DesktopNoticeQueue {
    var pending by mutableStateOf<DesktopNotice?>(null)
        private set

    fun post(notice: DesktopNotice) {
        pending = notice
    }

    fun clear() {
        pending = null
    }
}
