/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.Paths
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Captures the renderer's own window and reads the picture back, so a defect in it is found
 * here rather than reported later by a person.
 *
 * Two things were missing before this. The renderer's output could not be looked at without
 * looking at the machine, because the only way to photograph a window was to photograph the
 * desktop -- and on a shared machine that means taking the screen away from whoever is using
 * it. And nothing about the picture was ever checked, so every statement about it came either
 * from a run that had to wait for a person or from an assumption.
 *
 * A window can be captured by its own handle instead. That reads exactly one window and
 * nothing else: no part of anybody's screen is photographed, no window is brought forward, no
 * input is taken, and the check runs as often as it is useful to run it. The capture is
 * written to a file so the same run leaves behind something a person can look at afterwards,
 * and the numbers printed below are what makes the run itself able to fail.
 *
 * The numbers are chosen to catch the failures that are actually likely, not to describe the
 * picture. A blank capture is a renderer that drew nothing. A picture whose content occupies
 * a small corner of the frame is the reported defect, and its bounding box says so without
 * anyone having to interpret it. Huge flat areas are a stalled or untextured frame.
 *
 * Run through `:desktop:runCaptureProbe`. Exits non-zero when the capture does not contain a
 * picture that fills its window.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val window = AtomicReference<ComposeWindow?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val frame = ComposeWindow()
            frame.title = "capture probe"
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1100, 700)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = DesktopCellularityFileRepository(
                            Files.createTempDirectory("cellularity-capture-probe")
                                .resolve("substrates")
                        ),
                        onExit = { }
                    )
                }
            }
            frame.contentPane.add(panel)
            frame.pack()
            frame.setLocation(-4900, -4900)
            window.set(frame)
            frame.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(60, TimeUnit.SECONDS)) {
        println("CAPTURE the window was never built")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        println("CAPTURE could not open the play screen: $thrown")
        kotlin.system.exitProcess(3)
    }

    // Long enough for the session to be created and for several frames to have been drawn;
    // a capture taken before the first present is a picture of nothing.
    settle(8_000)

    val frame = window.get()!!
    val description = surfaceDescription()
    println("CAPTURE surface: $description")
    val handle = Regex("window=(\\d+)").find(description)?.groupValues?.get(1)
        ?.toLongOrNull()
        ?.takeIf { it != 0L }
    if (handle == null) {
        println("CAPTURE the surface did not report a window handle")
        kotlin.system.exitProcess(4)
    }

    val destination = Paths.get("build", "renderer-capture.bmp").toAbsolutePath()
    Files.createDirectories(destination.parent)
    Files.deleteIfExists(destination)
    val written = captureWindow(handle, destination.toString())
    if (written <= 0) {
        println("CAPTURE the window could not be captured (result=$written)")
        kotlin.system.exitProcess(5)
    }
    println("CAPTURE wrote $written bytes to $destination")

    val picture = try {
        readBmp(destination)
    } catch (thrown: Throwable) {
        println("CAPTURE the capture could not be read back: $thrown")
        kotlin.system.exitProcess(6)
    }
    report(picture)

    // The picture has to be more than a flat fill and has to reach most of its window. Both
    // are properties of a rendered world rather than of any particular world, so they hold
    // whatever the simulation happens to be doing.
    val verdict = judge(picture)
    println("CAPTURE verdict: $verdict")
    if (verdict != null) {
        javax.swing.SwingUtilities.invokeLater {
            frame.isVisible = false
            frame.dispose()
        }
        settle(500)
        kotlin.system.exitProcess(7)
    }
    println("CAPTURE the renderer's window contains a picture that fills it")

    javax.swing.SwingUtilities.invokeLater {
        frame.isVisible = false
        frame.dispose()
    }
    settle(500)
    kotlin.system.exitProcess(0)
}

/** A decoded 32-bit top-down BMP. */
private class Picture(
    val width: Int,
    val height: Int,
    private val pixels: IntArray
) {
    fun at(x: Int, y: Int): Int = pixels[y * width + x]
}

private fun readBmp(path: Path): Picture {
    val bytes = Files.readAllBytes(path)
    if (bytes.size < 54 || bytes[0] != 'B'.code.toByte() || bytes[1] != 'M'.code.toByte()) {
        throw IllegalArgumentException("not a BMP file")
    }
    val pixelOffset = readInt(bytes, 10)
    val width = readInt(bytes, 18)
    val height = readInt(bytes, 22)
    val bits = readShort(bytes, 28)
    if (bits != 32) {
        throw IllegalArgumentException("expected a 32-bit capture, found $bits")
    }
    val pixels = IntArray(width * height)
    var index = pixelOffset
    for (position in 0 until width * height) {
        if (index + 3 >= bytes.size) throw IllegalArgumentException("capture is truncated")
        val blue = bytes[index].toInt() and 0xFF
        val green = bytes[index + 1].toInt() and 0xFF
        val red = bytes[index + 2].toInt() and 0xFF
        pixels[position] = (red shl 16) or (green shl 8) or blue
        index += 4
    }
    return Picture(width, height, pixels)
}

private fun readInt(bytes: ByteArray, offset: Int): Int =
    (bytes[offset].toInt() and 0xFF) or
        ((bytes[offset + 1].toInt() and 0xFF) shl 8) or
        ((bytes[offset + 2].toInt() and 0xFF) shl 16) or
        ((bytes[offset + 3].toInt() and 0xFF) shl 24)

private fun readShort(bytes: ByteArray, offset: Int): Int =
    (bytes[offset].toInt() and 0xFF) or ((bytes[offset + 1].toInt() and 0xFF) shl 8)

/**
 * Prints what the capture contains.
 *
 * The spread of colours says whether anything was drawn at all; the bounding box of
 * everything that differs from the most common colour says where the drawn part is, which is
 * the number that turns "it looks wrong" into "the content occupies this rectangle"; and the
 * largest flat area says whether a region was left unrendered.
 */
private fun report(picture: Picture) {
    val counts = HashMap<Int, Int>()
    for (position in 0 until picture.width * picture.height) {
        val x = position % picture.width
        val y = position / picture.width
        val colour = picture.at(x, y)
        counts[colour] = (counts[colour] ?: 0) + 1
    }
    val background = counts.maxByOrNull { it.value }!!.key
    val distinct = counts.size
    var minX = picture.width
    var minY = picture.height
    var maxX = -1
    var maxY = -1
    var luminanceSum = 0L
    for (y in 0 until picture.height) {
        for (x in 0 until picture.width) {
            val colour = picture.at(x, y)
            val red = (colour shr 16) and 0xFF
            val green = (colour shr 8) and 0xFF
            val blue = colour and 0xFF
            luminanceSum += (red * 299 + green * 587 + blue * 114) / 1000
            if (colour != background) {
                if (x < minX) minX = x
                if (y < minY) minY = y
                if (x > maxX) maxX = x
                if (y > maxY) maxY = y
            }
        }
    }
    val backgroundShare = 100.0 * counts[background]!! / (picture.width * picture.height)
    println("CAPTURE picture ${picture.width}x${picture.height}")
    println("CAPTURE distinctColours=$distinct meanLuminance=${luminanceSum / (picture.width * picture.height)}")
    println(
        "CAPTURE background=#%06X covers=%.1f%%".format(background, backgroundShare)
    )
    if (maxX < 0) {
        println("CAPTURE the capture is a single flat colour")
    } else {
        println(
            "CAPTURE content spans x=$minX..$maxX y=$minY..$maxY " +
                "(${(maxX - minX + 1)}x${(maxY - minY + 1)})"
        )
    }
}

/**
 * Decides whether the capture is a picture of a running world, returning null when it is.
 *
 * Each check is a failure that has actually been reported or is likely to be, and each is
 * phrased as the thing a person would say rather than as a threshold. A flat capture is a
 * renderer that drew nothing. A handful of colours is a frame that never got its content.
 * Content confined to a corner is the defect this whole probe exists for, and it is measured
 * as a fraction of the frame rather than as a pixel count so that resizing the window does
 * not change the verdict.
 */
private fun judge(picture: Picture): String? {
    val counts = HashMap<Int, Int>()
    for (position in 0 until picture.width * picture.height) {
        val colour = picture.at(position % picture.width, position / picture.width)
        counts[colour] = (counts[colour] ?: 0) + 1
    }
    if (counts.size < 8) {
        return "the capture holds ${counts.size} distinct colours, which is not a rendered world"
    }
    val background = counts.maxByOrNull { it.value }!!.key
    var minX = picture.width
    var minY = picture.height
    var maxX = -1
    var maxY = -1
    for (y in 0 until picture.height) {
        for (x in 0 until picture.width) {
            if (picture.at(x, y) != background) {
                if (x < minX) minX = x
                if (y < minY) minY = y
                if (x > maxX) maxX = x
                if (y > maxY) maxY = y
            }
        }
    }
    if (maxX < 0) {
        return "the capture is a single flat colour"
    }
    val contentWidth = maxX - minX + 1
    val contentHeight = maxY - minY + 1
    // Half the frame in each direction is a deliberately generous floor: a dish at the
    // default zoom does not fill a square viewport to its edges, but it is nowhere near a
    // quarter of it either, and a picture smaller than that is the reported defect.
    if (contentWidth * 2 < picture.width || contentHeight * 2 < picture.height) {
        return "the drawn content covers ${contentWidth}x$contentHeight of " +
            "${picture.width}x${picture.height}, which is a picture in the corner of its " +
            "own viewport rather than a view of the dish"
    }
    return null
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

private fun surfaceDescription(): String = try {
    val diagnostics = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics"
    )
    val instance = diagnostics.getField("INSTANCE").get(null)
    diagnostics.getMethod("describe").invoke(instance) as? String
        ?: "the surface did not describe itself"
} catch (thrown: ReflectiveOperationException) {
    "the surface could not be described: $thrown"
}

private fun captureWindow(handle: Long, path: String): Int = try {
    val renderer = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer"
    )
    val instance = renderer.getField("INSTANCE").get(null)
    renderer.getMethod(
        "nativeCaptureWindow",
        java.lang.Long.TYPE,
        String::class.java
    ).invoke(instance, handle, path) as? Int ?: -1
} catch (thrown: ReflectiveOperationException) {
    println("CAPTURE could not reach the renderer: $thrown")
    -1
}
