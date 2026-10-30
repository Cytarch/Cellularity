/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorSection
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeScalarField
import java.awt.Dimension
import java.awt.Frame
import java.awt.Rectangle
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import javax.imageio.ImageIO

/**
 * Opens the desktop genome editor, drives it, and photographs it.
 *
 * Whether an editor works is not something a compile can establish: a section that throws
 * while composing, a list that renders empty, or a slider that changes state nothing reads
 * all build cleanly and all look like a working editor from the outside. So this opens the
 * real window, moves through the three sections the way a person would, checks that the
 * edits reached the shared reducer, and writes a picture of each section to be looked at.
 *
 * Run through `:desktop:runGenomeEditor`. The pictures land in `build/`.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val frame = AtomicReference<Frame?>(null)
    val panel = AtomicReference<ComposePanel?>(null)
    // A repository of its own under the build directory, so the probe never reads or writes
    // the genomes the person running it has saved.
    val store = DesktopGenomeEditorStore(
        repository = com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository(
            java.nio.file.Files.createTempDirectory("cellularity-editor-probe")
                .resolve("substrates")
        ),
        genomeProvider = object : com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider {
            override val genome: com.cytarch.cellularity.shared.model.GenomeDraft? = null
        }
    )

    javax.swing.SwingUtilities.invokeLater {
        try {
            val window = Frame("Cellularity genome editor")
            val composePanel = ComposePanel()
            composePanel.preferredSize = Dimension(1100, 700)
            window.add(composePanel)
            window.pack()
            window.setLocation(40, 40)
            composePanel.setContent {
                MaterialTheme {
                    DesktopGenomeEditor(store = store)
                }
            }
            panel.set(composePanel)
            frame.set(window)
            window.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(90, TimeUnit.SECONDS)) {
        System.err.println("GENOME the event thread never finished building the window")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        System.err.println("GENOME failed to open: $thrown")
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }

    val window = frame.get()!!
    // Let the first composition lay out and paint before anything is captured or asserted.
    settle(2_500)

    val outputDirectory = File("build")
    outputDirectory.mkdirs()

    val sections = listOf(
        GenomeEditorSection.GENERAL,
        GenomeEditorSection.SPLITTING,
        GenomeEditorSection.SPECIFIC
    )
    val captures = mutableListOf<String>()
    var editsReachedTheReducer = true

    for (section in sections) {
        store.dispatch(GenomeEditorAction.SelectSection(section))
        settle(900)

        val name = "genome-editor-${section.name.lowercase()}.png"
        capture(window, File(outputDirectory, name))?.let { captures += name }

        // The point of the editor is that an edit lands. A screen that draws but discards
        // its input is the failure this catches.
        val before = store.state.selectedGeneDraft.splitMass
        store.dispatch(GenomeEditorAction.SetScalar(GenomeScalarField.SPLIT_MASS, before + 0.05f))
        settle(200)
        if (store.state.selectedGeneDraft.splitMass == before) {
            editsReachedTheReducer = false
            System.err.println("GENOME an edit to split mass did not reach the reducer")
        }
    }

    val state = store.state
    println("GENOME window up, ${captures.size} section image(s) written: ${captures.joinToString()}")
    println("GENOME genes=${state.genome.genes.size} selectedGene=${state.selectedGene.value}")
    println("GENOME validationValid=${state.validation.isValid} dirty=${state.dirty}")
    println("GENOME editsReachedTheReducer=$editsReachedTheReducer")

    // The gene operations are what make a one-gene genome editable at all, and they are
    // the ones most likely to be quietly wrong, because a genome is an ordered list whose
    // genes name each other by position. So each is exercised and the count and validity
    // are checked after every step rather than only at the end.
    val operations = mutableListOf<String>()
    var operationsHeld = true

    fun check(step: String, expectedGenes: Int) {
        settle(250)
        val current = store.state
        val valid = current.validation.isValid && current.genome.genes.size == expectedGenes
        if (!valid) operationsHeld = false
        operations += "$step -> genes=${current.genome.genes.size} " +
            "valid=${current.validation.isValid} (expected $expectedGenes)"
        System.err.println("GENOME $step genes=${current.genome.genes.size} valid=${current.validation.isValid}")
    }

    store.dispatch(GenomeEditorAction.AddGene(GeneIndex(0), addGene(1)))
    check("add", 2)

    store.dispatch(GenomeEditorAction.SelectGene(GeneIndex(1)))
    store.dispatch(GenomeEditorAction.DuplicateGene(GeneIndex(1)))
    check("duplicate", 3)

    store.dispatch(GenomeEditorAction.MakeInitial(GeneIndex(2)))
    check("make initial", 3)

    store.dispatch(GenomeEditorAction.MoveGene(GeneIndex(2), GeneIndex(0)))
    check("move", 3)

    store.deleteGene(GeneIndex(1))
    check("delete", 2)

    store.deleteGene(GeneIndex(0))
    check("delete to the floor", 1)

    // The floor has to refuse rather than produce an unrepresentable genome. Refusing
    // loudly is the intent, so a throw is the pass and silence would be the failure.
    val floorBefore = store.state.genome.genes.size
    val refusedBelowTheFloor = try {
        store.deleteGene(GeneIndex(0))
        false
    } catch (_: IllegalArgumentException) {
        true
    }
    check("delete below the floor refuses", floorBefore)
    if (!refusedBelowTheFloor) operationsHeld = false
    println("GENOME deleteBelowTheFloorRefused=$refusedBelowTheFloor")

    println("GENOME gene operations:")
    operations.forEach { println("GENOME   $it") }
    println("GENOME operationsHeld=$operationsHeld")

    // A genome with several genes is what a person would actually want to look at, so the
    // last picture is taken of one rather than of the single gene every new genome starts
    // from.
    repeat(3) { index ->
        store.dispatch(
            GenomeEditorAction.AddGene(
                GeneIndex(store.state.genome.genes.size - 1),
                addGene(store.state.genome.genes.size)
            )
        )
    }
    settle(400)
    capture(window, File(outputDirectory, "genome-editor-many-genes.png"))

    println("GENOME fileState=${store.state.fileState}")
    System.out.flush()

    // The organism is grown for a day before the window is left open, so that an outside
    // photograph shows the preview working on something that has developed rather than on the
    // single cell every genome starts as.
    store.dispatch(GenomeEditorAction.SetPreviewTime(24.0))
    settle(6_000)
    capture(window, File(outputDirectory, "genome-editor-preview-24h.jpg"))

    // The window is held open after its pictures are taken, so that the frame the editor actually
    // paints can be photographed from outside the process. What the toolkit draws into a picture
    // of its own is not necessarily what reaches the screen, and the screen is the thing in
    // question.
    println("GENOME window held open for ${HOLD_MILLIS / 1000} s")
    System.out.flush()
    settle(HOLD_MILLIS)
    javax.swing.SwingUtilities.invokeLater {
        window.isVisible = false
        window.dispose()
    }
    val healthy = editsReachedTheReducer && operationsHeld && store.state.validation.isValid
    kotlin.system.exitProcess(if (healthy) 0 else 4)
}

/** A gene built for a given slot, the same way the editor builds one. */
private fun addGene(index: Int): com.cytarch.cellularity.shared.model.GeneDraft =
    DesktopGenomeEditorStore.createRandomizedEditorGene(index)

/** Waits for the interface to catch up, without pretending to know how long that takes. */
/** How long the window is left on screen for an outside observer to photograph. */
private const val HOLD_MILLIS = 45_000L

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

/**
 * Photographs the window itself, asking the window to draw rather than reading the screen.
 *
 * Reading the screen would photograph whatever else happens to be on it, which is both a
 * different picture from the one the editor drew and a picture of somebody's desktop.
 */
private fun capture(window: Frame, destination: File): File? = try {
    val size = window.size
    if (size.width <= 0 || size.height <= 0) {
        null
    } else {
        val image = java.awt.image.BufferedImage(
            size.width, size.height, java.awt.image.BufferedImage.TYPE_INT_RGB
        )
        // A window draws on the event thread of the toolkit that owns it, and this probe runs on
        // another one, so the drawing is asked for on that thread rather than demanded from here.
        javax.swing.SwingUtilities.invokeAndWait {
            val graphics = image.createGraphics()
            try {
                window.paintAll(graphics)
            } finally {
                graphics.dispose()
            }
        }
        ImageIO.write(image, "jpg", destination)
        destination
    }
} catch (thrown: Throwable) {
    // A capture can fail on a session with no usable display. That is worth saying, but it
    // is not the same as the editor being broken, so it does not stop the run.
    System.err.println("GENOME could not capture ${destination.name}: $thrown")
    null
}
