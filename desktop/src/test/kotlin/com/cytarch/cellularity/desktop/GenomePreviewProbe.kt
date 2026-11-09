/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import java.nio.file.Files
import java.nio.file.Path

/**
 * Draws the organism a genome grows into, and writes it out as a picture.
 *
 * This is the check the genome editor's preview rests on: the editor asks the renderer for a
 * picture of the creature its current draft encodes, and a preview that came back empty, or as a
 * single flat colour, or as a corner of something larger would look exactly like a working one in
 * a test that only read a status code. So the probe writes the picture to disk where a person can
 * look at it, and reports how many colours it holds.
 *
 * The genome it previews is taken from the plate itself: a cell is placed with the synthesizer,
 * selected with the diagnosis tool, and inspected -- the same road a user's genome takes to the
 * renderer, which means this probe also establishes that placing a cell by clicking works.
 *
 * Run through `:desktop:runGenomePreviewProbe`.
 */
fun main() {
    if (!DesktopNativeRenderer.isAvailable) {
        System.err.println("GENOMEPREVIEW the renderer library was not built")
        kotlin.system.exitProcess(3)
    }
    val roots = DesktopAssetRoots.resolve()
    if (roots == null) {
        System.err.println("GENOMEPREVIEW the compiled shaders and textures were not found")
        kotlin.system.exitProcess(3)
    }

    val width = 640
    val height = 480
    val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(width, height)
    if (anchor == 0L) {
        System.err.println("GENOMEPREVIEW the renderer could not create its surface anchor")
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
            System.err.println("GENOMEPREVIEW the simulation did not start")
            kotlin.system.exitProcess(4)
        }
        val pixels = java.nio.ByteBuffer.allocateDirect(width * height * BYTES_PER_PIXEL)
        DesktopNativeRenderer.nativeSetTemperatureMode(session, OBSERVE_MODE)

        // The plate is emptied first, so a cell found here afterwards is one this probe placed: on
        // a plate that seeds itself, "a cell exists" says nothing about the tap.
        DesktopNativeRenderer.nativeResetWorld(session)
        DesktopNativeRenderer.nativeSetToolOrdinal(session, SYNTHESIZER_TOOL)
        drawFrames(session, pixels, width, height, count = 6)

        val queued = DesktopNativeRenderer.nativeQueueTap(session, width / 2f, height / 2f)
        drawFrames(session, pixels, width, height, count = 8)
        println("GENOMEPREVIEW tap queued=$queued")

        // A placed cell is found by selecting it: the population counter is a published snapshot
        // and lags the world, while a selection is the answer to this very tap.
        DesktopNativeRenderer.nativeSetToolOrdinal(session, DIAGNOSIS_TOOL)
        DesktopNativeRenderer.nativeQueueTap(session, width / 2f, height / 2f)
        drawFrames(session, pixels, width, height, count = 6)
        val genome = DesktopNativeRenderer.nativeInspectSelectedGenome(session)
        if (genome == null || genome.isEmpty()) {
            System.err.println(
                "GENOMEPREVIEW a tap on empty medium placed no cell: nothing was selected where " +
                    "the synthesizer was used, so there is no genome to draw"
            )
            kotlin.system.exitProcess(5)
        }
        println("GENOMEPREVIEW genome bytes=${genome.size}")

        val edge = DesktopNativeRenderer.nativeGenomePreviewPixels()
        println("GENOMEPREVIEW edge=$edge")
        val preview = java.nio.ByteBuffer.allocateDirect(edge * edge * BYTES_PER_PIXEL)
        // Drawn by a renderer of the preview's own, which has no window: an editor that stood up
        // a plate to draw one creature would carry a simulation it never uses, and a preview that
        // needed a window could not be shown in a scene that draws everything itself.
        // The preview is anchored to a surface of its own: a backend's device is bound to the
        // surface it was created against, and the plate's anchor is already carrying one.
        val previewAnchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(16, 16)
        val previewSession = DesktopNativeRenderer.nativeCreateGenomePreviewSession(
            windowHandle = previewAnchor,
            shaderRoot = roots.shaderRoot.toString(),
            textureRoot = roots.textureRoot.toString()
        )
        println("GENOMEPREVIEW preview session=$previewSession")
        if (previewSession == 0L) {
            System.err.println("GENOMEPREVIEW a preview session of its own was refused")
            kotlin.system.exitProcess(6)
        }
        // The interactive path, published the way the phone publishes it: content, then the view,
        // then what the person did. A status of zero from each is the endpoint saying it accepted
        // the message, which is what makes this more than a compiled declaration.
        val contentStatus = DesktopNativeRenderer.nativePublishGenomePreviewContent(
            sessionHandle = previewSession,
            genomeBytes = genome,
            simulationTimeHours = PREVIEW_HOURS
        )
        val viewStatus = DesktopNativeRenderer.nativePublishGenomePreviewViewState(
            sessionHandle = previewSession,
            selectedGeneIndex = 0,
            gridEnabled = true,
            treeEnabled = false,
            viewportWidth = edge,
            viewportHeight = edge
        )
        // A camera centres and a tap lands in the viewport's own pixels, which is the space the
        // editor hands them over in: a fraction of the panel would be a different language from
        // the one the preview speaks, and a tap written that way simply misses.
        val zoomStatus = DesktopNativeRenderer.nativeEnqueueGenomePreviewInteraction(
            sessionHandle = previewSession,
            kind = 2,
            first = 1.15f,
            second = edge / 2f,
            third = edge / 2f
        )
        val selectStatus = DesktopNativeRenderer.nativeEnqueueGenomePreviewInteraction(
            sessionHandle = previewSession,
            kind = 3,
            first = edge / 2f,
            second = edge / 2f,
            third = 0.0f
        )
        println(
            "GENOMEPREVIEW publish content=$contentStatus view=$viewStatus " +
                "zoom=$zoomStatus select=$selectStatus"
        )

        val status = DesktopNativeRenderer.nativeCaptureGenomePreview(
            sessionHandle = previewSession,
            genomeBytes = genome,
            maximumSimulationTime = PREVIEW_HOURS,
            pixels = preview,
            rowStride = edge * BYTES_PER_PIXEL
        )
        println("GENOMEPREVIEW status=$status")
        println("GENOMEPREVIEW distinct colours=${distinctColours(preview, edge * edge)}")
        println("GENOMEPREVIEW ink=${describeInk(preview, edge)}")
        writeJpeg(preview, edge, edge, Path.of("build", "diagnostics", "genome-preview.jpg"))

        // The picture the editor's panel actually shows, which is not the saved genome's picture:
        // it is the session's own world, drawn through the session's own camera, into memory the
        // caller owns. Drawn into a Skia bitmap for the same reason the panel does -- the address
        // the renderer is given is that bitmap's memory, so what is measured here is the path the
        // panel takes rather than a path that resembles it.
        val live = LivePicture(edge)
        val liveStatus = DesktopNativeRenderer.nativeDrawGenomePreviewIntoAddress(
            sessionHandle = previewSession,
            address = live.address,
            rowStride = live.rowStride,
            capacityBytes = live.rowStride.toLong() * live.edge
        )
        // Read after the frame, not before it: the camera is published with the picture it was
        // taken through, so the one worth reporting is the one this picture came from.
        val before = cameraOf(previewSession)
        println("GENOMEPREVIEW live status=$liveStatus")
        println("GENOMEPREVIEW live ${describePreviewStatus(previewSession)}")
        println("GENOMEPREVIEW live camera=$before")
        println("GENOMEPREVIEW live colours=${live.distinctColours()} ink=${live.describeInk()}")
        writeJpeg(live, Path.of("build", "diagnostics", "genome-preview-live.jpg"))

        // The camera is what makes this a preview rather than a photograph, so it is moved through
        // the same road a person's gesture takes and the second picture is drawn from it. A zoom
        // that was accepted and never reached the picture would look exactly like a working one in
        // the status code the enqueue returns.
        val zoom = DesktopNativeRenderer.nativeEnqueueGenomePreviewInteraction(
            sessionHandle = previewSession,
            kind = 2,
            first = ZOOM_FACTOR,
            second = edge / 2f,
            third = edge / 2f
        )
        val zoomed = LivePicture(edge)
        val zoomedStatus = DesktopNativeRenderer.nativeDrawGenomePreviewIntoAddress(
            sessionHandle = previewSession,
            address = zoomed.address,
            rowStride = zoomed.rowStride,
            capacityBytes = zoomed.rowStride.toLong() * zoomed.edge
        )
        val after = cameraOf(previewSession)
        println("GENOMEPREVIEW zoom accepted=$zoom status=$zoomedStatus")
        println("GENOMEPREVIEW zoomed ${describePreviewStatus(previewSession)}")
        println("GENOMEPREVIEW zoomed camera=$after")
        println("GENOMEPREVIEW zoomed colours=${zoomed.distinctColours()} ink=${zoomed.describeInk()}")
        println(
            "GENOMEPREVIEW zoomed pixelsChanged=" +
                "${live.pixelsDifferingFrom(zoomed)} of ${edge * edge}"
        )
        writeJpeg(zoomed, Path.of("build", "diagnostics", "genome-preview-zoomed.jpg"))
        println(
            "GENOMEPREVIEW cameraMoved=" +
                "${before.zoom != after.zoom || before.centreX != after.centreX || before.centreY != after.centreY}"
        )
        live.close()
        zoomed.close()

        DesktopNativeRenderer.nativeStopSession(previewSession)
        System.out.flush()
    } finally {
        if (session != 0L) {
            DesktopNativeRenderer.nativeStopSession(session)
        }
        DesktopNativeRenderer.nativeDestroyWindow(anchor)
    }
}

private fun drawFrames(
    session: Long,
    pixels: java.nio.ByteBuffer,
    width: Int,
    height: Int,
    count: Int
) {
    var drawn = 0
    var attempts = 0
    while (drawn < count && attempts < count * 40) {
        attempts++
        val status = DesktopNativeRenderer.nativeRenderFrameInto(
            sessionHandle = session,
            pixels = pixels,
            width = width,
            height = height,
            rowStride = width * BYTES_PER_PIXEL
        )
        if (status == FRAME_DRAWN) drawn++ else Thread.sleep(4)
    }
}

private fun distinctColours(pixels: java.nio.ByteBuffer, pixelCount: Int): Int {
    val seen = HashSet<Int>()
    pixels.position(0)
    val sample = pixels.asIntBuffer()
    var index = 0
    while (index < pixelCount) {
        seen.add(sample.get(index))
        if (seen.size > MAXIMUM_COLOURS) return seen.size
        index += SAMPLE_STRIDE
    }
    return seen.size
}

/**
 * Where the picture has ink, and how much of it there is.
 *
 * A preview is only useful if the creature is in it: a picture holding a direction arrow and
 * nothing else has a colour count and a status code exactly like a good one, so the extent of
 * what was drawn is measured rather than inferred.
 */
private fun describeInk(pixels: java.nio.ByteBuffer, edge: Int): String {
    pixels.position(0)
    var ink = 0
    var left = edge
    var right = -1
    var top = edge
    var bottom = -1
    for (y in 0 until edge) {
        for (x in 0 until edge) {
            val offset = (y * edge + x) * BYTES_PER_PIXEL
            val alpha = pixels.get(offset + 3).toInt() and 0xFF
            if (alpha == 0) continue
            ink++
            if (x < left) left = x
            if (x > right) right = x
            if (y < top) top = y
            if (y > bottom) bottom = y
        }
    }
    if (ink == 0) return "none: the preview is empty"
    return "pixels=$ink box=${right - left + 1}x${bottom - top + 1} at $left,$top"
}

private fun writeJpeg(
    pixels: java.nio.ByteBuffer,
    width: Int,
    height: Int,
    target: Path
) {
    Files.createDirectories(target.parent)
    val image = java.awt.image.BufferedImage(
        width, height, java.awt.image.BufferedImage.TYPE_INT_RGB
    )
    pixels.position(0)
    val row = ByteArray(width * BYTES_PER_PIXEL)
    for (y in 0 until height) {
        pixels.get(row)
        for (x in 0 until width) {
            val offset = x * BYTES_PER_PIXEL
            val alpha = (row[offset + 3].toInt() and 0xFF) / 255.0
            // Composited onto white, because the renderer draws an organism onto transparency:
            // a dark creature read without its alpha becomes indistinguishable from the paper it
            // is printed on, which is how a working preview can look like an empty one.
            val red = ((row[offset].toInt() and 0xFF) * alpha + 255.0 * (1.0 - alpha)).toInt()
            val green = ((row[offset + 1].toInt() and 0xFF) * alpha + 255.0 * (1.0 - alpha)).toInt()
            val blue = ((row[offset + 2].toInt() and 0xFF) * alpha + 255.0 * (1.0 - alpha)).toInt()
            image.setRGB(x, y, (red shl 16) or (green shl 8) or blue)
        }
    }
    javax.imageio.ImageIO.write(image, "jpg", target.toFile())
    println("GENOMEPREVIEW picture=${target.toAbsolutePath()}")
}

/** The same, for a picture the preview drew into a bitmap of the probe's own. */
private fun writeJpeg(picture: LivePicture, target: Path) {
    Files.createDirectories(target.parent)
    val image = java.awt.image.BufferedImage(
        picture.edge, picture.edge, java.awt.image.BufferedImage.TYPE_INT_RGB
    )
    for (y in 0 until picture.edge) {
        for (x in 0 until picture.edge) {
            image.setRGB(x, y, compositeOnWhite(picture.colourAt(x, y)))
        }
    }
    javax.imageio.ImageIO.write(image, "jpg", target.toFile())
    println("GENOMEPREVIEW picture=${target.toAbsolutePath()}")
}

/** A colour with its alpha applied over white, which is what a person looking at it would see. */
private fun compositeOnWhite(argb: Int): Int {
    val alpha = (argb ushr 24) and 0xFF
    if (alpha == 255) return argb and 0xFFFFFF
    val weight = alpha / 255.0
    val red = (((argb ushr 16) and 0xFF) * weight + 255.0 * (1.0 - weight)).toInt()
    val green = (((argb ushr 8) and 0xFF) * weight + 255.0 * (1.0 - weight)).toInt()
    val blue = ((argb and 0xFF) * weight + 255.0 * (1.0 - weight)).toInt()
    return (red shl 16) or (green shl 8) or blue
}

/**
 * A picture the preview draws into, held the way the editor's panel holds it.
 *
 * The memory belongs to a Skia bitmap and the renderer is given the address of that memory, so
 * there is no copy between the renderer and what reads it -- which is the whole point of this path,
 * and the reason a probe that measured a copy would be measuring something the panel never does.
 */
private class LivePicture(val edge: Int) {
    private val bitmap = org.jetbrains.skia.Bitmap()
    private val pixmap: org.jetbrains.skia.Pixmap
    val address: Long
    val rowStride: Int = edge * BYTES_PER_PIXEL

    init {
        val allocated = bitmap.allocPixels(
            org.jetbrains.skia.ImageInfo(
                edge,
                edge,
                org.jetbrains.skia.ColorType.RGBA_8888,
                org.jetbrains.skia.ColorAlphaType.PREMUL,
                org.jetbrains.skia.ColorSpace.sRGB
            ),
            rowStride
        )
        check(allocated) { "the probe could not allocate a ${edge}x$edge picture" }
        val pixels = bitmap.peekPixels() ?: error("the probe's picture has no memory")
        pixmap = pixels
        address = pixels.addr
        check(address != 0L) { "the probe's picture has no address" }
    }

    /** The colour at a point, as ARGB with straight alpha. */
    fun colourAt(x: Int, y: Int): Int = bitmap.getColor(x, y)

    fun distinctColours(): Int {
        val seen = HashSet<Int>()
        var y = 0
        while (y < edge) {
            var x = 0
            while (x < edge) {
                seen.add(colourAt(x, y))
                if (seen.size > MAXIMUM_COLOURS) return seen.size
                x += SAMPLE_STRIDE
            }
            y += SAMPLE_STRIDE
        }
        return seen.size
    }

    /** Where the picture has ink, and how much of it there is. */
    fun describeInk(): String {
        var ink = 0
        var left = edge
        var right = -1
        var top = edge
        var bottom = -1
        for (y in 0 until edge) {
            for (x in 0 until edge) {
                if ((colourAt(x, y) ushr 24) and 0xFF == 0) continue
                ink++
                if (x < left) left = x
                if (x > right) right = x
                if (y < top) top = y
                if (y > bottom) bottom = y
            }
        }
        if (ink == 0) return "none: the preview is empty"
        return "pixels=$ink box=${right - left + 1}x${bottom - top + 1} at $left,$top"
    }

    /** How many pixels two pictures of the same size disagree about. */
    fun pixelsDifferingFrom(other: LivePicture): Int {
        require(other.edge == edge) { "two pictures can only be compared at one size" }
        var differing = 0
        for (y in 0 until edge) {
            for (x in 0 until edge) {
                if (colourAt(x, y) != other.colourAt(x, y)) differing++
            }
        }
        return differing
    }

    fun close() {
        pixmap.close()
        bitmap.close()
    }
}

/** Where the preview's camera stands, as the renderer reports it. */
private data class PreviewCamera(val centreX: Float, val centreY: Float, val zoom: Float) {
    override fun toString(): String =
        "centre=(%.4f, %.4f) zoom=%.4f".format(centreX, centreY, zoom)
}

/**
 * The preview's camera, read from the renderer rather than assumed.
 *
 * The three camera numbers come back as float bit patterns, the way the plate's food mass does: a
 * long cannot hold a fraction, and a rounded camera is a camera nobody set.
 */
private fun cameraOf(session: Long): PreviewCamera {
    val status = DesktopNativeRenderer.nativeReadGenomePreviewStatus(session)
    if (status == null || status.size < PREVIEW_STATUS_LENGTH) {
        return PreviewCamera(Float.NaN, Float.NaN, Float.NaN)
    }
    return PreviewCamera(
        centreX = Float.fromBits(status[6].toInt()),
        centreY = Float.fromBits(status[7].toInt()),
        zoom = Float.fromBits(status[8].toInt())
    )
}

/** The rest of the preview's account: what it grew, and which gene a tap chose. */
private fun describePreviewStatus(session: Long): String {
    val status = DesktopNativeRenderer.nativeReadGenomePreviewStatus(session)
    if (status == null || status.size < PREVIEW_STATUS_LENGTH) return "unavailable"
    return "cells=${status[0]} of ${status[2]} selection=${status[3]}/${status[4]} " +
        "viewportRevision=${status[5]}"
}

private const val BYTES_PER_PIXEL = 4
private const val OBSERVE_MODE = 1
private const val SYNTHESIZER_TOOL = 0
private const val DIAGNOSIS_TOOL = 4
private const val FRAME_DRAWN = 0
private const val MAXIMUM_COLOURS = 4096
private const val SAMPLE_STRIDE = 37
private const val PREVIEW_STATUS_LENGTH = 9

/** How much closer the camera is asked to move, which a picture either shows or does not. */
private const val ZOOM_FACTOR = 2.0f

/** How long the organism is grown before it is drawn, in hours. */
private const val PREVIEW_HOURS = 48.0
