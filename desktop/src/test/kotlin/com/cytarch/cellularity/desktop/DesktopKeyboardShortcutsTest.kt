/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import com.cytarch.cellularity.shared.ui.play.PlayTab
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTool
import kotlin.math.sqrt
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Keyboard acceptance contracts; every check returns pass/fail. */
class DesktopKeyboardShortcutsTest {
    /**
     * Input -> expected: Space/1 -> freeze/observe; 5/9 -> Cell/Diagnosis.
     * The complete mapping is asserted so an old key cannot silently retain its former action.
     */
    @Test
    fun `temperature and tool keys use the shifted desktop mapping`() {
        assertEquals(MicroscopeTemperature.FREEZE, desktopTemperatureShortcut(Key.Spacebar))
        assertEquals(MicroscopeTemperature.OBSERVE, desktopTemperatureShortcut(Key.One))
        assertEquals(MicroscopeTemperature.INCUBATE, desktopTemperatureShortcut(Key.Two))
        assertEquals(MicroscopeTemperature.SLOW, desktopTemperatureShortcut(Key.Three))
        assertEquals(
            MicroscopeTemperature.INCUBATE_NO_RENDER,
            desktopTemperatureShortcut(Key.Four)
        )
        assertNull(desktopTemperatureShortcut(Key.Five))

        assertEquals(MicroscopeTool.CELL, desktopToolShortcut(Key.Five))
        assertEquals(MicroscopeTool.MOVE, desktopToolShortcut(Key.Six))
        assertEquals(MicroscopeTool.ADD, desktopToolShortcut(Key.Seven))
        assertEquals(MicroscopeTool.REMOVE, desktopToolShortcut(Key.Eight))
        assertEquals(MicroscopeTool.DIAGNOSIS, desktopToolShortcut(Key.Nine))
        assertNull(desktopToolShortcut(Key.Zero))
    }

    /**
     * Input -> expected: Ctrl+Tab on Microscope -> Genome, then -> Substrate;
     * Ctrl+Shift+Tab on Microscope -> Substrate, then -> Genome. Direction wraps at both ends.
     */
    @Test
    fun `tab cycling moves in both directions and wraps`() {
        assertEquals(PlayTab.GENOME, cyclePlayTab(PlayTab.MICROSCOPE, forward = true))
        assertEquals(PlayTab.SUBSTRATE, cyclePlayTab(PlayTab.GENOME, forward = true))

        assertEquals(PlayTab.SUBSTRATE, cyclePlayTab(PlayTab.MICROSCOPE, forward = false))
        assertEquals(PlayTab.GENOME, cyclePlayTab(PlayTab.SUBSTRATE, forward = false))
    }

    /**
     * Input -> expected: holding W -> upward camera pan; adding A -> unit-length diagonal pan;
     * releasing W while A remains held -> left pan only; opposite keys -> no movement.
     */
    @Test
    fun `camera keys track holds releases and opposite directions`() {
        val w = updatedDesktopCameraKeys(emptySet(), Key.W, KeyEventType.KeyDown)!!
        assertEquals(Offset(0f, 1f), desktopCameraPanDirection(w))

        val wa = updatedDesktopCameraKeys(w, Key.A, KeyEventType.KeyDown)!!
        val diagonal = desktopCameraPanDirection(wa)
        assertEquals(1f, sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y), 0.0001f)

        val a = updatedDesktopCameraKeys(wa, Key.W, KeyEventType.KeyUp)!!
        assertEquals(Offset(1f, 0f), desktopCameraPanDirection(a))

        val ad = updatedDesktopCameraKeys(a, Key.D, KeyEventType.KeyDown)!!
        assertEquals(Offset.Zero, desktopCameraPanDirection(ad))
        assertNull(updatedDesktopCameraKeys(ad, Key.Enter, KeyEventType.KeyDown))
        assertTrue(updatedDesktopCameraKeys(ad, Key.A, KeyEventType.KeyUp)!!.size == 1)
    }
}
