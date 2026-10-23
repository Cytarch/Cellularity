package com.cytarch.cellularity.shared.model

/** Immutable data needed to present diagnostics for one selected simulation cell. */
data class CellInspectionSnapshot(
    val cell: CellSnapshot,
    val genome: GenomeDraft
)
