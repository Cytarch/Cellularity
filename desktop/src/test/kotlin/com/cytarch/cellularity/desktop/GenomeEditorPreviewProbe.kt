/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.unit.Density
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewVisualContract
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme
import java.io.File
import java.nio.file.Files
import javax.imageio.ImageIO

/**
 * Composes the genome editor, grows an organism in it, zooms the preview with the wheel, and
 * photographs what the panel drew.
 *
 * The editor's preview is a live view and not a picture: it publishes a genome and a view to a
 * renderer, hands over the person's gestures, and draws the frame that comes back. Every one of
 * those steps can fail silently -- a view that is published but never applied, a camera that is
 * never asked for a frame -- and the result would be a panel that looks like a working one until
 * somebody tries to move it. So the whole editor is composed here, the wheel is turned over the
 * panel the way a person turns it, and both frames are measured and written out.
 *
 * Run through `:desktop:runGenomeEditorPreview`. The pictures land in `build/`.
 */
fun main() {
    if (!DesktopNativeRenderer.isAvailable) {
        System.err.println("PREVIEWPANEL the renderer library was not built")
        kotlin.system.exitProcess(3)
    }
    if (DesktopAssetRoots.resolve() == null) {
        System.err.println("PREVIEWPANEL the compiled shaders and textures were not found")
        kotlin.system.exitProcess(3)
    }

    // A repository of its own under a temporary directory, so the probe never reads or writes the
    // genomes the person running it has saved.
    val store = DesktopGenomeEditorStore(
        repository = com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository(
            Files.createTempDirectory("cellularity-preview-panel-probe").resolve("substrates")
        ),
        genomeProvider = object : GenomeProvider {
            override val genome: GenomeDraft? = null
        }
    )

    val outputDirectory = File("build").apply { mkdirs() }
    val scene = ImageComposeScene(
        width = SCENE_WIDTH,
        height = SCENE_HEIGHT,
        density = Density(1f)
    )
    try {
        scene.setContent {
            // The application's own theme rather than Material's defaults, so the picture is of
            // what the editor shows inside the application and not of what it would look like
            // without one.
            CellularityTheme {
                DesktopGenomeEditor(store = store)
            }
        }
        // The session is opened on a thread of the preview's own and the first frame has to be
        // drawn before there is anything to look at, so the scene is drawn while that happens.
        renderFor(scene, SETTLE_FRAMES, SETTLE_MILLIS)

        // The organism is grown for a day, which is what makes a preview worth looking at: a
        // genome nobody has developed is one cell in the middle of the medium.
        store.dispatch(GenomeEditorAction.SetPreviewTime(PREVIEW_HOURS))
        renderFor(scene, GROWTH_FRAMES, GROWTH_MILLIS)

        val before = photograph(scene, outputDirectory, "genome-editor-preview-live.jpg")
        println("PREVIEWPANEL before $before")

        // What the panel holds is measured rather than assumed, because the editor around it draws
        // plenty: a frame with no organism in it at all would still have thousands of colours. The
        // organism has a colour -- the one the genome says its Cells are -- so the panel is asked
        // whether that colour is in it, and how much of the panel is still the panel's own fill.
        val panel = describePreviewPanel(
            frame = before,
            right = (SCENE_WIDTH * PREVIEW_PANEL_FRACTION).toInt(),
            organismColour = geneColour(store.state.selectedGeneDraft.color)
        )
        println("PREVIEWPANEL previewPanel $panel")

        // The wheel, over the middle of the preview panel, which is where a person's hand would
        // be. It is turned outwards rather than inwards for the same reason a person would turn it
        // that way: the preview opens on the creature rather than on the medium it swims in, so
        // turning out is what shows the organism whole. Nothing else is touched -- if the panel's
        // camera is live these frames differ, and if it is not, they are the same picture twice.
        val zoomCentre = Offset(SCENE_WIDTH * PREVIEW_PANEL_FRACTION / 2f, SCENE_HEIGHT / 2f)
        repeat(ZOOM_NOTCHES) {
            scene.sendPointerEvent(
                eventType = PointerEventType.Scroll,
                position = zoomCentre,
                scrollDelta = Offset(0f, 1f)
            )
        }
        renderFor(scene, SETTLE_FRAMES, SETTLE_MILLIS)

        val after = photograph(scene, outputDirectory, "genome-editor-preview-zoomed.jpg")
        println("PREVIEWPANEL after $after")

        val wholeFrame = FrameInspection.difference(before, after)
        val previewPanel = differenceWithin(
            before,
            after,
            left = 0,
            right = (SCENE_WIDTH * PREVIEW_PANEL_FRACTION).toInt()
        )
        println("PREVIEWPANEL difference wholeFrame=$wholeFrame previewPanel=$previewPanel")
        println("PREVIEWPANEL zoomMovedThePicture=${previewPanel > 0}")
        println(
            "PREVIEWPANEL panelPaintedTheOrganism=" +
                "${panel.organismPixels > 0} backgroundShare=${panel.backgroundPercent}%"
        )

        val healthy = panel.organismPixels > 0 && previewPanel > 0
        if (!healthy) {
            System.err.println(
                "PREVIEWPANEL the preview panel did not answer: $panel " +
                    "previewPanelDifference=$previewPanel"
            )
        }
        System.out.flush()
        kotlin.system.exitProcess(if (healthy) 0 else 4)
    } finally {
        scene.close()
    }
}

/** Draws the scene a number of times, waiting between frames for the work behind it to happen. */
private fun renderFor(scene: ImageComposeScene, frames: Int, millisBetweenFrames: Long) {
    repeat(frames) {
        scene.render()
        try {
            Thread.sleep(millisBetweenFrames)
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
            return
        }
    }
}

/**
 * Draws the scene once and writes what it drew, as a picture and as numbers.
 *
 * The frame is read out of the scene rather than off the screen: this is the same drawing the
 * window would show, without a window, a monitor or anything in front of anybody.
 */
private fun photograph(
    scene: ImageComposeScene,
    directory: File,
    name: String
): FrameInspection.Frame {
    val image = scene.render()
    val bitmap = org.jetbrains.skia.Bitmap()
    bitmap.allocN32Pixels(SCENE_WIDTH, SCENE_HEIGHT)
    image.readPixels(bitmap)
    val pixels = IntArray(SCENE_WIDTH * SCENE_HEIGHT)
    val picture = java.awt.image.BufferedImage(
        SCENE_WIDTH, SCENE_HEIGHT, java.awt.image.BufferedImage.TYPE_INT_RGB
    )
    for (y in 0 until SCENE_HEIGHT) {
        for (x in 0 until SCENE_WIDTH) {
            val colour = bitmap.getColor(x, y)
            pixels[y * SCENE_WIDTH + x] = colour and 0xFFFFFF
            picture.setRGB(x, y, colour and 0xFFFFFF)
        }
    }
    val target = File(directory, name)
    ImageIO.write(picture, "jpg", target)
    println("PREVIEWPANEL picture=${target.absolutePath}")
    return FrameInspection.Frame(SCENE_WIDTH, SCENE_HEIGHT, pixels)
}

/**
 * How many pixels two frames disagree about inside a region.
 *
 * The whole frame is not the measurement that matters here: the summary line counts Cells, and a
 * Cell count that changed would make two frames differ without the preview having moved. What the
 * wheel is supposed to change is the preview, so the preview is what is compared.
 */
private fun differenceWithin(
    first: FrameInspection.Frame,
    second: FrameInspection.Frame,
    left: Int,
    right: Int
): Int {
    require(first.width == second.width && first.height == second.height) {
        "two frames can only be compared when they are the same size"
    }
    var differing = 0
    for (y in 0 until first.height) {
        for (x in left until right) {
            if (first.pixels[y * first.width + x] != second.pixels[y * second.width + x]) {
                differing++
            }
        }
    }
    return differing
}

/** What the preview half of the editor holds, measured. */
private class PanelDescription(
    val colours: Int,
    val backgroundPercent: Int,
    val organismPixels: Int
) {
    override fun toString(): String =
        "colours=$colours backgroundShare=$backgroundPercent% organismPixels=$organismPixels"
}

/**
 * Measures the panel: how much of it is still its own fill, and whether the organism is in it.
 *
 * A panel that never received a frame is its own background colour with the editor's controls over
 * it, and the controls are enough to give a whole frame thousands of colours -- so "the frame is
 * not blank" is not the same claim as "the organism is drawn". The organism has a colour, the one
 * the genome says its Cells are, and finding that colour in the panel is the claim that can fail.
 */
private fun describePreviewPanel(
    frame: FrameInspection.Frame,
    right: Int,
    organismColour: Int
): PanelDescription {
    val panelFill = GenomePreviewVisualContract.BACKGROUND_ARGB and 0xFFFFFF
    val seen = HashSet<Int>()
    var fill = 0
    var organism = 0
    var total = 0
    for (y in 0 until frame.height) {
        for (x in 0 until right) {
            val colour = frame.pixels[y * frame.width + x]
            total++
            seen.add(colour)
            if (colour == panelFill) fill++
            if (isNear(colour, organismColour)) organism++
        }
    }
    return PanelDescription(
        colours = seen.size,
        backgroundPercent = if (total == 0) 0 else fill * 100 / total,
        organismPixels = organism
    )
}

/**
 * Whether a pixel is the organism's colour, allowing for the renderer's own shading of it.
 *
 * The cells are lit and outlined rather than filled flat, so the exact colour of the genome is not
 * the only colour it produces; what is being asked is whether the panel shows something of that
 * colour at all, which a panel holding only the editor's controls does not.
 */
private fun isNear(colour: Int, expected: Int): Boolean {
    val red = kotlin.math.abs(((colour ushr 16) and 0xFF) - ((expected ushr 16) and 0xFF))
    val green = kotlin.math.abs(((colour ushr 8) and 0xFF) - ((expected ushr 8) and 0xFF))
    val blue = kotlin.math.abs((colour and 0xFF) - (expected and 0xFF))
    return red <= ORGANISM_COLOUR_TOLERANCE &&
        green <= ORGANISM_COLOUR_TOLERANCE &&
        blue <= ORGANISM_COLOUR_TOLERANCE
}

/** The colour the genome says one of its genes has, as ARGB. */
private fun geneColour(components: List<Float>): Int {
    if (components.size < 3) return 0
    val red = (components[0].coerceIn(0f, 1f) * 255f).toInt()
    val green = (components[1].coerceIn(0f, 1f) * 255f).toInt()
    val blue = (components[2].coerceIn(0f, 1f) * 255f).toInt()
    return (red shl 16) or (green shl 8) or blue
}

private const val SCENE_WIDTH = 1200
private const val SCENE_HEIGHT = 760

/** The share of the editor the preview panel takes when the editor is wider than it is tall. */
private const val PREVIEW_PANEL_FRACTION = 0.5f

/** How long the organism is grown before the panel is photographed, in hours. */
private const val PREVIEW_HOURS = 24.0

private const val SETTLE_FRAMES = 30
private const val SETTLE_MILLIS = 60L
private const val GROWTH_FRAMES = 60
private const val GROWTH_MILLIS = 60L

/** Wheel notches turned over the panel: each is one step of the zoom the viewport uses. */
private const val ZOOM_NOTCHES = 18

/** How far a pixel may differ from the genome's own colour and still be called the organism. */
private const val ORGANISM_COLOUR_TOLERANCE = 10
