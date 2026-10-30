/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.unit.Density
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepositoryTest
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.files.FileLoadCompatibility
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.CellularityApp
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuEffect
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuPage
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuStore
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme
import java.io.File
import java.nio.file.Files
import java.util.Collections
import javax.imageio.ImageIO
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

/**
 * Opens the application with a file, the way Windows does, and says what happened to it.
 *
 * `Cellularity "C:\...\Dish.substrate"` is what "Open with" produces once the association is
 * installed, and it is also what the association is for: the process starts with a path and no
 * other explanation of why. That path used to be read by nobody -- the window opened on the main
 * menu and the file was left where it was -- which is indistinguishable, from the outside, from an
 * association that does not work.
 *
 * What this drives is everything the entry point does after reading its arguments: the import is
 * performed by the same object the launcher calls, into the same repository the launcher built, and
 * the outcome is reported to a real main-menu store, whose state and effects are then read back.
 * The pages are composed over that store afterwards, so the claim being checked is not only that a
 * file arrived but that the interface the person is looking at has something new in it.
 *
 * The data directory is the probe's own, set by the task that runs it; the first thing the probe
 * does is refuse to continue if that is not true, because an import check that wrote into the
 * person's own collection would be worse than no check at all.
 *
 * Run through `:desktop:runImportProbe`.
 */
fun main() {
    val root = File("build/diagnostics/import-check").absoluteFile
    root.deleteRecursively()
    val outside = File(root, "outside").apply { mkdirs() }
    val environment = DesktopEnvironment.create()
    val dataDirectory = environment.dataDirectory.toAbsolutePath()
    if (!dataDirectory.startsWith(root.toPath())) {
        System.err.println("IMPORT refused: the data directory is $dataDirectory, not inside $root")
        kotlin.system.exitProcess(3)
    }
    runBlocking { environment.initialize() }
    val repository = environment.fileRepository
    var failures = 0

    fun step(what: String, expected: String, actual: String, matches: Boolean) {
        val mark = if (matches) "ok  " else "FAIL"
        if (!matches) failures++
        println("IMPORT $mark $what -> expected $expected, was $actual")
    }

    // The files a person might double-click. The plate is the container the storage tests build --
    // the strongest one the adapter can be shown without a body encoder -- and the genome is a
    // draft, which is the format this side writes and reads.
    val genome = GenomeDraft.editorDefaults { listOf(0.35f, 0.75f, 0.45f) }
    val openedGenome = File(outside, "From Outside.genome")
    openedGenome.writeBytes(GenomeDraftWireCodec.encode(genome))
    val openedPlate = File(outside, "Someone's Plate.substrate")
    openedPlate.writeBytes(
        DesktopCellularityFileRepositoryTest.releaseContainerBytes(
            geneCount = 5,
            documentKind = DesktopCellularityFileRepositoryTest.SUBSTRATE_KIND
        )
    )
    val notes = File(outside, "Notes.substrate")
    notes.writeText("these are notes, not a plate")

    val store = MainMenuStore()
    val effects = Collections.synchronizedList(mutableListOf<MainMenuEffect>())
    val scope = CoroutineScope(Dispatchers.Default)
    scope.launch { store.effects.collect { effect -> effects += effect } }

    // Both pages are photographed before anything is imported, so "the list gained a row" is
    // measured against the same page rather than against an expectation.
    val emptyGeneBank = render(environment, MainMenuPage.GENE_BANK, revision = 0L, "gene-bank-before.jpg")
    val emptyFreezer = render(environment, MainMenuPage.EXPERIMENTS, revision = 0L, "freezer-before.jpg")

    // 1. The genome, as the entry point would hand it over.
    val importedGenome = runBlocking {
        DesktopMainMenuImport.run(repository, store, openedGenome.toPath())
    }
    Thread.sleep(EFFECT_SETTLE_MILLIS)
    step(
        "the opened genome is imported",
        "From Outside",
        importedGenome?.displayName ?: "nothing",
        importedGenome?.displayName == "From Outside"
    )
    step(
        "its kind comes from its extension",
        "GENOME",
        importedGenome?.kind?.name ?: "nothing",
        importedGenome?.kind?.name == "GENOME"
    )
    val storedGenome = dataDirectory.resolve("substrates").resolve("From Outside.genome")
    step(
        "it is stored under its own name",
        "true",
        Files.isRegularFile(storedGenome).toString(),
        Files.isRegularFile(storedGenome)
    )
    val sameBytes = Files.isRegularFile(storedGenome) &&
        Files.readAllBytes(storedGenome).contentEquals(openedGenome.readBytes())
    step("the bytes stored are the bytes opened", "true", sameBytes.toString(), sameBytes)
    step(
        "the gene bank was told to re-read",
        "1",
        store.state.value.collectionRevisions.genomes.toString(),
        store.state.value.collectionRevisions.genomes == 1L
    )
    step(
        "the menu moved to the collection it belongs to",
        MainMenuPage.GENE_BANK.name,
        store.state.value.page.name,
        store.state.value.page == MainMenuPage.GENE_BANK
    )
    step(
        "the success was reported rather than swallowed",
        "ImportSucceeded",
        effects.lastOrNull()?.let { it::class.simpleName } ?: "nothing",
        effects.lastOrNull() == MainMenuEffect.ImportSucceeded
    )
    step(
        "no import is left running",
        "false",
        store.state.value.importInProgress.toString(),
        !store.state.value.importInProgress
    )

    // 2. The plate, which is the same road with the other collection at the end of it.
    val importedPlate = runBlocking {
        DesktopMainMenuImport.run(repository, store, openedPlate.toPath())
    }
    Thread.sleep(EFFECT_SETTLE_MILLIS)
    step(
        "the opened plate is imported",
        "Someone's Plate",
        importedPlate?.displayName ?: "nothing",
        importedPlate?.displayName == "Someone's Plate"
    )
    val plateInfo = runBlocking {
        (repository.readSubstrateInfo(StoredSubstrateId("Someone's Plate")) as?
            StorageResult.Success)?.value
    }
    step(
        "the plate is listed as a plate, not as a damaged file",
        FileLoadCompatibility.LOADABLE.name,
        plateInfo?.compatibility?.name ?: "unreadable",
        plateInfo?.compatibility == FileLoadCompatibility.LOADABLE
    )
    step(
        "the experiment page was told to re-read",
        "1",
        store.state.value.collectionRevisions.substrates.toString(),
        store.state.value.collectionRevisions.substrates == 1L
    )
    step(
        "an imported plate is what the application was asked to open",
        "OpenSubstrate",
        effects.lastOrNull()?.let { it::class.simpleName } ?: "nothing",
        effects.lastOrNull() is MainMenuEffect.OpenSubstrate
    )

    // 3. The same genome again. A second copy would be a silent overwrite of the one already saved,
    //    and the person would be told the import succeeded.
    val repeated = runBlocking {
        DesktopMainMenuImport.run(repository, store, openedGenome.toPath())
    }
    Thread.sleep(EFFECT_SETTLE_MILLIS)
    step(
        "opening the same file twice does not import it twice",
        "nothing",
        repeated?.displayName ?: "nothing",
        repeated == null
    )
    step(
        "the refusal carries its reason",
        "NAME_EXISTS",
        (effects.lastOrNull() as? MainMenuEffect.ImportFailed)?.error?.code?.name ?: "nothing",
        (effects.lastOrNull() as? MainMenuEffect.ImportFailed)?.error?.code?.name == "NAME_EXISTS"
    )
    // The shared store sends the person to the collection that holds the file even when the import
    // was refused for its name, so the list re-reads and the file that is already there is what
    // they see. That is the phone's rule and this probe follows it rather than inventing one: what
    // matters here is that the refusal is reported and that no second copy was made.
    step(
        "the person is shown the file that is already there",
        "GENE_BANK",
        store.state.value.page.name,
        store.state.value.page == MainMenuPage.GENE_BANK
    )

    // 4. A file that is not what its name claims.
    runBlocking { DesktopMainMenuImport.run(repository, store, notes.toPath()) }
    Thread.sleep(EFFECT_SETTLE_MILLIS)
    step(
        "a foreign file is refused for what it is",
        "UNSUPPORTED_FORMAT",
        (effects.lastOrNull() as? MainMenuEffect.ImportFailed)?.error?.code?.name ?: "nothing",
        (effects.lastOrNull() as? MainMenuEffect.ImportFailed)?.error?.code?.name ==
            "UNSUPPORTED_FORMAT"
    )

    // 5. What the pages draw now, with the revisions the imports produced -- which is exactly what
    //    the launcher passes to them when the store advances.
    val geneBank = render(environment, MainMenuPage.GENE_BANK, revision = 1L, "gene-bank-after.jpg")
    val freezer = render(environment, MainMenuPage.EXPERIMENTS, revision = 1L, "freezer-after.jpg")
    val geneBankDifference = FrameInspection.difference(emptyGeneBank, geneBank)
    val freezerDifference = FrameInspection.difference(emptyFreezer, freezer)
    step(
        "the gene bank page draws a list where it drew none before",
        "more than 0 differing pixels",
        "$geneBankDifference of ${SCENE_WIDTH * SCENE_HEIGHT}",
        geneBankDifference > 0
    )
    step(
        "the freezer page draws the imported plate",
        "more than 0 differing pixels",
        "$freezerDifference of ${SCENE_WIDTH * SCENE_HEIGHT}",
        freezerDifference > 0
    )

    // 6. A plate brought from a phone, which is the case the whole feature exists for: the file is
    //    imported like any other and then opened, because a plate that sits in the collection and
    //    cannot be opened is not an import, it is a file. The path is given by the task, since a
    //    plate with a body cannot be written by this side -- the desktop links no substrate encoder.
    val brought = System.getenv("CELLULARITY_PROBE_PLATE")?.takeIf(String::isNotBlank)?.let(::File)
    if (brought == null) {
        println(
            "IMPORT note: no plate was given (CELLULARITY_PROBE_PLATE), so opening an imported " +
                "plate was not measured"
        )
    } else if (!brought.isFile) {
        step("the plate given to the probe is a file", "true", "false", false)
    } else {
        val copy = File(outside, "Tardigrade.substrate")
        brought.copyTo(copy, overwrite = true)
        val importedBrought = runBlocking {
            DesktopMainMenuImport.run(repository, store, copy.toPath())
        }
        step(
            "a plate brought from a phone is imported",
            "Tardigrade",
            importedBrought?.displayName ?: "nothing",
            importedBrought?.displayName == "Tardigrade"
        )
        val storedPath = runBlocking {
            (repository.substrateFilePath(StoredSubstrateId("Tardigrade")) as?
                StorageResult.Success)?.value
        }
        step(
            "the imported plate has a path to open",
            "true",
            (storedPath != null).toString(),
            storedPath != null
        )
        val roots = DesktopAssetRoots.resolve()
        if (storedPath == null || roots == null) {
            step("the renderer can open the imported plate", "assets and a path", "missing", false)
        } else {
            // Opened the way the plate screen opens it: the same entry point, the same allocation
            // the launcher passes for a stored plate, and the same shaders and textures.
            val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(
                WORLD_WIDTH,
                WORLD_HEIGHT
            )
            val session = DesktopNativeRenderer.nativeCreateSessionFromSubstrate(
                windowHandle = anchor,
                width = WORLD_WIDTH,
                height = WORLD_HEIGHT,
                maxCellCount = SimulationContract.defaultConfiguration.maxCellCount,
                maxFoodCount = SimulationContract.defaultConfiguration.maxFoodCount,
                dishRadius = SimulationContract.defaultConfiguration.radius,
                shaderRoot = roots.shaderRoot.toString(),
                textureRoot = roots.textureRoot.toString(),
                substratePath = storedPath.toString()
            )
            step(
                "the imported plate opens a world",
                "a live session",
                session.toString(),
                session != 0L
            )
            if (session == 0L) {
                // The reason is the useful half of a refusal, and it is what the plate screen now
                // shows instead of "the simulation could not be opened".
                val problem = DesktopNativeRenderer.nativeDescribeSubstrateProblem(
                    substratePath = storedPath.toString(),
                    maxCellCount = SimulationContract.defaultConfiguration.maxCellCount,
                    maxFoodCount = SimulationContract.defaultConfiguration.maxFoodCount,
                    dishRadius = SimulationContract.defaultConfiguration.radius
                )
                println("IMPORT plateOpenProblem=${problem ?: "none reported"}")
            }
            if (session != 0L) {
                // The world is stepped while it is read, because a plate is a running experiment
                // rather than a picture: what has to be non-empty is the plate, not the frame.
                var cells = 0L
                var seconds = 0.0
                val deadline = System.nanoTime() + PLATE_OPEN_TIMEOUT_NANOS
                while (System.nanoTime() < deadline) {
                    DesktopNativeRenderer.nativeRenderFrame(session)
                    val stats = DesktopNativeRenderer.nativeReadStats(session)
                    cells = stats?.getOrNull(STATS_CELL_COUNT) ?: 0L
                    seconds = stats?.getOrNull(STATS_SIMULATION_TIME)?.let(Double::fromBits) ?: 0.0
                    if (cells > 0L) break
                    Thread.sleep(PLATE_OPEN_POLL_MILLIS)
                }
                step(
                    "the imported plate holds its cells",
                    "more than 0",
                    cells.toString(),
                    cells > 0L
                )
                step(
                    "the imported plate carries the time it was saved at",
                    "more than 0 seconds",
                    "$seconds s (${seconds / 3600.0} h)",
                    seconds > 0.0
                )
                DesktopNativeRenderer.nativeStopSession(session)
            }
            DesktopNativeRenderer.nativeDestroyWindow(anchor)
        }
    }

    // 7. The menu's own import action: the road a person uses when no file association is
    //    installed and there is nothing to double-click. Two frames of the same menu are compared
    //    -- one where the platform offers a chooser and one where it does not -- so what is
    //    measured is the action itself rather than the menu around it, and then the action is
    //    pressed through the scene's own pointer input to see that it asks the platform.
    val asked = java.util.concurrent.atomic.AtomicInteger(0)
    val withoutAction = renderMenu(store, environment, onImportRequest = null, "menu-without-import.jpg")
    val withAction = renderMenu(
        store,
        environment,
        onImportRequest = { asked.incrementAndGet() },
        "menu-with-import.jpg",
        press = true
    )
    val actionDifference = FrameInspection.difference(withoutAction, withAction)
    step(
        "the menu offers the platform's chooser where there is one",
        "more than 0 differing pixels",
        "$actionDifference",
        actionDifference > 0
    )
    step(
        "pressing it asks the platform for a file",
        "1",
        asked.get().toString(),
        asked.get() == 1
    )

    scope.cancel()
    println("IMPORT steps failed=$failures")
    System.out.flush()
    kotlin.system.exitProcess(if (failures == 0) 0 else 4)
}

/**
 * Composes one menu page against the collections and draws it.
 *
 * Which tab the person happens to be on is a different question, and one the store's state answers
 * above: what is being looked at here is the page the launcher would show for a collection
 * revision, composed the way the launcher composes it.
 */
private fun render(
    environment: DesktopEnvironment,
    page: MainMenuPage,
    revision: Long,
    name: String
): FrameInspection.Frame {
    val scene = ImageComposeScene(
        width = SCENE_WIDTH,
        height = SCENE_HEIGHT,
        density = Density(1f)
    )
    val scope = CoroutineScope(Dispatchers.Default)
    try {
        scene.setContent {
            CellularityTheme {
                DesktopMainMenuPages(
                    page = page,
                    collectionRevision = revision,
                    settings = environment.settingsRepository.settings.value,
                    environment = environment,
                    scope = scope,
                    notices = DesktopNoticeQueue(),
                    onOpenPlate = { }
                )
            }
        }
        // The pages read their collections through their stores, so the frames are drawn while
        // those reads complete: the first frame is drawn before the list exists.
        repeat(SETTLE_FRAMES - 1) {
            scene.render()
            Thread.sleep(SETTLE_MILLIS)
        }
        return photograph(scene, name)
    } finally {
        scene.close()
        scope.cancel()
    }
}

/**
 * Composes the whole menu -- the shared application around the desktop's pages -- and draws it.
 *
 * This is what the window shows, not a page on its own: the import action is the menu's, so the
 * frame it has to appear in is the menu's too.
 *
 * When [press] is set, the action is pressed before the frame is read: the point is taken from the
 * difference between the two frames rather than from a guessed coordinate, so the press lands on
 * what actually changed.
 */
private fun renderMenu(
    store: MainMenuStore,
    environment: DesktopEnvironment,
    onImportRequest: (() -> Unit)?,
    name: String,
    press: Boolean = false
): FrameInspection.Frame {
    val scene = ImageComposeScene(
        width = SCENE_WIDTH,
        height = SCENE_HEIGHT,
        density = Density(1f)
    )
    val scope = CoroutineScope(Dispatchers.Default)
    try {
        scene.setContent {
            CellularityApp(
                mainMenuState = store.state.value,
                onMainMenuAction = { },
                avoidCameraCutout = false,
                themeColors = environment.settingsRepository.settings.value.theme,
                themeComponents = environment.settingsRepository.settings.value.themeComponents,
                onImportRequest = onImportRequest
            ) { page, revision ->
                DesktopMainMenuPages(
                    page = page,
                    collectionRevision = revision,
                    settings = environment.settingsRepository.settings.value,
                    environment = environment,
                    scope = scope,
                    notices = DesktopNoticeQueue(),
                    onOpenPlate = { }
                )
            }
        }
        repeat(SETTLE_FRAMES - 1) {
            scene.render()
            Thread.sleep(SETTLE_MILLIS)
        }
        if (press) {
            val drawn = photograph(scene, name)
            val target = actionCentre(drawn, primaryColour(environment))
            if (target == null) {
                println("IMPORT pressTarget=none: the bar draws no import action to press")
            } else {
                println("IMPORT pressTarget=${target.first.toInt()},${target.second.toInt()}")
                scene.sendPointerEvent(
                    eventType = androidx.compose.ui.input.pointer.PointerEventType.Press,
                    position = androidx.compose.ui.geometry.Offset(target.first, target.second)
                )
                scene.sendPointerEvent(
                    eventType = androidx.compose.ui.input.pointer.PointerEventType.Release,
                    position = androidx.compose.ui.geometry.Offset(target.first, target.second)
                )
                repeat(SETTLE_FRAMES) {
                    scene.render()
                    Thread.sleep(SETTLE_MILLIS)
                }
            }
            return drawn
        }
        return photograph(scene, name)
    } finally {
        scene.close()
        scope.cancel()
    }
}

/**
 * The middle of the import action, found by the shape of the colour it is filled with.
 *
 * The action is a solid block of the theme's primary colour at the end of the menu's bar, and the
 * longest horizontal run of that colour in the bar is that block: the bars' own selected label is
 * drawn in the same colour, and a label is glyphs -- runs of a few pixels separated by gaps -- where
 * the action is one run the width of its padding. Taking the longest run rather than the bounding
 * box of every primary pixel is what keeps the two apart, and the target is measured rather than
 * written down because a coordinate written into a probe goes stale the next time the bar changes.
 */
private fun actionCentre(frame: FrameInspection.Frame, primaryColour: Int): Pair<Float, Float>? {
    val firstRow = (frame.height - BAR_SEARCH_HEIGHT).coerceAtLeast(0)
    var bestStart = -1
    var bestEnd = -1
    var bestRow = -1
    for (y in firstRow until frame.height) {
        var x = 0
        while (x < frame.width) {
            if (frame.pixels[y * frame.width + x] != primaryColour) {
                x++
                continue
            }
            val start = x
            while (x < frame.width && frame.pixels[y * frame.width + x] == primaryColour) x++
            if (x - start > bestEnd - bestStart) {
                bestStart = start
                bestEnd = x
                bestRow = y
            }
        }
    }
    if (bestRow < 0) return null
    return Pair((bestStart + bestEnd) / 2f, bestRow.toFloat())
}

/** The theme's fill colour, as the frame holds it: three bytes and no alpha. */
private fun primaryColour(environment: DesktopEnvironment): Int =
    environment.settingsRepository.settings.value.theme.primary
        .removePrefix("#")
        .toInt(16) and 0xFFFFFF

private fun photograph(scene: ImageComposeScene, name: String): FrameInspection.Frame {
    val image = scene.render()
    val bitmap = org.jetbrains.skia.Bitmap()
    bitmap.allocN32Pixels(SCENE_WIDTH, SCENE_HEIGHT)
    image.readPixels(bitmap)
    val pixels = IntArray(SCENE_WIDTH * SCENE_HEIGHT)
    val picture = java.awt.image.BufferedImage(
        SCENE_WIDTH,
        SCENE_HEIGHT,
        java.awt.image.BufferedImage.TYPE_INT_RGB
    )
    for (y in 0 until SCENE_HEIGHT) {
        for (x in 0 until SCENE_WIDTH) {
            val colour = bitmap.getColor(x, y)
            pixels[y * SCENE_WIDTH + x] = colour and 0xFFFFFF
            picture.setRGB(x, y, colour and 0xFFFFFF)
        }
    }
    val directory = File("build/diagnostics/import-check").apply { mkdirs() }
    val target = File(directory, name)
    ImageIO.write(picture, "jpg", target)
    return FrameInspection.Frame(SCENE_WIDTH, SCENE_HEIGHT, pixels)
}

private const val SCENE_WIDTH = 1200
private const val SCENE_HEIGHT = 780
private const val SETTLE_FRAMES = 20
private const val SETTLE_MILLIS = 50L
private const val EFFECT_SETTLE_MILLIS = 250L

/** A window for the plate that is opened, which is never shown: only its surface is used. */
private const val WORLD_WIDTH = 640
private const val WORLD_HEIGHT = 480

/** How long a plate is given to publish its cells once it has been opened. */
private const val PLATE_OPEN_TIMEOUT_NANOS = 20_000_000_000L
private const val PLATE_OPEN_POLL_MILLIS = 50L

/** How much of the bottom of the frame the menu's bar occupies, for looking the action up in it. */
private const val BAR_SEARCH_HEIGHT = 120

/** Where the runtime puts the two numbers this probe reads; see `nativeReadStats`. */
private const val STATS_CELL_COUNT = 3
private const val STATS_SIMULATION_TIME = 5
