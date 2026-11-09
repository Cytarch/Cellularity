/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import androidx.compose.ui.graphics.toComposeImageBitmap
import java.nio.ByteBuffer
import java.util.concurrent.TimeUnit

/**
 * Measures what one frame of the world costs, and what it leaves behind.
 *
 * Two ways of getting the same picture to the screen are compared by the only numbers that
 * matter to a person watching it: how long a frame takes, and how much memory the process is
 * holding after a few hundred of them.
 *
 * The first way hands the renderer a direct byte buffer, copies those pixels into an array and
 * builds a fresh image out of it. The second hands the renderer the address of the very picture
 * the interface will draw, so the frame lands where it is going to be shown. The difference
 * should be one window-sized copy per frame and one image allocation per frame; the report says
 * whether it is.
 *
 * Run through `:desktop:runFrameCostProbe` with `buffer` or `address`, and watch the process's
 * working set from outside while it runs. The probe does not measure memory itself: a JVM's own
 * idea of what it holds leaves out everything the graphics library keeps outside the heap, and
 * that is precisely where this leak lives.
 */
fun main(arguments: Array<String>) {
    // Gradle passes --args through as one string, so a comma is what separates values in
    // practice; splitting on both keeps "address 640 400" and "address,640,400" the same thing.
    val values = arguments.flatMap { it.split(',').map(String::trim) }.filter { it.isNotEmpty() }
    val mode = values.firstOrNull() ?: "address"
    if (mode != "buffer" && mode != "address") {
        System.err.println("FRAMECOST mode must be 'buffer' or 'address', not '$mode'")
        kotlin.system.exitProcess(2)
    }
    if (!DesktopNativeRenderer.isAvailable) {
        System.err.println("FRAMECOST the renderer library was not built, so there is nothing to measure")
        kotlin.system.exitProcess(3)
    }
    val roots = DesktopAssetRoots.resolve()
    if (roots == null) {
        System.err.println("FRAMECOST the compiled shaders and textures were not found")
        kotlin.system.exitProcess(3)
    }

    // The size is an argument because the cost per frame has two very different explanations and
    // they are told apart by how it scales: a cost that grows with the number of pixels is
    // bandwidth (the readback), and a cost that stays put is latency (the wait for each frame to
    // finish before the next one may start).
    val width = values.getOrNull(1)?.toIntOrNull() ?: 1280
    val height = values.getOrNull(2)?.toIntOrNull() ?: 800
    val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(width, height)
    if (anchor == 0L) {
        System.err.println("FRAMECOST the renderer could not create the surface it draws through")
        kotlin.system.exitProcess(4)
    }
    var session = 0L
    try {
        session = DesktopNativeRenderer.nativeCreateSession(
            windowHandle = anchor,
            width = width,
            height = height,
            maxCellCount = 1_000,
            maxFoodCount = 3_500,
            dishRadius = 1.0,
            shaderRoot = roots.shaderRoot.toString(),
            textureRoot = roots.textureRoot.toString()
        )
        if (session == 0L) {
            System.err.println("FRAMECOST the simulation did not start")
            kotlin.system.exitProcess(4)
        }
        // A plate that is not moving has no new frames to give, so the run is measured on a
        // world that is doing what a person would be watching.
        DesktopNativeRenderer.nativeSetTemperatureMode(session, OBSERVE_MODE)
        val pixels = ByteBuffer.allocateDirect(width * height * BYTES_PER_PIXEL)
        val copiedPixels = ByteArray(width * height * BYTES_PER_PIXEL)

        repeat(WARM_UP_FRAMES) {
            when (mode) {
                "buffer" -> DesktopNativeRenderer.nativeRenderFrameInto(
                    session, pixels, width, height, width * BYTES_PER_PIXEL
                )
                else -> DesktopNativeRenderer.nativeRenderFrameIntoAddress(
                    session, surfaceAddress(width, height), width, height,
                    width * BYTES_PER_PIXEL
                )
            }
        }

        val timings = ArrayList<Long>(MEASURED_FRAMES)
        var lastImage: androidx.compose.ui.graphics.ImageBitmap? = null
        // Frames the renderer had nothing new for. A run whose time per frame is dominated by
        // these is limited by the world's own pace rather than by the cost of drawing it.
        var idleAnswers = 0
        var idleNanos = 0L
        var drawn = 0
        val startedAt = System.nanoTime()
        while (drawn < MEASURED_FRAMES) {
            val frameStartedAt = System.nanoTime()
            val status = when (mode) {
                "buffer" -> {
                    val status = DesktopNativeRenderer.nativeRenderFrameInto(
                        session, pixels, width, height, width * BYTES_PER_PIXEL
                    )
                    if (status == FRAME_DRAWN) {
                        // What the old path did with the pixels, step for step: copy them out of
                        // the buffer and build an image out of the copy. That is a whole window
                        // allocated per frame, and the native bitmap behind it is released by a
                        // finaliser rather than by anything that can be pointed at -- which is
                        // why the process grows while the plate is on screen and settles when it
                        // is not.
                        pixels.position(0)
                        pixels.get(copiedPixels)
                        val image = org.jetbrains.skia.Image.makeRaster(
                            org.jetbrains.skia.ImageInfo(
                                width,
                                height,
                                org.jetbrains.skia.ColorType.RGBA_8888,
                                org.jetbrains.skia.ColorAlphaType.PREMUL,
                                org.jetbrains.skia.ColorSpace.sRGB
                            ),
                            copiedPixels,
                            width * BYTES_PER_PIXEL
                        )
                        lastImage = image.toComposeImageBitmap()
                    }
                    status
                }

                else -> DesktopNativeRenderer.nativeRenderFrameIntoAddress(
                    session, surfaceAddress(width, height), width, height,
                    width * BYTES_PER_PIXEL
                )
            }
            if (status == FRAME_DRAWN) {
                timings.add(System.nanoTime() - frameStartedAt)
                drawn++
                if (drawn % 300 == 0) {
                    println("FRAMECOST progress mode=$mode frames=$drawn")
                }
            } else {
                idleAnswers++
                idleNanos += System.nanoTime() - frameStartedAt
                Thread.sleep(IDLE_SLEEP_MILLIS)
            }
        }
        check(lastImage != null || mode == "address") { "the buffered path produced no picture" }
        val elapsedNanos = System.nanoTime() - startedAt
        timings.sort()
        val medianMillis = timings[timings.size / 2] / 1_000_000.0
        val p95Millis = timings[(timings.size * 95) / 100] / 1_000_000.0
        val meanMillis = timings.average() / 1_000_000.0
        val framesPerSecond = drawn * 1_000_000_000.0 / elapsedNanos
        println(
            "FRAMECOST mode=$mode frames=$drawn " +
                "fps=${"%.1f".format(framesPerSecond)} " +
                "median=${"%.2f".format(medianMillis)}ms " +
                "p95=${"%.2f".format(p95Millis)}ms " +
                "mean=${"%.2f".format(meanMillis)}ms"
        )
        println("FRAMECOST cells=${DesktopNativeRenderer.nativeCellCount(session)}")
        val idleMeanMillis = idleNanos / 1_000_000.0 / maxOf(idleAnswers, 1)
        println(
            "FRAMECOST idleAnswers=$idleAnswers " +
                "idleMean=${"%.2f".format(idleMeanMillis)}ms"
        )
        System.out.flush()
    } finally {
        if (session != 0L) {
            DesktopNativeRenderer.nativeStopSession(session)
        }
        DesktopNativeRenderer.nativeDestroyWindow(anchor)
    }
}

/**
 * The address the renderer writes a frame to.
 *
 * In the probe this is one bitmap held for the whole run, which is what the application holds
 * too: the point of the measurement is a steady state, not a fresh allocation per frame.
 */
private var surfaceRef: org.jetbrains.skia.Bitmap? = null
private var surfacePixmapRef: org.jetbrains.skia.Pixmap? = null
private var surfaceWidth = 0
private var surfaceHeight = 0

private fun surfaceAddress(width: Int, height: Int): Long {
    val existing = surfaceRef
    if (existing != null && surfaceWidth == width && surfaceHeight == height) {
        return surfacePixmapRef?.addr ?: 0L
    }
    surfacePixmapRef?.close()
    existing?.close()
    val bitmap = org.jetbrains.skia.Bitmap()
    if (!bitmap.allocPixels(
            org.jetbrains.skia.ImageInfo(
                width,
                height,
                org.jetbrains.skia.ColorType.RGBA_8888,
                org.jetbrains.skia.ColorAlphaType.PREMUL,
                org.jetbrains.skia.ColorSpace.sRGB
            ),
            width * BYTES_PER_PIXEL
        )
    ) {
        return 0L
    }
    val pixmap = bitmap.peekPixels() ?: return 0L
    surfaceRef = bitmap
    surfacePixmapRef = pixmap
    surfaceWidth = width
    surfaceHeight = height
    return pixmap.addr
}

private const val BYTES_PER_PIXEL = 4
private const val OBSERVE_MODE = 1
private const val FRAME_DRAWN = 0
private const val WARM_UP_FRAMES = 40
private const val MEASURED_FRAMES = 1200
private const val IDLE_SLEEP_MILLIS = 4L

/** Kept so the compiler does not drop the copy the old path is measured with. */
@Suppress("unused")
private val keepAliveUntilEnd = TimeUnit.SECONDS

