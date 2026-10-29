package com.cytarch.cellularity.shared.model

/**
 * Immutable data needed to present diagnostics for one selected simulation cell.
 *
 * The genome is optional because it is a separate document from the cell on both platforms:
 * the runtime publishes the cell every frame and the genome when it changes, so there are
 * moments when one is known and the other is not. A panel that showed nothing in those moments
 * would be hiding the parts it does know, and an absent genome means the genome sections are
 * left out rather than filled in with a genome nobody read.
 */
data class CellInspectionSnapshot(
    val cell: CellSnapshot,
    val genome: GenomeDraft?
)
