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
 * Where the launcher says what a file operation did.
 *
 * Android shows these as a toast. A desktop window has no toast, so this is a snackbar at
 * the bottom of the same content -- the same words through the same resolver, on the surface
 * the platform does have.
 *
 * It exists because the alternative was already happening and was wrong: the launcher
 * received these notices and discarded them. Renaming a plate, deleting one, or failing to
 * import one would then complete with nothing said, which reads as the action having been
 * ignored. The message is not decoration; on the failure paths it is the only thing that
 * distinguishes "that name is taken" from "nothing happened".
 *
 * The cause is logged as well as shown, matching the phone, because the message is written
 * for a person and the storage code is what a report needs.
 */
@Composable
fun DesktopNoticeHost(
    notice: FileCollectionNotice?,
    onNoticeShown: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    val hostState = remember { SnackbarHostState() }

    LaunchedEffect(notice) {
        val pending = notice ?: return@LaunchedEffect
        pending.storageError?.let { error ->
            System.err.println(
                "Stored-file ${pending.operation} failed: ${error.code} ${error.detail}"
            )
        }
        hostState.showSnackbar(
            message = getString(pending.feedback.messageResource()),
            duration = SnackbarDuration.Short
        )
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
 * Kept in one place because both collection pages need it and neither owns the other. The
 * notice is cleared by the host rather than by a timer, so a message is never replaced by a
 * later one before it has been read.
 */
class DesktopNoticeQueue {
    var pending by mutableStateOf<FileCollectionNotice?>(null)
        private set

    fun post(notice: FileCollectionNotice) {
        pending = notice
    }

    fun clear() {
        pending = null
    }
}
