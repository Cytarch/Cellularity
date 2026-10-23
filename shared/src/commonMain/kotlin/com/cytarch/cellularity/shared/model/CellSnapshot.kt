package com.cytarch.cellularity.shared.model

/** Immutable application snapshot of one native cell. */
class CellSnapshot(
    val id: Int,
    val positionX: Double,
    val positionY: Double,
    val positionZ: Double,
    val radius: Double,
    val age: Double,
    val mass: Double,
    val nitrates: Double,
    val velocityX: Double,
    val velocityY: Double,
    val angle: Double,
    val angularVelocity: Double,
    val toxin: Float,
    val toxication: Float,
    val lipids: Float,
    val geneIndex: Int,
    val originTag: Int,
    val selectTag: Int,
    val mutationCount: Int,
    val telomeresCount: Int,
    val mirrored: Boolean,
    val cellType: CellType,
    color: Iterable<Float>,
    signalSubstance: Iterable<Float>,
    newSignalSubstance: Iterable<Float>,
    val organismId: Long = 0,
    val parentOrganismId: Long = 0
) {
    val color: List<Float> = immutableListCopy(color)
    val signalSubstance: List<Float> = immutableListCopy(signalSubstance)
    val newSignalSubstance: List<Float> = immutableListCopy(newSignalSubstance)
}
