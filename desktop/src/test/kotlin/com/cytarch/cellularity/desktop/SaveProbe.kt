/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopSubstratePreviewEncoder
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.SimulationContract
import java.io.File
import kotlinx.coroutines.runBlocking

/**
 * Saves a running plate and opens what was saved, measuring both.
 *
 * The save is the one operation that has to be right about a file the person cannot check: they are
 * told it was saved, and everything after that depends on the file being a plate with the same world
 * in it. So the probe does not stop at "the file exists": it opens the saved file in a session of its
 * own and compares the world it holds with the one that was saved -- cells and simulated time -- and
 * it asks the repository to describe the saved plate the way the freezer's card does, because a card
 * that shows zero cells for a plate holding thousands is the same fault seen from the other side.
 *
 * The picture is measured separately and honestly: the capture that needs the renderer's own thread
 * is not available to a probe that has no surface, so what is measured here is that an encoded
 * preview is accepted by the container, comes back byte for byte, and is reported by the card as
 * available. A plate written without a picture is a complete plate, and the probe says which of the
 * two it produced rather than assuming.
 *
 * Run through `:desktop:runSaveProbe -PopenedPlate=<a plate with a body>`.
 */
fun main() {
    val root = File("build/diagnostics/save-check").absoluteFile
    root.deleteRecursively()
    val environment = DesktopEnvironment.create()
    val dataDirectory = environment.dataDirectory.toAbsolutePath()
    if (!dataDirectory.startsWith(root.toPath())) {
        System.err.println("SAVE refused: the data directory is $dataDirectory, not inside $root")
        kotlin.system.exitProcess(3)
    }
    runBlocking { environment.initialize() }
    val repository = environment.fileRepository
    var failures = 0

    fun step(what: String, expected: String, actual: String, matches: Boolean) {
        val mark = if (matches) "ok  " else "FAIL"
        if (!matches) failures++
        println("SAVE $mark $what -> expected $expected, was $actual")
    }

    val source = System.getenv("CELLULARITY_PROBE_PLATE")
        ?.takeIf(String::isNotBlank)
        ?.let(::File)
    if (source == null || !source.isFile) {
        println(
            "SAVE note: no plate was given (CELLULARITY_PROBE_PLATE), and this side cannot write " +
                "one with a body, so nothing was saved"
        )
        kotlin.system.exitProcess(0)
    }

    val roots = DesktopAssetRoots.resolve()
    if (roots == null) {
        println("SAVE FAIL the renderer's shaders and textures were not found, so nothing could run")
        kotlin.system.exitProcess(4)
    }
    val sourceName = "Probe Source"
    val savedName = "Probe Saved"
    val storedSource = runBlocking {
        val resolved = repository.resolveExternalFile(source.toPath())
        if (resolved is StorageResult.Failure) {
            println("SAVE FAIL the plate given to the probe could not be read: ${resolved.error}")
            kotlin.system.exitProcess(4)
        }
        val imported = repository.importExternalFile(
            (resolved as StorageResult.Success).value.copy(
                displayName = sourceName,
                fileName = "$sourceName.substrate"
            )
        )
        if (imported is StorageResult.Failure) {
            println("SAVE FAIL the plate given to the probe could not be stored: ${imported.error}")
            kotlin.system.exitProcess(4)
        }
        repository.substrateFilePath(StoredSubstrateId(sourceName))
    }
    val storedPath = (storedSource as? StorageResult.Success)?.value
    if (storedPath == null) {
        println("SAVE FAIL the stored plate has no path")
        kotlin.system.exitProcess(4)
    }

    val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(WORLD_WIDTH, WORLD_HEIGHT)
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
    step("the plate given to the probe opens a world", "a live session", session.toString(), session != 0L)
    if (session == 0L) {
        DesktopNativeRenderer.nativeDestroyWindow(anchor)
        kotlin.system.exitProcess(4)
    }

    try {
        // The world is stepped until it has cells, because a plate is a running experiment: what is
        // being saved is the world on screen, and an empty world would make the comparison below
        // meaningless.
        var cellsBefore = 0L
        var secondsBefore = 0.0
        val deadline = System.nanoTime() + PLATE_READY_TIMEOUT_NANOS
        while (System.nanoTime() < deadline) {
            DesktopNativeRenderer.nativeRenderFrame(session)
            val stats = DesktopNativeRenderer.nativeReadStats(session)
            cellsBefore = stats?.getOrNull(STATS_CELL_COUNT) ?: 0L
            secondsBefore = stats?.getOrNull(STATS_SIMULATION_TIME)?.let(Double::fromBits) ?: 0.0
            if (cellsBefore > 0L) break
            Thread.sleep(PLATE_READY_POLL_MILLIS)
        }
        step("the plate has something to save", "more than 0 cells", cellsBefore.toString(), cellsBefore > 0L)

        val gateway = DesktopSimulationGateway(
            sessionHandle = session,
            fileRepository = repository,
            openedPlate = StoredSubstrateId(sourceName),
            previewCapture = null
        )
        val saved = runBlocking {
            gateway.savePlate(StoredSubstrateId(savedName), replaceExisting = false)
        }
        step(
            "saving the running plate succeeds",
            "a stored plate",
            when (saved) {
                is StorageResult.Success -> "written"
                is StorageResult.Failure -> "${saved.error.code} ${saved.error.detail}"
            },
            saved is StorageResult.Success
        )
        if (saved is StorageResult.Failure) {
            kotlin.system.exitProcess(4)
        }
        val savedPath = runBlocking { repository.substrateFilePath(StoredSubstrateId(savedName)) }
        val savedFile = (savedPath as? StorageResult.Success)?.value
        step(
            "the saved plate is a file in the collection",
            "a file",
            savedFile?.toString() ?: "none",
            savedFile != null && savedFile.toFile().isFile
        )

        // The card's own reading of the saved plate: the numbers the freezer shows a person.
        val card = runBlocking { repository.readSubstrateInfo(StoredSubstrateId(savedName)) }
        val info = (card as? StorageResult.Success)?.value
        step(
            "the card shows the cells the plate holds",
            "$cellsBefore (the loaded plate)",
            (info?.cellCount ?: -1).toString(),
            info != null && info.cellCount == cellsBefore.toInt()
        )
        step(
            "the card shows the time the plate was saved with",
            "about ${"%.2f".format(secondsBefore / 3600.0)} h",
            "${"%.2f".format((info?.time ?: -1.0))} h",
            info != null && info.time > 0.0 &&
                kotlin.math.abs(info.time * 3600.0 - secondsBefore) < SAVED_TIME_TOLERANCE_SECONDS
        )
        step(
            "the card reports no picture for a plate saved without one",
            "false",
            (info?.previewAvailable ?: true).toString(),
            info?.previewAvailable == false
        )

        // What was saved, opened again: the only check that matters about a save.
        val reopenAnchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(WORLD_WIDTH, WORLD_HEIGHT)
        val reopened = DesktopNativeRenderer.nativeCreateSessionFromSubstrate(
            windowHandle = reopenAnchor,
            width = WORLD_WIDTH,
            height = WORLD_HEIGHT,
            maxCellCount = SimulationContract.defaultConfiguration.maxCellCount,
            maxFoodCount = SimulationContract.defaultConfiguration.maxFoodCount,
            dishRadius = SimulationContract.defaultConfiguration.radius,
            shaderRoot = roots.shaderRoot.toString(),
            textureRoot = roots.textureRoot.toString(),
            substratePath = savedFile?.toString().orEmpty()
        )
        step("the saved plate opens again", "a live session", reopened.toString(), reopened != 0L)
        if (reopened != 0L) {
            var cellsAfter = 0L
            var secondsAfter = 0.0
            val reopenDeadline = System.nanoTime() + PLATE_READY_TIMEOUT_NANOS
            while (System.nanoTime() < reopenDeadline) {
                DesktopNativeRenderer.nativeRenderFrame(reopened)
                val stats = DesktopNativeRenderer.nativeReadStats(reopened)
                cellsAfter = stats?.getOrNull(STATS_CELL_COUNT) ?: 0L
                secondsAfter = stats?.getOrNull(STATS_SIMULATION_TIME)?.let(Double::fromBits) ?: 0.0
                if (cellsAfter > 0L) break
                Thread.sleep(PLATE_READY_POLL_MILLIS)
            }
            step(
                "the world that was saved is the world that comes back",
                "$cellsBefore cells (a living plate may have divided since)",
                cellsAfter.toString(),
                cellsAfter >= cellsBefore && cellsAfter <= cellsBefore + CELL_DIVISION_TOLERANCE
            )
            step(
                "the saved world keeps its clock",
                "at least ${"%.1f".format(secondsBefore)} s",
                "${"%.1f".format(secondsAfter)} s",
                secondsAfter >= secondsBefore
            )
            DesktopNativeRenderer.nativeStopSession(reopened)
        }
        DesktopNativeRenderer.nativeDestroyWindow(reopenAnchor)

        // The picture, in the two steps a probe can honestly take: an encoded frame the container
        // accepts, and the card noticing that the plate now has one.
        val previewBytes = previewFrame()
        step(
            "a preview frame encodes to what the container accepts",
            "RIFF....WEBP",
            describePreview(previewBytes),
            isWebp(previewBytes)
        )
        DesktopNativeRenderer.nativeAttachSubstratePreview(savedFile.toString(), previewBytes)
        val readBack = DesktopNativeRenderer.nativeReadSubstratePreview(savedFile.toString())
        step(
            "the container keeps the picture it was given",
            "${previewBytes.size} bytes",
            "${readBack?.size ?: 0} bytes",
            readBack != null && readBack.contentEquals(previewBytes)
        )
        val cardWithPicture = runBlocking { repository.readSubstrateInfo(StoredSubstrateId(savedName)) }
        step(
            "the card reports the picture the plate now has",
            "true",
            ((cardWithPicture as? StorageResult.Success)?.value?.previewAvailable ?: false).toString(),
            (cardWithPicture as? StorageResult.Success)?.value?.previewAvailable == true
        )
        val loadedPicture = runBlocking { repository.readSubstratePreview(StoredSubstrateId(savedName)) }
        step(
            "the card's own reader returns that picture",
            "${previewBytes.size} bytes",
            "${(loadedPicture as? StorageResult.Success)?.value?.size ?: 0} bytes",
            (loadedPicture as? StorageResult.Success)?.value?.contentEquals(previewBytes) == true
        )
    } finally {
        DesktopNativeRenderer.nativeStopSession(session)
        DesktopNativeRenderer.nativeDestroyWindow(anchor)
    }

    println("SAVE steps failed=$failures")
    System.out.flush()
    kotlin.system.exitProcess(if (failures == 0) 0 else 4)
}

/** A preview frame of the size the format uses, filled with something rather than nothing. */
private fun previewFrame(): ByteArray {
    val side = DesktopNativeRenderer.nativeSubstratePreviewPixels()
    val rgba = ByteArray(side * side * BYTES_PER_PIXEL)
    for (pixel in 0 until side * side) {
        val offset = pixel * BYTES_PER_PIXEL
        rgba[offset] = (pixel % 256).toByte()
        rgba[offset + 1] = ((pixel / 256) % 256).toByte()
        rgba[offset + 2] = 0x40
        rgba[offset + 3] = 0xff.toByte()
    }
    return DesktopSubstratePreviewEncoder.encodeWebpLossy(rgba, side, side, side * BYTES_PER_PIXEL)
        ?: ByteArray(0)
}

/** The format's own signature: `RIFF`, a size, then `WEBP`. */
private fun isWebp(bytes: ByteArray): Boolean =
    bytes.size > 12 &&
        bytes[0] == 'R'.code.toByte() &&
        bytes[1] == 'I'.code.toByte() &&
        bytes[2] == 'F'.code.toByte() &&
        bytes[3] == 'F'.code.toByte() &&
        bytes[8] == 'W'.code.toByte() &&
        bytes[9] == 'E'.code.toByte() &&
        bytes[10] == 'B'.code.toByte() &&
        bytes[11] == 'P'.code.toByte()

private fun describePreview(bytes: ByteArray): String =
    if (bytes.isEmpty()) "nothing" else "${bytes.size} bytes"

private const val WORLD_WIDTH = 640
private const val WORLD_HEIGHT = 480
private const val BYTES_PER_PIXEL = 4
private const val STATS_CELL_COUNT = 3
private const val STATS_SIMULATION_TIME = 5
private const val PLATE_READY_TIMEOUT_NANOS = 20_000_000_000L
private const val PLATE_READY_POLL_MILLIS = 50L

/** A living plate may divide while it is being saved and reopened; a few cells is that, not a fault. */
private const val CELL_DIVISION_TOLERANCE = 8L

/** The saved time must be the world's own, not the wall clock's. */
private const val SAVED_TIME_TOLERANCE_SECONDS = 5.0
