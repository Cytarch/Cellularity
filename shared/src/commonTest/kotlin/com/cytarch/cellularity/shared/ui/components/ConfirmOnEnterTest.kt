package com.cytarch.cellularity.shared.ui.components

import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * Acceptance contract for confirming a single-line input with a physical Enter key.
 *
 * Input -> expected examples: Enter/NumPadEnter KeyDown -> confirm; Enter KeyUp or A KeyDown ->
 * leave the event for the text field. Metric: pass/fail for the key classifier.
 */
class ConfirmOnEnterTest {
    @Test
    fun `only enter key down events request confirmation`() {
        assertTrue(isConfirmEnterKey(Key.Enter, KeyEventType.KeyDown))
        assertTrue(isConfirmEnterKey(Key.NumPadEnter, KeyEventType.KeyDown))

        assertFalse(isConfirmEnterKey(Key.Enter, KeyEventType.KeyUp))
        assertFalse(isConfirmEnterKey(Key.A, KeyEventType.KeyDown))
    }
}
