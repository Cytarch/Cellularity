package com.cytarch.cellularity.shared.ui.components

import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type

/** Whether this event is a physical Enter press rather than a release or another key. */
internal fun isConfirmEnterKey(key: Key, type: KeyEventType): Boolean =
    type == KeyEventType.KeyDown && (key == Key.Enter || key == Key.NumPadEnter)

/**
 * Confirms from Enter while the event belongs to this field, using the same callback as OK.
 *
 * The modifier is attached only to single-line inputs (or a container made exclusively of them),
 * not to a whole dialog: Enter on Cancel or another focused control keeps its ordinary meaning.
 */
internal fun Modifier.confirmOnEnter(
    enabled: Boolean = true,
    onConfirm: () -> Unit
): Modifier = onPreviewKeyEvent { event ->
    if (enabled && isConfirmEnterKey(event.key, event.type)) {
        onConfirm()
        true
    } else {
        false
    }
}
