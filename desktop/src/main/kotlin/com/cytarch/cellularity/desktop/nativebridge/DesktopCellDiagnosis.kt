/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.CellSnapshotWireCodec
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec

/**
 * Reads the runtime's own account of which cell is selected, and of what that cell is.
 *
 * Cell diagnosis is the one place where the two platforms read the same native document for
 * the same purpose -- a person selects a cell and asks what it is -- so the questions are
 * asked here in the same words the phone asks them in, and the answer is decoded by the codec
 * the two platforms share.
 *
 * Everything in this file is a reading of the runtime's published status rather than a command
 * to it. The runtime decides for itself when a diagnosis is wanted: it answers only while the
 * diagnosis tool is selected and a cell is selected, and the flag it publishes says whether
 * the document that goes with the status exists at all. Asking for a snapshot without that
 * flag would be asking for a document that was never written, which is a refusal rather than
 * an error and has to be read as one.
 */
object DesktopCellDiagnosis {

    /** Nothing is selected; the runtime publishes this rather than zero, because zero is a cell. */
    const val NO_SELECTION = -1

    /**
     * The cell the runtime currently has selected, or null when it has none.
     *
     * Null is the ordinary answer for most of a session -- a newly opened plate has nothing
     * selected until somebody taps it -- so it is not a failure and callers treat it as the
     * absence of a selection rather than as a broken read.
     */
    fun selectedCellId(sessionHandle: Long): Int? {
        val status = DesktopNativeRenderer.nativeReadStats(sessionHandle) ?: return null
        if (status.size <= STATUS_SELECTED_CELL_ID) return null
        val selected = status[STATUS_SELECTED_CELL_ID].toInt()
        return selected.takeIf { it != NO_SELECTION }
    }

    /**
     * What the selected cell is, or null when the runtime is not currently answering.
     *
     * The flag is checked before the document is read, because the two are published as one
     * status: a status that says no diagnosis is wanted is a status whose matching document
     * was not written, and asking anyway would turn an ordinary absence into a refusal the
     * caller has to interpret.
     */
    fun inspectSelectedCell(sessionHandle: Long): CellInspectionSnapshot? {
        val status = DesktopNativeRenderer.nativeReadStats(sessionHandle) ?: return null
        if (status.size <= STATUS_DIAGNOSIS_REQUESTED) return null
        if (status[STATUS_DIAGNOSIS_REQUESTED] == 0L) return null
        val selected = status[STATUS_SELECTED_CELL_ID].toInt()
        if (selected == NO_SELECTION) return null

        // The document carries the cell. The genome is a second document, because the runtime
        // publishes them separately: the cell changes every frame and the genome does not, and
        // the panel shows both because they describe the same organism. A cell whose genome
        // cannot be read is reported with the genome it does have rather than not at all --
        // the panel's first sections are about the cell and are worth showing either way.
        val document = DesktopNativeRenderer.nativeInspectCell(sessionHandle, selected)
            ?: return null
        // A malformed document is not thrown away quietly: it is a disagreement between the
        // runtime and the codec about a shared format, which is the one thing that cannot be
        // discovered by either side alone, so it is allowed to surface.
        val cell = CellSnapshotWireCodec.decode(document) ?: return null
        return CellInspectionSnapshot(cell = cell, genome = readGenome(sessionHandle))
    }

    /**
     * The selected cell's genome, or null when there is not one to read.
     *
     * Null rather than a stand-in genome, because the panel reads a gene by the index the cell
     * reports and a stand-in would put values in front of a person that no organism has. The
     * snapshot carries the genome as optional for exactly this: the sections that need a gene
     * are left out and the rest of the panel still describes the cell.
     */
    private fun readGenome(sessionHandle: Long): GenomeDraft? {
        val bytes = DesktopNativeRenderer.nativeInspectSelectedGenome(sessionHandle)
            ?: return null
        return try {
            GenomeDraftWireCodec.decode(bytes)
        } catch (_: RuntimeException) {
            null
        }
    }

    /**
     * Where the fields the runtime publishes appear in its status array.
     *
     * Documented where the array is filled in the bridge, which is the side that decides the
     * order; these names exist so that the reading code does not speak in bare integers.
     */
    private const val STATUS_SELECTED_CELL_ID = 8
    private const val STATUS_DIAGNOSIS_REQUESTED = 10
}
