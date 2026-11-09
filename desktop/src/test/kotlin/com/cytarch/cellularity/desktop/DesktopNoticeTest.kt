/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionFeedback
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionNotice
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionOperation
import com.cytarch.cellularity.shared.ui.main_menu.messageResource
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlinx.coroutines.runBlocking
import org.jetbrains.compose.resources.getString

/**
 * That a notice says something, and that the launcher holds it until it has been shown.
 *
 * These are two separate failures the launcher used to have. It dropped notices entirely, so
 * a refused rename completed in silence; and a queue that overwrote its value would replace
 * one message with the next before either was read, which looks the same from outside.
 *
 * The message text is resolved rather than compared to a literal. What matters is not the
 * wording but that every reason a file operation can fail has words at all -- the mapping is
 * exhaustive in the shared module, and a reason added there without a string would otherwise
 * only be discovered by a person hitting that path and seeing nothing.
 */
class DesktopNoticeTest {

    @Test
    fun `a notice is held until it has been shown`() {
        val queue = DesktopNoticeQueue()
        assertNull(queue.pending, "nothing has happened yet, so there is nothing to say")

        val notice = DesktopFileNotice(
            FileCollectionNotice(
                operation = FileCollectionOperation.RENAME,
                feedback = FileCollectionFeedback.BAD_NAME
            )
        )
        queue.post(notice)

        assertEquals(notice, queue.pending, "a posted notice must wait to be shown")

        queue.clear()

        assertNull(queue.pending, "a shown notice must not be shown again")
    }

    @Test
    fun `a notice from the settings screen shares the one queue`() {
        // Both kinds travel through the same place, because both are shown by the same host
        // and a second queue would be a second place for a message to be lost. The type is
        // what makes that possible without the host having to know which page asked.
        val queue = DesktopNoticeQueue()

        queue.post(DesktopThemeNotice("Theme imported from polar.cltheme"))

        assertEquals(
            DesktopThemeNotice("Theme imported from polar.cltheme"),
            queue.pending
        )
    }

    @Test
    fun `every reason a file operation can fail has a message`() = runBlocking {
        val missing = mutableListOf<String>()
        FileCollectionFeedback.entries.forEach { feedback ->
            val text = try {
                getString(feedback.messageResource())
            } catch (thrown: Throwable) {
                missing += "$feedback: ${thrown.message}"
                return@forEach
            }
            if (text.isBlank()) missing += "$feedback: the message is empty"
        }

        assertEquals(
            emptyList(),
            missing,
            "a failure a person can hit must have something to say: $missing"
        )
    }
}
