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

/** Temperature modes: Space freezes, digits 1 through 4 select the running modes. */
internal fun desktopTemperatureShortcut(key: Key): MicroscopeTemperature? = when (key) {
    Key.Spacebar -> MicroscopeTemperature.FREEZE
    Key.One -> MicroscopeTemperature.OBSERVE
    Key.Two -> MicroscopeTemperature.INCUBATE
    Key.Three -> MicroscopeTemperature.SLOW
    Key.Four -> MicroscopeTemperature.INCUBATE_NO_RENDER
    else -> null
}

/** Tools follow the temperatures without leaving an unused digit between the groups. */
internal fun desktopToolShortcut(key: Key): MicroscopeTool? = when (key) {
    Key.Five -> MicroscopeTool.CELL
    Key.Six -> MicroscopeTool.MOVE
    Key.Seven -> MicroscopeTool.ADD
    Key.Eight -> MicroscopeTool.REMOVE
    Key.Nine -> MicroscopeTool.DIAGNOSIS
    else -> null
}

/** The next experiment tab in the bar's order, wrapping at either end. */
internal fun cyclePlayTab(current: PlayTab, forward: Boolean): PlayTab {
    val tabs = PlayTab.entries
    val step = if (forward) 1 else -1
    return tabs[(current.ordinal + step + tabs.size) % tabs.size]
}

private val cameraKeys = setOf(Key.W, Key.A, Key.S, Key.D)

/**
 * Applies one key event to the held-camera-key set.
 *
 * Null means the event is not camera input and must continue through the normal shortcut and text
 * handlers. A set is returned for both press and release, so a held key can always be released.
 */
internal fun updatedDesktopCameraKeys(
    held: Set<Key>,
    key: Key,
    type: KeyEventType
): Set<Key>? {
    if (key !in cameraKeys) return null
    return when (type) {
        KeyEventType.KeyDown -> held + key
        KeyEventType.KeyUp -> held - key
        else -> held
    }
}

/**
 * Screen deltas for the native camera's existing pan command.
 *
 * Native pan follows a drag: positive x moves the camera left and positive y moves it up. WASD is
 * translated accordingly. Diagonal movement is normalized so it is not faster than a cardinal one.
 */
internal fun desktopCameraPanDirection(held: Set<Key>): Offset {
    val x = (if (Key.A in held) 1f else 0f) - (if (Key.D in held) 1f else 0f)
    val y = (if (Key.W in held) 1f else 0f) - (if (Key.S in held) 1f else 0f)
    val length = sqrt(x * x + y * y)
    return if (length == 0f) Offset.Zero else Offset(x / length, y / length)
}
