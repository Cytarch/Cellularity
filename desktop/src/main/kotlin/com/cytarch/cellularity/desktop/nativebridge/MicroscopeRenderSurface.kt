/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.awt.SwingPanel
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.material3.Text
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.dp
import java.awt.Canvas
import java.awt.Color as AwtColor
import java.awt.Component
import java.awt.Dimension
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.event.MouseWheelEvent
import kotlinx.coroutines.isActive

/**
 * The simulation's input, in the surface's own client pixels.
 *
 * A sealed hierarchy rather than raw callbacks because every one of these has to be
 * applied on the thread that owns the renderer, and the queued value is the natural
 * place to make that requirement visible.
 */
sealed interface SurfaceInputEvent {
    data class Pan(val dx: Float, val dy: Float) : SurfaceInputEvent
    data class Zoom(val factor: Float, val x: Float, val y: Float) : SurfaceInputEvent
    data class Tap(val x: Float, val y: Float) : SurfaceInputEvent
    data class Temperature(val mode: Int) : SurfaceInputEvent
    data class Tool(val ordinal: Int) : SurfaceInputEvent
    data object Reset : SurfaceInputEvent
}

/**
 * The live simulation, rendered by the native renderer inside the Compose window.
 *
 * The renderer presents into a Win32 child window, and the child window is parented to
 * the heavyweight AWT canvas that Compose puts here. That is the whole trick: Compose
 * draws its own scene with Skia into the same top-level window, and the child window
 * sits above it with the simulation in it, so the interface and the simulation are one
 * window rather than two.
 *
 * The canvas is deliberately heavyweight. A lightweight Swing component would be painted
 * by Skia and have no native window of its own, and then there would be nothing to parent
 * the renderer to.
 */
@Composable
fun MicroscopeRenderSurface(
    assetRoots: DesktopAssetRoots.Roots?,
    maxCellCount: Int,
    maxFoodCount: Int,
    dishRadius: Double,
    inputEvents: List<SurfaceInputEvent>,
    onCellCount: (Long) -> Unit,
    onFailure: (String) -> Unit,
    modifier: Modifier = Modifier
) {    if (!DesktopNativeRenderer.isAvailable || assetRoots == null) {
        RendererUnavailable(
            reason = if (!DesktopNativeRenderer.isAvailable) {
                DesktopNativeRenderer.failureDescription
                    ?: "The desktop renderer library could not be loaded."
            } else {
                "The compiled shaders and textures were not found. Build the native " +
                    "targets first, or set CELLULARITY_HOST_SHADER_ROOT and " +
                    "CELLULARITY_HOST_TEXTURE_ROOT."
            },
            modifier = modifier
        )
        return
    }

    // The native window and the session outlive recomposition, so they are held in
    // plain fields rather than in state: changing them must not itself trigger a
    // recomposition, and a recomposition must not recreate them.
    val holder = remember {
        SurfaceHolder(
            maxCellCount = maxCellCount,
            maxFoodCount = maxFoodCount,
            dishRadius = dishRadius,
            shaderRoot = assetRoots.shaderRoot.toString(),
            textureRoot = assetRoots.textureRoot.toString(),
            onCellCount = onCellCount,
            onFailure = onFailure
        )
    }

    DisposableEffect(holder) {
        onDispose { holder.release() }
    }

    Box(modifier = modifier) {
        SwingPanel(
            modifier = Modifier.fillMaxSize(),
            factory = { holder.attachCanvas() },
            update = { }
        )
    }

    // The queue is handed to the frame loop rather than drained by its own effect.
    // A LaunchedEffect keyed on the input list would restart every time an event arrived
    // and replay the whole list, so every pan would be applied once per event that
    // followed it. The loop already runs once per frame; this makes it the single place
    // that touches the renderer, which is also what the renderer requires.
    DisposableEffect(holder, inputEvents) {
        holder.hold(inputEvents)
        onDispose { holder.clearHold(inputEvents) }
    }

    // The frame loop. Drawing is what presents, and the presenter blocks on the display
    // refresh, so this loop paces itself rather than needing a timer.
    LaunchedEffect(holder) {
        while (isActive) {
            if (!holder.renderOnce()) {
                break
            }
        }
    }
}

@Composable
private fun RendererUnavailable(reason: String, modifier: Modifier = Modifier) {
    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp),
            modifier = Modifier.padding(24.dp)
        ) {
            Text(text = "The simulation renderer is unavailable", textAlign = TextAlign.Center)
            Text(text = reason, textAlign = TextAlign.Center)
        }
    }
}

/**
 * Owns the native window and session for one place in the interface.
 *
 * This is not state and is not remembered by key: it is created once and released once,
 * because both resources are expensive and neither tolerates being recreated while an
 * older copy is still presenting.
 */
private class SurfaceHolder(
    private val maxCellCount: Int,
    private val maxFoodCount: Int,
    private val dishRadius: Double,
    private val shaderRoot: String,
    private val textureRoot: String,
    private val onCellCount: (Long) -> Unit,
    private val onFailure: (String) -> Unit
) {
    private var canvas: Canvas? = null
    private var windowHandle: Long = 0L
    private var sessionHandle: Long = 0L
    private var width: Int = 0
    private var height: Int = 0
    private var reported = false
    private var released = false

    /** Input published by the composition, consumed by the frame loop. */
    private val pendingInput =
        java.util.concurrent.atomic.AtomicReference<List<SurfaceInputEvent>>(emptyList())

    /**
     * Creates the heavyweight canvas and, once AWT has realised it, the native window
     * inside it.
     *
     * The native window cannot be created here: a component that is not yet displayable
     * has no native peer, and therefore no handle to parent to. The listener waits for
     * that to become true rather than assuming it.
     */
    fun attachCanvas(): Component {
        val surface = Canvas().apply {
            background = AwtColor(0x1E, 0x1E, 0x1E)
            // A preferred size keeps the first layout from collapsing the canvas to
            // nothing, which would open the native window at zero extent.
            preferredSize = Dimension(640, 480)
            minimumSize = Dimension(1, 1)
        }
        surface.addComponentListener(object : ComponentAdapter() {
            override fun componentResized(event: ComponentEvent) {
                onSizeChanged(event.component)
            }

            override fun componentShown(event: ComponentEvent) {
                onSizeChanged(event.component)
            }
        })
        surface.addMouseListener(surfaceMouseHandler(surface))
        surface.addMouseMotionListener(surfaceMouseHandler(surface))
        surface.addMouseWheelListener(surfaceMouseHandler(surface))
        canvas = surface
        return surface
    }

    /**
     * Translates pointer events into queued simulation input.
     *
     * One handler serves all three listener roles so the drag state is shared: a press
     * has to be remembered across a motion event and then judged on release, and two
     * separate handlers could not see each other's state.
     */
    private fun surfaceMouseHandler(surface: Canvas): MouseAdapter =
        object : MouseAdapter() {
            private var pressedX = 0
            private var pressedY = 0
            private var lastX = 0
            private var lastY = 0
            private var dragging = false
            private var moved = false

            override fun mousePressed(event: MouseEvent) {
                dragging = true
                moved = false
                pressedX = event.x
                pressedY = event.y
                lastX = event.x
                lastY = event.y
                // The canvas has to ask for focus explicitly, otherwise the keyboard
                // shortcuts (temperature, tool, reset) go to whichever component had it.
                surface.requestFocusInWindow()
            }

            override fun mouseReleased(event: MouseEvent) {
                if (!dragging) return
                dragging = false
                // A press that never moved is a click, which is the gesture that selects
                // a cell or places one. A press that moved was a pan, which has already
                // been applied frame by frame, and must not also count as a click or
                // every pan would end by selecting whatever it stopped over.
                if (!moved) {
                    DesktopNativeRenderer.nativeQueueTap(
                        sessionHandle,
                        event.x.toFloat(),
                        event.y.toFloat()
                    )
                }
            }

            override fun mouseDragged(event: MouseEvent) {
                if (!dragging) return
                val dx = event.x - lastX
                val dy = event.y - lastY
                lastX = event.x
                lastY = event.y
                if (dx == 0 && dy == 0) return
                moved = true
                DesktopNativeRenderer.nativeQueuePan(sessionHandle, dx.toFloat(), dy.toFloat())
            }

            override fun mouseWheelMoved(event: MouseWheelEvent) {
                // Multiplicative, matching the wheel on the phone and the native host:
                // zooming in then out returns exactly where it started.
                val factor = if (event.wheelRotation < 0) 1.1f else 1f / 1.1f
                DesktopNativeRenderer.nativeQueueZoom(
                    sessionHandle,
                    factor,
                    event.x.toFloat(),
                    event.y.toFloat()
                )
            }
        }

    private fun onSizeChanged(component: Component) {
        if (released) return
        val newWidth = component.width
        val newHeight = component.height
        if (newWidth <= 0 || newHeight <= 0) return
        if (newWidth == width && newHeight == height && windowHandle != 0L) return

        val ownerHandle = nativeWindowHandleOf(component)
        if (ownerHandle == 0L) {
            // Not an error yet: AWT realises the peer asynchronously, and the listener
            // is called again once it has.
            return
        }

        if (windowHandle == 0L) {
            windowHandle = DesktopNativeRenderer.nativeCreateWindow(
                ownerHandle,
                0,
                0,
                newWidth,
                newHeight
            )
            if (windowHandle == 0L) {
                failOnce("The renderer could not create its window.")
                return
            }
        } else {
            DesktopNativeRenderer.nativeResizeWindow(windowHandle, newWidth, newHeight)
        }
        width = newWidth
        height = newHeight

        if (sessionHandle == 0L) {
            sessionHandle = DesktopNativeRenderer.nativeCreateSession(
                windowHandle,
                newWidth,
                newHeight,
                maxCellCount,
                maxFoodCount,
                dishRadius,
                shaderRoot,
                textureRoot
            )
            if (sessionHandle == 0L) {
                failOnce("The simulation could not be started.")
            }
        }
    }

    /** Draws one frame and applies whatever input has arrived since the last one. */
    fun renderOnce(): Boolean {
        if (released) return false
        val pending = pendingInput.getAndSet(emptyList())
        for (event in pending) {
            apply(event)
        }
        if (sessionHandle == 0L) {
            // Still waiting for the canvas to be realised and the session to start.
            Thread.sleep(8)
            return true
        }
        val result = DesktopNativeRenderer.nativeRenderFrame(sessionHandle)
        if (result == 2) return false
        if (result == 1) {
            failOnce("The renderer reported a failed frame.")
            return false
        }
        onCellCount(DesktopNativeRenderer.nativeCellCount(sessionHandle))
        return true
    }

    /**
     * Publishes the queue this composition is using.
     *
     * The queue is rebuilt on every input event, so the reference is swapped rather than
     * the list mutated: the frame loop always sees either the old list or the new one,
     * never a list being written to while it is read.
     */
    fun hold(events: List<SurfaceInputEvent>) {
        pendingInput.set(events)
    }

    /** Drops the queue, but only if it is still the one being cleared. */
    fun clearHold(events: List<SurfaceInputEvent>) {
        pendingInput.compareAndSet(events, emptyList())
    }

    fun apply(event: SurfaceInputEvent) {
        if (sessionHandle == 0L) return
        when (event) {
            is SurfaceInputEvent.Pan ->
                DesktopNativeRenderer.nativeQueuePan(sessionHandle, event.dx, event.dy)

            is SurfaceInputEvent.Zoom ->
                DesktopNativeRenderer.nativeQueueZoom(sessionHandle, event.factor, event.x, event.y)

            is SurfaceInputEvent.Tap ->
                DesktopNativeRenderer.nativeQueueTap(sessionHandle, event.x, event.y)

            is SurfaceInputEvent.Temperature ->
                DesktopNativeRenderer.nativeSetTemperatureMode(sessionHandle, event.mode)

            is SurfaceInputEvent.Tool ->
                DesktopNativeRenderer.nativeSetToolOrdinal(sessionHandle, event.ordinal)

            SurfaceInputEvent.Reset ->
                DesktopNativeRenderer.nativeResetWorld(sessionHandle)
        }
    }

    fun release() {
        if (released) return
        released = true
        if (sessionHandle != 0L) {
            DesktopNativeRenderer.nativeStopSession(sessionHandle)
            sessionHandle = 0L
        }
        if (windowHandle != 0L) {
            DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
            windowHandle = 0L
        }
        canvas = null
    }

    private fun failOnce(message: String) {
        if (reported) return
        reported = true
        onFailure(message)
    }

    private companion object {
        /**
         * The AWT peer's native window handle, which is the parent the renderer needs.
         *
         * There is no supported way to get this. `Component.getPeer` was removed in JDK
         * 21, so the package-private `peer` field is the only route left, and reading it
         * needs `--add-opens java.desktop/java.awt=ALL-UNNAMED` on the command line --
         * see the note in the launcher. `getHWnd` is the accessor AWT itself uses on
         * Windows, and the peer's type lives in a package the module system does not
         * export, so the value can be held as `Any` but its type cannot be named.
         *
         * When the open is missing this returns zero, and the interface reports the
         * renderer as unavailable rather than crashing, which is the right failure for a
         * packaging mistake.
         */
        fun nativeWindowHandleOf(component: Component): Long {
            if (!component.isShowing) return 0L
            return try {
                val peerField = Component::class.java.getDeclaredField("peer")
                peerField.isAccessible = true
                val peer: Any = peerField.get(component) ?: return 0L
                val getHWnd = peer.javaClass.methods.firstOrNull { it.name == "getHWnd" }
                    ?: return 0L
                (getHWnd.invoke(peer) as? Long) ?: 0L
            } catch (_: ReflectiveOperationException) {
                0L
            } catch (_: RuntimeException) {
                0L
            } catch (_: LinkageError) {
                // A missing --add-opens surfaces as an error rather than an exception on
                // some JDKs. Either way the caller gets zero and says so.
                0L
            }
        }
    }
}
